/* Copyright (c) 2026 Riverside Research */
#ifndef HAMMER_BACKENDS_TRACE__H
#define HAMMER_BACKENDS_TRACE__H

struct HTraceState_;

#include "internal.h" /* HParser, HParseState, HParseResult */

#include <stddef.h>

typedef enum HTraceNumericRangeKind_ {
    H_TRACE_NUMERIC_RANGE_NONE = 0,
    H_TRACE_NUMERIC_RANGE_SINT,
    H_TRACE_NUMERIC_RANGE_UINT,
    H_TRACE_NUMERIC_RANGE_FLOAT
} HTraceNumericRangeKind;

typedef struct HTraceNumericRange_ {
    HTraceNumericRangeKind kind;
    union {
        int64_t sint;
        uint64_t uint;
        double floating;
    } actual;
    union {
        struct {
            int64_t lower;
            int64_t upper;
        } integer;
        struct {
            double lower;
            double upper;
        } floating;
    } expected;
} HTraceNumericRange;

#define H_TRACE_MAX_DISPATCH_OPCODES 16

typedef struct HTraceDispatchFailure_ {
    bool present;
    bool has_opcode;
    size_t opcode;
    uint32_t expected[H_TRACE_MAX_DISPATCH_OPCODES];
    size_t expected_count;
    bool expected_truncated;
} HTraceDispatchFailure;

#define H_TRACE_MAX_FAILURE_CANDIDATES 16
#define H_TRACE_MAX_CANDIDATE_CHOICE_DEPTH 8
#define H_TRACE_MAX_CANDIDATE_FRAMES 16

typedef struct HTraceCandidateFrame_ {
    const HParser *parser;
    size_t start;
} HTraceCandidateFrame;

typedef struct HTraceCandidateChoice_ {
    const HParser *choice;
    const HParser *origin;
    size_t alternative;
    size_t id;
} HTraceCandidateChoice;

typedef struct HTraceFailureCandidate_ {
    size_t start;
    size_t end;
    HParseErrorKind kind;
    const HParser *parser;
    const HDiagnosticContext *provenance;
    bool expected_bytes[256];
    bool expected_eof;
    HTraceCandidateChoice choices[H_TRACE_MAX_CANDIDATE_CHOICE_DEPTH];
    size_t choice_depth;
    HTraceCandidateFrame input_frames[H_TRACE_MAX_CANDIDATE_FRAMES];
    size_t input_frame_count;
} HTraceFailureCandidate;

#define H_TRACE_MAX_CHOICE_NODES 16
#define H_TRACE_MAX_CHOICE_ALTERNATIVES 16
#define H_TRACE_CHOICE_NONE SIZE_MAX

typedef struct HTraceChoiceAlternative_ {
    size_t alternative;
    HParseError error;
    bool expected_bytes[256];
    bool expected_eof;
    bool input_too_short;
    HTraceNumericRange numeric_range;
    HTraceDispatchFailure dispatch_failure;
    size_t child_node;
    size_t next;
} HTraceChoiceAlternative;

typedef struct HTraceChoiceNode_ {
    size_t first_alternative;
    size_t alternative_count;
    size_t furthest_progress;
    bool truncated;
} HTraceChoiceNode;

#define H_PARSE_DIAGNOSTIC_MAX_INPUT_FRAMES 64

typedef struct HTraceFrame_ {
    const HParser *parser;
    const char *name;
    size_t start;
    uint8_t start_bit;
    size_t reached;
    uint8_t reached_bit;
    unsigned long failure_serial;
} HTraceFrame;

struct HParseDiagnostic_ {
    HParseError error;
    bool expected_bytes[256];
    bool expected_eof;
    bool input_too_short;
    HTraceNumericRange numeric_range;
    HTraceDispatchFailure dispatch_failure;
    HTraceChoiceNode choice_nodes[H_TRACE_MAX_CHOICE_NODES];
    size_t choice_node_count;
    HTraceChoiceAlternative choice_alternatives[H_TRACE_MAX_CHOICE_ALTERNATIVES];
    size_t choice_alternative_count;
    size_t choice_root;
    HTraceFrame input_frames[H_PARSE_DIAGNOSTIC_MAX_INPUT_FRAMES];
    size_t input_frame_count;
    char *execution_trace;
    size_t execution_trace_length;
};

typedef struct HRVMTrace_ HRVMTrace;
typedef struct HSVMContext_ HSVMContext;

