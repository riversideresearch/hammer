#define _GNU_SOURCE
#include "regex.h"

#include "../internal.h"
#include "../parsers/parser_internal.h"
#include "trace.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#undef a_new
#define a_new(typ, count) a_new_(arena, typ, count)
#undef a_new0
#define a_new0(typ, count) a_new0_(arena, typ, count)

typedef struct HRVMThread_ {
    HRVMTrace *trace;
    uint16_t ip;
} HRVMThread;

typedef struct HRVMMatchFailure_ {
    bool present;
    size_t index;
    HTraceFailureCandidate candidates[H_TRACE_MAX_FAILURE_CANDIDATES];
    size_t candidate_count;
    HRVMTrace *trace;
} HRVMMatchFailure;

static bool rvm_failure_is_explicit(const HTraceFailureCandidate *candidate) {
    return candidate->kind == H_PARSE_ERROR_EXPLICIT_FAILURE;
}

static void record_rvm_match_failure(HRVMMatchFailure *failure, size_t index, uint8_t lo,
                                     uint8_t hi, bool expected_eof, const HParser *parser,
                                     const HDiagnosticContext *diagnostic_context,
                                     HRVMTrace *trace) {
    bool explicit_failure = h_is_nothing_parser(parser);
    if (!failure->present || index > failure->index) {
        memset(failure, 0, sizeof(*failure));
        failure->present = true;
        failure->index = index;
        failure->trace = trace;
    } else if (index < failure->index) {
        return;
    }

    bool has_non_explicit = false;
    for (size_t i = 0; i < failure->candidate_count; i++)
        has_non_explicit |= !rvm_failure_is_explicit(&failure->candidates[i]);
    if (explicit_failure && has_non_explicit)
        return;
    if (!explicit_failure && failure->candidate_count > 0) {
        size_t kept = 0;
        for (size_t i = 0; i < failure->candidate_count; i++)
            if (!rvm_failure_is_explicit(&failure->candidates[i]))
                failure->candidates[kept++] = failure->candidates[i];
        failure->candidate_count = kept;
        failure->trace = trace;
    }

    HTraceFailureCandidate *candidate = NULL;
    for (size_t i = 0; i < failure->candidate_count; i++) {
        HTraceFailureCandidate *item = &failure->candidates[i];
        if (item->parser == parser && item->provenance == diagnostic_context) {
            candidate = item;
            break;
        }
    }
    if (!candidate) {
        if (failure->candidate_count >= H_TRACE_MAX_FAILURE_CANDIDATES)
            return;
        candidate = &failure->candidates[failure->candidate_count++];
        memset(candidate, 0, sizeof(*candidate));
        candidate->start = index;
        candidate->end = index + 1;
        candidate->kind =
            explicit_failure ? H_PARSE_ERROR_EXPLICIT_FAILURE : H_PARSE_ERROR_PRIMITIVE_MISMATCH;
        candidate->parser = parser;
        candidate->provenance = diagnostic_context;
        HTraceCandidateChoice reverse_path[H_TRACE_MAX_CANDIDATE_CHOICE_DEPTH];
        size_t reverse_depth = 0;
        for (const HDiagnosticContext *item = diagnostic_context; item; item = item->next) {
            if (item->choice && reverse_depth < H_TRACE_MAX_CANDIDATE_CHOICE_DEPTH) {
                const HParser *origin = item->choice;
                for (const HDiagnosticContext *parent = item->next; parent; parent = parent->next) {
                    if (parent->choice)
                        break;
                    if (h_is_context_parser(parent->parser)) {
                        origin = parent->parser;
                        break;
                    }
                }
                HTraceCandidateChoice *path = &reverse_path[reverse_depth++];
                path->choice = item->choice;
                path->origin = origin;
                path->alternative = item->choice_alternative;
                path->id = item->choice_id;
            }
        }
        while (reverse_depth > 0)
            candidate->choices[candidate->choice_depth++] = reverse_path[--reverse_depth];
    }

    if (expected_eof) {
        candidate->expected_eof = true;
    } else {
        for (unsigned int c = lo; c <= hi; c++)
            candidate->expected_bytes[c] = true;
    }
}

HParseResult *run_trace(HAllocator *mm__, HRVMProg *orig_prog, HRVMTrace *trace,
                        const uint8_t *input, size_t len, HTraceState *trace_state);

