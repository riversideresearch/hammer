/*
 * Internal regex/RVM backend definitions.
 *
 * This header is installed so external parser extensions can provide
 * compile_to_rvm implementations for custom regular parsers. It is not part of
 * Hammer's stable public API.
 */
#ifndef HAMMER_BACKEND_REGEX__H
#define HAMMER_BACKEND_REGEX__H

#include "../hammer.h"

#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct HDiagnosticContext_ HDiagnosticContext;

// each insn is an 8-bit opcode and a 16-bit parameter
// [a] are actions; they add an instruction to the stackvm that is being output.
// [m] are match ops; they can either succeed or fail, depending on the current character
// [c] are control ops. They affect the pc non-linearly.
typedef enum HRVMOp_ {
    RVM_ACCEPT,  // [a]
    RVM_GOTO,    // [c] parameter is an offset into the instruction table
    RVM_FORK,    // [c] parameter is an offset into the instruction table
    RVM_PUSH,    // [a] No arguments, just pushes a mark (pointer to some
                 //     character in the input string) onto the stack
    RVM_ACTION,  // [a] argument is an action ID
    RVM_CAPTURE, // [a] Capture the last string (up to the current
                 //     position, non-inclusive), and push it on the
                 //     stack. No arg.
    RVM_EOF,     // [m] Succeeds only if at EOF.
    RVM_MATCH,   // [m] The high byte of the parameter is an upper bound
                 //     and the low byte is a lower bound, both
                 //     inclusive. An inverted match should be handled
                 //     as two ranges.
    RVM_STEP,    // [a] Step to the next byte of input
    RVM_OPCOUNT
} HRVMOp;

// Stack VM
typedef enum HSVMOp_ {
    SVM_PUSH,    // Push a mark. There is no VM insn to push an object.
    SVM_NOP,     // Used to start the chain, and possibly elsewhere. Does nothing.
    SVM_ACTION,  // Same meaning as RVM_ACTION
    SVM_CAPTURE, // Same meaning as RVM_CAPTURE
    SVM_ACCEPT,
    SVM_OPCOUNT
} HSVMOp;

typedef struct HRVMInsn_ {
    uint8_t op;
    uint16_t arg;
} HRVMInsn;

#define TT_MARK TT_RESERVED_1

typedef enum HSVMFailureKind_ {
    SVM_FAILURE_NONE = 0,
    SVM_FAILURE_INT_RANGE,
    SVM_FAILURE_FLOAT_RANGE,
    SVM_FAILURE_SEMANTIC_PREDICATE,
} HSVMFailureKind;

typedef struct HSVMFailure_ {
    HSVMFailureKind kind;
    size_t start;
    size_t end;
    HTokenType actual_type;
    int64_t lower;
    int64_t upper;
    double float_lower;
    double float_upper;
    double float_actual;
    union {
        int64_t sint;
        uint64_t uint;
    } actual;
    const char *parser;
} HSVMFailure;

typedef struct HSVMContext_ {
    HParsedToken **stack;
    size_t stack_count; // number of items on the stack. Thus stack[stack_count] is the first unused
                        // item on the stack.
    size_t stack_capacity;
    size_t input_pos;
    const HParser *parser;
    const HDiagnosticContext *diagnostic_context;
    struct HActionPlan_ *action_plan;
    void *action_plan_frames;
    HSVMFailure failure;
} HSVMContext;

// These actions all assume that the items on the stack are not
// aliased anywhere.
typedef bool (*HSVMActionFunc)(HArena *arena, HSVMContext *ctx, void *env);
typedef struct HSVMAction_ {
    HSVMActionFunc action;
    void *env;
} HSVMAction;

typedef struct HRVMTrace_ {
    struct HRVMTrace_ *next; // When parsing, these are
                             // reverse-threaded. There is a postproc
                             // step that inverts all the pointers.
    size_t input_pos;
    const HParser *parser;
    const HDiagnosticContext *diagnostic_context;
    uint16_t arg;
    uint8_t opcode;
} HRVMTrace;

struct HRVMProg_ {
    HAllocator *allocator;
    HArena *arena; // storage for action payloads that outlive their source parsers
    size_t length;
    size_t action_count;
    HRVMInsn *insns;
    const HParser **insn_parsers;
    const HDiagnosticContext **insn_contexts;
    HSVMAction *actions;
    const HParser *current_parser;
    const HDiagnosticContext *current_context;
    size_t next_choice_id;
    const HParser *root_parser;
    jmp_buf except;
};

// Returns true IFF the provided parser could be compiled.
bool h_compile_regex(HRVMProg *prog, const HParser *parser);

// These functions are used by the compile_to_rvm method of HParser
uint16_t h_rvm_create_action(HRVMProg *prog, HSVMActionFunc action_func, void *env);

// Allocate an action payload that remains valid for the lifetime of prog.
void *h_rvm_alloc(HRVMProg *prog, size_t size);

// returns the address of the instruction just created
uint16_t h_rvm_insert_insn(HRVMProg *prog, HRVMOp op, uint16_t arg);

// returns the address of the next insn to be created.
uint16_t h_rvm_get_ip(HRVMProg *prog);

// Used to insert forward references; the idea is to generate a JUMP
// or FORK instruction with a target of 0, then update it once the
// correct target is known.
void h_rvm_patch_arg(HRVMProg *prog, uint16_t ip, uint16_t new_val);

// Common SVM action funcs...
bool h_svm_action_make_sequence(HArena *arena, HSVMContext *ctx, void *env);
bool h_svm_action_clear_to_mark(HArena *arena, HSVMContext *ctx, void *env);

extern HParserBackendVTable h__regex_backend_vtable;

#endif