/* Render the failure reason, position, and expectations shared by the main
 * diagnostic and individual choice alternatives. Parser/source framing is
 * deliberately left to the caller. */
void h_trace_fprint_error_detail(FILE *stream, const HParseDiagnostic *diagnostic,
                                 bool choice_failure);

/* Compile-time master switch for diagnostic collection and execution tracing.
 * Runtime options and collected failures are owned by an HTraceState attached
 * to one HInputStream; ordinary parses carry NULL and pay only the guard cost.
 * Define HAMMER_TRACE_AST=0 to compile this support out entirely. */
#ifndef HAMMER_TRACE_AST
#define HAMMER_TRACE_AST 1
#endif
#if HAMMER_TRACE_AST

// packrat trace functions
HTraceState *h_trace_state_new(bool print_summary);
void h_trace_state_free(HTraceState *trace);
bool h_trace_is_enabled(const HTraceState *trace);
bool h_trace_is_dump_enabled(const HTraceState *trace);
bool h_trace_should_print_summary(const HTraceState *trace);
void h_trace_get_error(const HTraceState *trace, HParseError *out);
void h_trace_get_diagnostic(const HTraceState *trace, HParseDiagnostic **out);
void h_trace_begin(HTraceState *trace, const uint8_t *input, size_t input_len);
void h_trace_enter(const HParser *parser, HParseState *state);
void h_trace_exit(const HParser *parser, HParseState *state, HParseResult *res, const char *note);
void h_trace_end(HParseResult *res, HParseState *state);
void h_trace_fprint_input_context(FILE *stream, const uint8_t *input, size_t length,
                                  size_t start_index, size_t end_index);
void h_trace_note_int_range(HTraceState *trace, const HParsedToken *token, int64_t lower,
                            int64_t upper);
void h_trace_note_float_range(HTraceState *trace, const HParsedToken *token, double lower,
                              double upper);
void h_trace_note_dispatch(HTraceState *trace, const HParsedToken *token, bool has_opcode,
                           size_t opcode, const OpcodeMap *map, size_t count);
/* Record a failure diagnosed by the parser itself. The message must have
 * static lifetime; the completed public diagnostic receives an owned copy.
 * SIZE_MAX for both offsets preserves the furthest child-failure span. */
void h_trace_note_failure(HTraceState *trace, HParseErrorKind kind, const char *message,
                          size_t start, size_t end);
size_t h_trace_choice_begin(HTraceState *trace);
void h_trace_choice_arm_begin(HTraceState *trace, size_t scope, size_t alternative);
void h_trace_choice_arm_end(HTraceState *trace, size_t scope, bool success);
void h_trace_choice_end(HTraceState *trace, size_t scope, bool success);

// context-free backend trace functions
void h_cf_trace_begin(HTraceState *trace, HParserBackend backend, const HParser *parser,
                      const uint8_t *input, size_t input_len);
void h_cf_trace_failure(HTraceState *trace, size_t start, size_t end, HParseErrorKind kind,
                        const HParser *parser, const HDiagnosticContext *provenance,
                        const bool expected[256], bool expected_eof);
void h_cf_trace_parser_enter(HTraceState *trace, const HParser *parser, size_t index,
                             const char *role);
void h_cf_trace_parser_exit(HTraceState *trace, const HParser *parser, size_t start, size_t end,
                            const HParsedToken *token, bool success, const char *role);
void h_cf_trace_lr_shift(HTraceState *trace, size_t branch, size_t from_state, size_t to_state,
                         size_t index, const HParser *parser, const HDiagnosticContext *provenance,
                         const HParsedToken *token);
void h_cf_trace_lr_reduce(HTraceState *trace, size_t branch, size_t from_state, size_t to_state,
                          size_t length, size_t start, size_t end, const HParser *parser,
                          const HDiagnosticContext *provenance, const HParsedToken *token,
                          bool success);
void h_cf_trace_lr_error(HTraceState *trace, size_t branch, size_t state, size_t index,
                         const HParser *parser, const HDiagnosticContext *provenance);
size_t h_cf_trace_glr_fork(HTraceState *trace, size_t branch, size_t state, size_t index);
void h_cf_trace_glr_merge(HTraceState *trace, size_t survivor, size_t merged, size_t state,
                          size_t index);
void h_cf_trace_end(HTraceState *trace, bool success);

/* Shared backend diagnostic entry points (also used by the regex VM). */
void h_backend_trace_begin(HTraceState *trace, HParserBackend backend, const HParser *parser,
                           const uint8_t *input, size_t input_len);