HRVMTrace *invert_trace(HRVMTrace *trace) {
    HRVMTrace *last = NULL;
    if (!trace) {
        return NULL;
    }
    if (!trace->next) {
        return trace;
    }
    do {
        HRVMTrace *next = trace->next;
        trace->next = last;
        last = trace;
        trace = next;
    } while (trace);
    return last;
}

static void *h_rvm_run__m(HAllocator *mm__, HRVMProg *prog, const uint8_t *input, size_t len,
                          HTraceState *trace_state) {
    HArena *arena = h_new_arena(mm__, 0);
    HSArray *heads_a = h_sarray_new(mm__, prog->length), // Both of these contain HRVMTrace*'s
        *heads_b = h_sarray_new(mm__, prog->length);

    HRVMTrace *volatile ret_trace = NULL;
    HParseResult *ret = NULL;
    HRVMMatchFailure match_failure = {0};

    // out of memory handling
    if (!arena || !heads_a || !heads_b)
        goto end;
    jmp_buf except;
    h_arena_set_except(arena, &except);
    if (setjmp(except))
        goto end;

    HSArray *heads_n = heads_a, *heads_p = heads_b;
    uint8_t *insn_seen = a_new0(uint8_t, prog->length); // 0 -> not seen, 1->processed, 2->queued
    HRVMThread *ip_queue = a_new0(HRVMThread, prog->length);
    size_t ipq_top;

#define THREAD ip_queue[ipq_top - 1]
#define PUSH_SVM(op_, arg_)                                                                        \
    do {                                                                                           \
        HRVMTrace *nt = a_new0(HRVMTrace, 1);                                                      \
        nt->arg = (arg_);                                                                          \
        nt->opcode = (op_);                                                                        \
        nt->parser = prog->insn_parsers[THREAD.ip];                                                \
        nt->diagnostic_context = prog->insn_contexts[THREAD.ip];                                   \
        nt->next = THREAD.trace;                                                                   \
        nt->input_pos = off;                                                                       \
        THREAD.trace = nt;                                                                         \
    } while (0)

    ((HRVMTrace *)h_sarray_set(heads_n, 0, a_new0(HRVMTrace, 1)))->opcode =
        SVM_NOP; // Initial thread

    size_t off = 0;
    int live_threads = 1; // May be redundant
    for (off = 0; off <= len; off++) {
        uint8_t ch = ((off == len) ? 0 : input[off]);
        /* scope */ {
            HSArray *heads_t;
            heads_t = heads_n;
            heads_n = heads_p;
            heads_p = heads_t;
            h_sarray_clear(heads_n);
        }
        memset(insn_seen, 0, prog->length); // no insns seen yet
        if (!live_threads) {
            goto finalize;
        }
        live_threads = 0;
        HRVMTrace *tr_head;
        H_SARRAY_FOREACH_KV(tr_head, ip_s, heads_p) {
            ipq_top = 1;
            // TODO: Write this as a threaded VM
            assert(ip_s <= UINT16_MAX); // RVM uses 16-bit jump operands, reject programs exceeding
            THREAD.ip = (uint16_t)ip_s;
            THREAD.trace = tr_head;
            uint8_t hi, lo;
            uint16_t arg;
            while (ipq_top > 0) {
                if (insn_seen[THREAD.ip] == 1) {
                    ipq_top--; // Kill thread.
                    continue;
                }
                insn_seen[THREAD.ip] = 1;
                arg = prog->insns[THREAD.ip].arg;
                const HParser *insn_parser = prog->insn_parsers[THREAD.ip];
                const HDiagnosticContext *insn_context = prog->insn_contexts[THREAD.ip];
                switch (prog->insns[THREAD.ip].op) {
                case RVM_ACCEPT:
                    PUSH_SVM(SVM_ACCEPT, 0);
                    ret_trace = THREAD.trace;
                    ipq_top--;
                    goto next_insn;
                case RVM_MATCH:
                    hi = (uint8_t)((arg >> 8) & 0xff);
                    lo = (uint8_t)(arg & 0xff);
                    THREAD.ip++;
                    if (off == len || ch < lo || ch > hi) {
                        record_rvm_match_failure(&match_failure, off, lo, hi, false, insn_parser,
                                                 insn_context, THREAD.trace);
                        ipq_top--; // terminate thread
                    }
                    goto next_insn;
                case RVM_GOTO:
                    THREAD.ip = arg;
                    goto next_insn;
                case RVM_FORK:
                    THREAD.ip++;
                    if (!insn_seen[arg]) {
                        insn_seen[THREAD.ip] = 2;
                        HRVMTrace *tr = THREAD.trace;
                        ipq_top++;
                        THREAD.ip = arg;
                        THREAD.trace = tr;
                    }
                    goto next_insn;
                case RVM_PUSH:
                    PUSH_SVM(SVM_PUSH, 0);
                    THREAD.ip++;
                    goto next_insn;
                case RVM_ACTION:
                    PUSH_SVM(SVM_ACTION, arg);
                    THREAD.ip++;
                    goto next_insn;
                case RVM_CAPTURE:
                    PUSH_SVM(SVM_CAPTURE, 0);
                    THREAD.ip++;
                    goto next_insn;
                case RVM_EOF:
                    THREAD.ip++;
                    if (off != len) {
                        record_rvm_match_failure(&match_failure, off, 0, 0, true, insn_parser,
                                                 insn_context, THREAD.trace);
                        ipq_top--; // Terminate thread
                    }
                    goto next_insn;
                case RVM_STEP:
                    // save thread
                    live_threads++;
                    h_sarray_set(heads_n, ++THREAD.ip, THREAD.trace);
                    ipq_top--;
                    goto next_insn;
                }
            next_insn:;
            }
        }
    }
finalize:
    h_arena_set_except(arena, NULL); // there should be no more allocs from this
    if (ret_trace) {
        // Invert the direction of the trace linked list.
        ret_trace = invert_trace(ret_trace);
        ret = run_trace(mm__, prog, ret_trace, input, len, trace_state);
        // NB: ret is in its own arena
        // Dump execution trace on successful parse if tracing is enabled
        if (ret && h_trace_is_dump_enabled(trace_state)) {
            h_backend_trace_begin(trace_state, PB_REGULAR, prog->root_parser, input, len);
            dump_rvm_prog(trace_state, prog);
            dump_svm_prog(trace_state, prog, ret_trace);
            h_backend_trace_end(trace_state, true);
        }
    } else if (match_failure.present) {
        for (size_t i = 0; i < match_failure.candidate_count; i++) {
            HTraceFailureCandidate *candidate = &match_failure.candidates[i];
            candidate->end =
                match_failure.index < len ? match_failure.index + 1 : match_failure.index;
            if (candidate->kind != H_PARSE_ERROR_EXPLICIT_FAILURE)
                candidate->kind = match_failure.index < len ? H_PARSE_ERROR_PRIMITIVE_MISMATCH
                                                            : H_PARSE_ERROR_UNEXPECTED_EOF;
        }
        rvm_match_error(trace_state, prog, input, len, match_failure.candidates,
                        match_failure.candidate_count, match_failure.trace);
    }

end:
    if (arena)
        h_delete_arena(arena);
    if (heads_a)
        h_sarray_free(heads_a);
    if (heads_b)
        h_sarray_free(heads_b);
    return ret;
}
#undef PUSH_SVM
#undef THREAD