void h_backend_trace_failure(HTraceState *trace, size_t start, size_t end, HParseErrorKind kind,
                             const HParser *parser, const HDiagnosticContext *provenance,
                             const bool expected[256], bool expected_eof);
void h_backend_trace_failures(HTraceState *trace, const HTraceFailureCandidate *candidates,
                              size_t count);
void h_trace_fprint_choice(FILE *stream, const HTraceChoiceNode *nodes, size_t node_count,
                           const HTraceChoiceAlternative *alternatives, size_t alternative_count,
                           size_t root);
void h_trace_fprint_input_trail(FILE *stream, const HTraceFrame *frames, size_t frame_count);
size_t h_cf_trace_candidates(const HCFChoice *root, const uint8_t *input, size_t input_pos,
                             size_t input_len, size_t start, size_t index, HParseErrorKind kind,
                             const HParser *fallback, HTraceFailureCandidate candidates[]);
void h_backend_trace_end(HTraceState *trace, bool success);

// regex trace functions
char *h_trace_parser_name(const HParser *parser);
void dump_rvm_prog(HTraceState *trace_state, HRVMProg *prog);
void dump_svm_prog(HTraceState *trace_state, HRVMProg *prog, HRVMTrace *trace);
void rvm_match_error(HTraceState *trace_state, HRVMProg *prog, const uint8_t *input,
                     size_t input_len, const HTraceFailureCandidate *candidates,
                     size_t candidate_count, HRVMTrace *trace);
void svm_action_error(HSVMContext *ctx, HRVMProg *orig_prog, HRVMTrace *trace, const uint8_t *input,
                      size_t input_len, const char *msg);
void svm_failure_error(HSVMContext *ctx, HRVMProg *orig_prog, HRVMTrace *trace,
                       const uint8_t *input, size_t input_len);

#define TRACE_ENABLED(trace) h_trace_is_enabled((trace))
#define TRACE_GET_ERROR(trace, out) h_trace_get_error((trace), (out))
#define TRACE_GET_DIAGNOSTIC(trace, out) h_trace_get_diagnostic((trace), (out))
#define TRACE_BEGIN(trace, input, len) h_trace_begin((trace), (input), (size_t)(len))
#define TRACE_ENTER(p, s) h_trace_enter((p), (s))
#define TRACE_EXIT(p, s, res, note) h_trace_exit((p), (s), (res), (note))
#define TRACE_END(res, state) h_trace_end((res), (state))
#define TRACE_CHOICE_BEGIN(trace) h_trace_choice_begin((trace))
#define TRACE_CHOICE_ARM_BEGIN(trace, scope, alternative)                                          \
    h_trace_choice_arm_begin((trace), (scope), (alternative))
#define TRACE_CHOICE_ARM_END(trace, scope, success)                                                \
    h_trace_choice_arm_end((trace), (scope), (success))
#define TRACE_CHOICE_END(trace, scope, success) h_trace_choice_end((trace), (scope), (success))
#define CF_TRACE_BEGIN(trace, backend, parser, input, len)                                         \
    h_cf_trace_begin((trace), (backend), (parser), (input), (size_t)(len))
#define CF_TRACE_FAILURE(trace, start, end, kind, parser, provenance, expected, expected_eof)      \
    h_cf_trace_failure((trace), (start), (end), (kind), (parser), (provenance), (expected),        \
                       (expected_eof))
#define CF_TRACE_PARSER_ENTER(trace, parser, index, role)                                          \
    h_cf_trace_parser_enter((trace), (parser), (index), (role))
#define CF_TRACE_PARSER_EXIT(trace, parser, start, end, token, success, role)                      \
    h_cf_trace_parser_exit((trace), (parser), (start), (end), (token), (success), (role))
#define CF_TRACE_LR_SHIFT(trace, branch, from, to, index, parser, provenance, token)               \
    h_cf_trace_lr_shift((trace), (branch), (from), (to), (index), (parser), (provenance), (token))
#define CF_TRACE_LR_REDUCE(trace, branch, from, to, length, start, end, parser, provenance, token, \
                           success)                                                                \
    h_cf_trace_lr_reduce((trace), (branch), (from), (to), (length), (start), (end), (parser),      \
                         (provenance), (token), (success))
#define CF_TRACE_LR_ERROR(trace, branch, state, index, parser, provenance)                         \
    h_cf_trace_lr_error((trace), (branch), (state), (index), (parser), (provenance))