bool svm_stack_ensure_cap(HAllocator *mm__, HSVMContext *ctx, size_t addl) {
    if (ctx->stack_count + addl >= ctx->stack_capacity) {
        ctx->stack =
            mm__->realloc(mm__, ctx->stack, sizeof(*ctx->stack) * (ctx->stack_capacity *= 2));
        if (!ctx->stack) {
            return false;
        }
        return true;
    }
    return true;
}

/*
 * GCC produces the following diagnostic on this function:
 *
 *    error: argument 'trace' might be clobbered by 'longjmp' or 'vfork' [-Werror=clobbered]
 *
 * However, this is spurious; what is happening is that the trace
 * argument gets reused to store cur, and GCC doesn't know enough
 * about setjmp to know that the second return only returns nonzero
 * (and therefore the now-clobbered value of trace is invalid.)
 *
 * A side effect of disabling this warning is that we need to be
 * careful about undefined behaviour involving automatic
 * variables. Specifically, any automatic variable in this function
 * whose value gets modified after setjmp has an undefined value after
 * the second return; here, the only variables that could matter for
 * are arena and ctx (because they're referenced in "goto fail").
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wclobbered"
#endif
HParseResult *run_trace(HAllocator *mm__, HRVMProg *orig_prog, HRVMTrace *trace,
                        const uint8_t *input, size_t len, HTraceState *trace_state) {
    // orig_prog is only used for the action table
    HSVMContext *ctx = NULL;
    HArena *arena = h_new_arena(mm__, 0);
    if (arena == NULL) {
        return NULL;
    }
    ctx = h_new(HSVMContext, 1);
    if (!ctx)
        goto fail;
    memset(ctx, 0, sizeof(*ctx));
    ctx->trace_state = trace_state;
    ctx->stack_count = 0;
    ctx->stack_capacity = 16;
    ctx->stack = h_new(HParsedToken *, ctx->stack_capacity);

    // out of memory handling
    if (!arena || !ctx->stack)
        goto fail;
    jmp_buf except;
    h_arena_set_except(arena, &except);
    if (setjmp(except))
        goto fail;

    HParsedToken *tmp_res;
    HRVMTrace *cur;
    for (cur = trace; cur; cur = cur->next) {
        ctx->input_pos = cur->input_pos;
        ctx->parser = cur->parser;
        ctx->diagnostic_context = cur->diagnostic_context;
        switch (cur->opcode) {
        case SVM_PUSH:
            if (!svm_stack_ensure_cap(mm__, ctx, 1)) {
                svm_action_error(ctx, orig_prog, trace, input, len,
                                 "out of memory: cannot grow stack");
                goto fail;
            }
            tmp_res = a_new0(HParsedToken, 1);
            tmp_res->token_type = TT_MARK;
            tmp_res->index = cur->input_pos;
            tmp_res->bit_offset = 0;
            ctx->stack[ctx->stack_count++] = tmp_res;
            break;
        case SVM_NOP:
            break;
        case SVM_ACTION:
            // Action should modify stack appropriately
            memset(&ctx->failure, 0, sizeof(ctx->failure));
            if (!orig_prog->actions[cur->arg].action(arena, ctx,
                                                     orig_prog->actions[cur->arg].env)) {
                if (ctx->failure.kind != SVM_FAILURE_NONE)
                    svm_failure_error(ctx, orig_prog, trace, input, len);
                else
                    svm_action_error(ctx, orig_prog, trace, input, len,
                                     "SVM action failed unexpectedly");
                // action failed... abort somehow
                goto fail;
            }
            break;
        case SVM_CAPTURE:
            // Top of stack must be a mark
            // This replaces said mark in-place with a TT_BYTES.
            if (ctx->stack_count == 0 || ctx->stack[ctx->stack_count - 1]->token_type != TT_MARK) {
                svm_action_error(ctx, orig_prog, trace, input, len,
                                 "invalid capture: top of stack is not a mark");
                goto fail;
            }
            assert(ctx->stack[ctx->stack_count - 1]->token_type == TT_MARK);

            tmp_res = ctx->stack[ctx->stack_count - 1];
            tmp_res->token_type = TT_BYTES;
            // TODO: Will need to copy if bit_offset is nonzero
            if (tmp_res->bit_offset != 0) {
                svm_action_error(ctx, orig_prog, trace, input, len,
                                 "invalid capture: bit offset is nonzero");
                goto fail;
            }
            assert(tmp_res->bit_offset == 0);

            tmp_res->token_data.bytes.token = input + tmp_res->index;
            tmp_res->token_data.bytes.len = cur->input_pos - tmp_res->index;
            break;
        case SVM_ACCEPT:
            if (ctx->stack_count > 1) {
                svm_action_error(ctx, orig_prog, trace, input, len,
                                 "invalid accept: stack has more than one item");
                goto fail;
            }
            if (ctx->action_plan_frames) {
                svm_action_error(ctx, orig_prog, trace, input, len,
                                 "invalid accept: action plan frames not empty");
                goto fail;
            }
            assert(ctx->stack_count <= 1);
            if (!h_action_plan_execute(arena, ctx->action_plan)) {
                svm_action_error(ctx, orig_prog, trace, input, len, "action execution failed");
                goto fail;
            }
            HParseResult *res = a_new0(HParseResult, 1);
            if (ctx->stack_count == 1) {
                res->ast = ctx->stack[0];
            } else {
                res->ast = NULL;
            }
            res->bit_length = cur->input_pos * 8;
            res->arena = arena;
            h_arena_set_except(arena, NULL);
            h_free(ctx->stack);
            h_free(ctx);
            return res;
        }
    }
fail:
    if (arena)
        h_delete_arena(arena);
    if (ctx) {
        if (ctx->stack)
            h_free(ctx->stack);
        h_free(ctx);
    }
    return NULL;
}
// Reenable -Wclobber
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

uint16_t h_rvm_create_action(HRVMProg *prog, HSVMActionFunc action_func, void *env) {
    if (prog->action_count >= (size_t)UINT16_MAX) {
        longjmp(prog->except, 1);
    }
    for (uint16_t i = 0; i < prog->action_count; i++) {
        if (prog->actions[i].action == action_func && prog->actions[i].env == env) {
            return i;
        }
    }
    // Ensure that there's room in the action array...
    if (!(prog->action_count & (prog->action_count + 1))) {
        // needs to be scaled up.
        size_t array_size = (prog->action_count + 1) * 2; // action_count+1 is a
                                                          // power of two
        prog->actions = prog->allocator->realloc(prog->allocator, prog->actions,
                                                 array_size * sizeof(*prog->actions));
        if (!prog->actions) {
            longjmp(prog->except, 1);
        }
    }

    HSVMAction *action = &prog->actions[prog->action_count];
    action->action = action_func;
    action->env = env;
    uint16_t action_id = (uint16_t)prog->action_count;
    prog->action_count++;
    return action_id;
}

void *h_rvm_alloc(HRVMProg *prog, size_t size) {
    if (!prog->arena) {
        prog->arena = h_new_arena(prog->allocator, 0);
        if (!prog->arena)
            longjmp(prog->except, 1);
        h_arena_set_except(prog->arena, &prog->except);
    }
    return h_arena_malloc_noinit(prog->arena, size);
}

uint16_t h_rvm_insert_insn(HRVMProg *prog, HRVMOp op, uint16_t arg) {
    if (prog->length >= (size_t)UINT16_MAX) {
        longjmp(prog->except, 1);
    }
    // Ensure that there's room in the insn array...
    if (!(prog->length & (prog->length + 1))) {
        // needs to be scaled up.
        size_t array_size = (prog->length + 1) * 2; // action_count+1 is a
                                                    // power of two
        prog->insns = prog->allocator->realloc(prog->allocator, prog->insns,
                                               array_size * sizeof(*prog->insns));
        if (!prog->insns) {
            longjmp(prog->except, 1);
        }
        prog->insn_parsers = prog->allocator->realloc(prog->allocator, prog->insn_parsers,
                                                      array_size * sizeof(*prog->insn_parsers));
        if (!prog->insn_parsers) {
            longjmp(prog->except, 1);
        }
        prog->insn_contexts = prog->allocator->realloc(prog->allocator, prog->insn_contexts,
                                                       array_size * sizeof(*prog->insn_contexts));
        if (!prog->insn_contexts) {
            longjmp(prog->except, 1);
        }
    }

    prog->insns[prog->length].op = op;
    prog->insns[prog->length].arg = arg;
    prog->insn_parsers[prog->length] = prog->current_parser;
    prog->insn_contexts[prog->length] = prog->current_context;
    uint16_t ip = (uint16_t)prog->length;
    prog->length++;
    return ip;
}

uint16_t h_rvm_get_ip(HRVMProg *prog) {
    if (prog->length > UINT16_MAX) {
        longjmp(prog->except, 1);
    }
    return (uint16_t)prog->length;
}

void h_rvm_patch_arg(HRVMProg *prog, uint16_t ip, uint16_t new_val) {
    if (prog->length <= ip)
        longjmp(prog->except, 1);
    assert(prog->length > ip);
    prog->insns[ip].arg = new_val;
}

size_t h_svm_count_to_mark(HSVMContext *ctx) {
    size_t ctm;
    for (ctm = 0; ctm < ctx->stack_count; ctm++) {
        if (ctx->stack[ctx->stack_count - 1 - ctm]->token_type == TT_MARK) {
            return ctm;
        }
    }
    return ctx->stack_count;
}

// TODO: Implement the primitive actions
bool h_svm_action_make_sequence(HArena *arena, HSVMContext *ctx, void *env) {
    size_t n_items = h_svm_count_to_mark(ctx);
    if (n_items >= ctx->stack_count)
        return false;
    assert(n_items < ctx->stack_count);
    HParsedToken *res = ctx->stack[ctx->stack_count - 1 - n_items];
    if (res->token_type != TT_MARK)
        return false;
    assert(res->token_type == TT_MARK);
    res->token_type = TT_SEQUENCE;

    HCountedArray *ret_carray = h_carray_new_sized(arena, n_items);
    res->token_data.seq = ret_carray;
    // res index and bit offset are the same as the mark.
    for (size_t i = 0; i < n_items; i++) {
        ret_carray->elements[i] = ctx->stack[ctx->stack_count - n_items + i];
    }
    ret_carray->used = n_items;
    ctx->stack_count -= n_items;
    return true;
}

bool h_svm_action_clear_to_mark(HArena *arena, HSVMContext *ctx, void *env) {
    while (ctx->stack_count > 0) {
        if (ctx->stack[--ctx->stack_count]->token_type == TT_MARK) {
            return true;
        }
    }
    return false; // no mark found.
}

// Glue regex backend to rest of system

bool h_compile_regex(HRVMProg *prog, const HParser *parser) {
    if (!parser->vtable->compile_to_rvm)
        return false;

    const HParser *parent_parser = prog->current_parser;
    const HDiagnosticContext *parent_context = prog->current_context;
    prog->current_parser = parser;
    HDiagnosticContext *path = h_rvm_alloc(prog, sizeof(*path));
    path->parser = parser;
    path->choice = NULL;
    path->choice_alternative = 0;
    path->choice_id = 0;
    path->next = parent_context;

    prog->current_context = path;
    bool result = parser->vtable->compile_to_rvm(prog, parser->env);
    prog->current_context = parent_context;
    prog->current_parser = parent_parser;
    return result;
}

static void h_rvm_prog_free(HRVMProg *prog) {
    HAllocator *mm__ = prog->allocator;
    h_free(prog->insns);
    h_free(prog->insn_parsers);
    h_free(prog->insn_contexts);
    h_free(prog->actions);
    if (prog->arena)
        h_delete_arena(prog->arena);
    h_free(prog);
}

static void h_regex_free(HParser *parser) {
    HRVMProg *prog = (HRVMProg *)parser->backend_data;
    h_rvm_prog_free(prog);
    parser->backend_data = NULL;
    parser->backend_vtable = h_get_default_backend_vtable();
    parser->backend = h_get_default_backend();
}

static int h_regex_compile(HAllocator *mm__, HParser *parser, const void *params) {
    if (!parser->vtable->isValidRegular(parser->env)) {
        return -1;
    }
    HRVMProg *prog = h_new(HRVMProg, 1);
    prog->length = prog->action_count = 0;
    prog->insns = NULL;
    prog->insn_parsers = NULL;
    prog->insn_contexts = NULL;
    prog->actions = NULL;
    prog->current_parser = NULL;
    prog->current_context = NULL;
    prog->next_choice_id = 0;
    prog->root_parser = parser;
    prog->allocator = mm__;
    prog->arena = NULL;
    if (setjmp(prog->except)) {
        h_rvm_prog_free(prog);
        return 3;
    }
    if (!h_compile_regex(prog, parser)) {
        // this shouldn't normally fail when isValidRegular() returned true
        h_rvm_prog_free(prog);
        return 2;
    }
    h_rvm_insert_insn(prog, RVM_ACCEPT, 0);
    memset(prog->except, 0, sizeof(prog->except));
    parser->backend_data = prog;
    return 0;
}

static HParseResult *h_regex_parse(HAllocator *mm__, const HParser *parser,
                                   HInputStream *input_stream) {
    return h_rvm_run__m(mm__, (HRVMProg *)parser->backend_data, input_stream->input,
                        input_stream->length, input_stream->trace);
}

HParserBackendVTable h__regex_backend_vtable = {
    .compile = h_regex_compile,
    .parse = h_regex_parse,
    .free = h_regex_free,
    /* Name/param resolution functions */
    .backend_short_name = "regex",
    .backend_description = "Regular expression matcher (broken)",
    .get_description_with_params = h_get_description_with_no_params,
    .get_short_name_with_params = h_get_short_name_with_no_params};