#define CF_TRACE_GLR_FORK(trace, branch, state, index)                                             \
    h_cf_trace_glr_fork((trace), (branch), (state), (index))
#define CF_TRACE_GLR_MERGE(trace, survivor, merged, state, index)                                  \
    h_cf_trace_glr_merge((trace), (survivor), (merged), (state), (index))
#define CF_TRACE_END(trace, success) h_cf_trace_end((trace), (success))

#else /* tracing compiled out */

#define h_trace_state_new(dump) ((HTraceState *)NULL)
#define h_trace_state_free(trace) ((void)0)
#define h_trace_note_int_range(trace, token, lower, upper) ((void)0)
#define h_trace_note_float_range(trace, token, lower, upper) ((void)0)
#define h_trace_note_dispatch(trace, token, has_opcode, opcode, map, count) ((void)0)
#define h_trace_note_failure(trace, kind, message, start, end)                                     \
    ((void)(trace), (void)(kind), (void)(message), (void)(start), (void)(end))
#define h_trace_fprint_choice(stream, nodes, node_count, alternatives, alternative_count, root)    \
    ((void)0)
#define h_trace_fprint_input_trail(stream, frames, frame_count) ((void)0)
#define h_trace_fprint_input_context(stream, input, length, start, end) ((void)0)
#define h_trace_is_dump_enabled(trace) false
#define h_trace_should_print_summary(trace) false
#define h_backend_trace_begin(trace, backend, parser, input, len) ((void)0)
#define h_backend_trace_failures(trace, candidates, count)                                         \
    ((void)(trace), (void)(candidates), (void)(count))
#define h_backend_trace_end(trace, success) ((void)0)
#define h_cf_trace_candidates(root, input, input_pos, input_len, start, index, kind, fallback,     \
                              candidates)                                                          \
    ((void)(candidates), (size_t)0)
#define dump_rvm_prog(trace_state, prog) ((void)0)
#define dump_svm_prog(trace_state, prog, trace) ((void)0)
#define rvm_match_error(trace_state, prog, input, input_len, candidates, candidate_count, trace)   \
    ((void)0)
#define svm_action_error(ctx, prog, trace, input, input_len, msg) ((void)0)
#define svm_failure_error(ctx, prog, trace, input, input_len) ((void)0)
#define TRACE_ENABLED(trace) false
#define TRACE_GET_ERROR(trace, out) ((void)0)
#define TRACE_GET_DIAGNOSTIC(trace, out) ((void)0)
#define TRACE_BEGIN(trace, input, len) ((void)0)
#define TRACE_ENTER(p, s) ((void)0)
#define TRACE_EXIT(p, s, res, note) ((void)0)
#define TRACE_END(res, state) ((void)0)
#define TRACE_CHOICE_BEGIN(trace) H_TRACE_CHOICE_NONE
#define TRACE_CHOICE_ARM_BEGIN(trace, scope, alternative) ((void)0)
#define TRACE_CHOICE_ARM_END(trace, scope, success) ((void)0)
#define TRACE_CHOICE_END(trace, scope, success) ((void)0)
#define CF_TRACE_BEGIN(trace, backend, parser, input, len) ((void)0)
#define CF_TRACE_FAILURE(trace, start, end, kind, parser, provenance, expected, expected_eof)      \
    ((void)(trace), (void)(start), (void)(end), (void)(kind), (void)(parser), (void)(provenance),  \
     (void)(expected), (void)(expected_eof))
#define CF_TRACE_PARSER_ENTER(trace, parser, index, role) ((void)0)
#define CF_TRACE_PARSER_EXIT(trace, parser, start, end, token, success, role)                      \
    ((void)(trace), (void)(parser), (void)(start), (void)(end), (void)(token), (void)(success),    \
     (void)(role))
#define CF_TRACE_LR_SHIFT(trace, branch, from, to, index, parser, provenance, token) ((void)0)
#define CF_TRACE_LR_REDUCE(trace, branch, from, to, length, start, end, parser, provenance, token, \
                           success)                                                                \
    ((void)0)
#define CF_TRACE_LR_ERROR(trace, branch, state, index, parser, provenance) ((void)0)
#define CF_TRACE_GLR_FORK(trace, branch, state, index) (branch)
#define CF_TRACE_GLR_MERGE(trace, survivor, merged, state, index) ((void)0)
#define CF_TRACE_END(trace, success) ((void)0)

#endif /* HAMMER_TRACE_AST */

#endif /* HAMMER_BACKENDS_TRACE__H */
