/* Copyright (c) 2026 Riverside Research */
#ifndef HAMMER_BACKENDS_TRACE__H
#define HAMMER_BACKENDS_TRACE__H

#include "backends/regex.h"
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

struct HParseDiagnostic_ {
    HParseError error;
    bool expected_bytes[256];
    bool expected_eof;
    HTraceNumericRange numeric_range;
};

/* Compile-time master switch for the AST tracer.
 *
 * Default OFF. Enable at build time with -DHAMMER_TRACE_AST=1 (e.g. via the
 * scons --trace flag). The #ifndef guard lets that -D win without a
 * redefinition error under -Werror. Set the default below to 1 if you want
 * tracing compiled in unconditionally. */
#define HAMMER_TRACE_AST 1
#if HAMMER_TRACE_AST

// packrat trace functions
void h_trace_set_enabled(bool enabled, bool dumpExecutionTrace);
bool h_trace_is_enabled(void);
bool h_trace_is_dump_enabled(void);
void h_trace_get_error(HParseError *out);
void h_trace_get_diagnostic(HParseDiagnostic **out);
void h_trace_begin(const uint8_t *input, size_t input_len);
void h_trace_enter(const HParser *parser, HParseState *state);
void h_trace_exit(const HParser *parser, HParseState *state, HParseResult *res, const char *note);
void h_trace_end(HParseResult *res, HParseState *state);
void h_trace_file_context(const uint8_t *input, size_t length, size_t start_index, size_t end_index);
void h_trace_note_int_range(const HParsedToken *token, int64_t lower, int64_t upper);
void h_trace_note_float_range(const HParsedToken *token, double lower, double upper);

// context-free backend trace functions
void h_cf_trace_begin(HParserBackend backend, const HParser *parser, const uint8_t *input,
                      size_t input_len);
void h_cf_trace_failure(size_t start, size_t end, HParseErrorKind kind, const HParser *parser,
                        const bool expected[256], bool expected_eof);
void h_cf_trace_parser_enter(const HParser *parser, size_t index, const char *role);
void h_cf_trace_parser_exit(const HParser *parser, size_t start, size_t end,
                            const HParsedToken *token, bool success, const char *role);
void h_cf_trace_lr_shift(size_t branch, size_t from_state, size_t to_state, size_t index,
                         const HParser *parser, const HParsedToken *token);
void h_cf_trace_lr_reduce(size_t branch, size_t from_state, size_t to_state, size_t length,
                          size_t start, size_t end, const HParser *parser,
                          const HParsedToken *token, bool success);
void h_cf_trace_lr_error(size_t branch, size_t state, size_t index, const HParser *parser);
size_t h_cf_trace_glr_fork(size_t branch, size_t state, size_t index);
void h_cf_trace_glr_merge(size_t survivor, size_t merged, size_t state, size_t index);
void h_cf_trace_end(bool success);

/* Shared backend diagnostic entry points (also used by the regex VM). */
void h_backend_trace_begin(HParserBackend backend, const HParser *parser, const uint8_t *input,
                           size_t input_len);
void h_backend_trace_failure(size_t start, size_t end, HParseErrorKind kind, const HParser *parser,
                             const bool expected[256], bool expected_eof);
void h_backend_trace_end(bool success);

// regex trace functions
char *getsym(HSVMActionFunc addr);
char *h_trace_parser_name(const HParser *parser);
void dump_rvm_prog(HRVMProg *prog);
void dump_svm_prog(HRVMProg *prog, HRVMTrace *trace);
void rvm_match_error(HRVMProg *prog, const uint8_t *input, size_t input_len, size_t off,
                     const bool expected[256], bool expected_eof, const HParser *parser);
void svm_action_error(HSVMContext *ctx, HRVMProg *orig_prog, HRVMTrace *trace, const uint8_t *input,
                      size_t input_len, const char *msg);
void svm_failure_error(HSVMContext *ctx, HRVMProg *orig_prog, HRVMTrace *trace,
                       const uint8_t *input, size_t input_len);

#define TRACE_SET_ENABLED(a, b) h_trace_set_enabled(a, b)
#define TRACE_ENABLED() h_trace_is_enabled()
#define TRACE_GET_ERROR(out) h_trace_get_error((out))
#define TRACE_GET_DIAGNOSTIC(out) h_trace_get_diagnostic((out))
#define TRACE_BEGIN(input, len) h_trace_begin((input), (size_t)(len))
#define TRACE_ENTER(p, s) h_trace_enter((p), (s))
#define TRACE_EXIT(p, s, res, note) h_trace_exit((p), (s), (res), (note))
#define TRACE_END(res, state) h_trace_end((res), (state))
#define CF_TRACE_BEGIN(backend, parser, input, len)                                                \
    h_cf_trace_begin((backend), (parser), (input), (size_t)(len))
#define CF_TRACE_FAILURE(start, end, kind, parser, expected, expected_eof)                         \
    h_cf_trace_failure((start), (end), (kind), (parser), (expected), (expected_eof))
#define CF_TRACE_PARSER_ENTER(parser, index, role)                                                 \
    h_cf_trace_parser_enter((parser), (index), (role))
#define CF_TRACE_PARSER_EXIT(parser, start, end, token, success, role)                             \
    h_cf_trace_parser_exit((parser), (start), (end), (token), (success), (role))
#define CF_TRACE_LR_SHIFT(branch, from, to, index, parser, token)                                  \
    h_cf_trace_lr_shift((branch), (from), (to), (index), (parser), (token))
#define CF_TRACE_LR_REDUCE(branch, from, to, length, start, end, parser, token, success)           \
    h_cf_trace_lr_reduce((branch), (from), (to), (length), (start), (end), (parser), (token),      \
                         (success))
#define CF_TRACE_LR_ERROR(branch, state, index, parser)                                            \
    h_cf_trace_lr_error((branch), (state), (index), (parser))
#define CF_TRACE_GLR_FORK(branch, state, index) h_cf_trace_glr_fork((branch), (state), (index))
#define CF_TRACE_GLR_MERGE(survivor, merged, state, index)                                         \
    h_cf_trace_glr_merge((survivor), (merged), (state), (index))
#define CF_TRACE_END(success) h_cf_trace_end((success))

#else /* tracing compiled out */

#define TRACE_SET_ENABLED(enabled, dump) ((void)0)
#define TRACE_ENABLED() false
#define TRACE_GET_ERROR(out) ((void)0)
#define TRACE_GET_DIAGNOSTIC(out) ((void)0)
#define TRACE_BEGIN(input, len) ((void)0)
#define TRACE_ENTER(p, s) ((void)0)
#define TRACE_EXIT(p, s, res, note) ((void)0)
#define TRACE_END(res, state) ((void)0)
#define CF_TRACE_BEGIN(backend, parser, input, len) ((void)0)
#define CF_TRACE_FAILURE(start, end, kind, parser, expected, expected_eof) ((void)0)
#define CF_TRACE_PARSER_ENTER(parser, index, role) ((void)0)
#define CF_TRACE_PARSER_EXIT(parser, start, end, token, success, role) ((void)0)
#define CF_TRACE_LR_SHIFT(branch, from, to, index, parser, token) ((void)0)
#define CF_TRACE_LR_REDUCE(branch, from, to, length, start, end, parser, token, success) ((void)0)
#define CF_TRACE_LR_ERROR(branch, state, index, parser) ((void)0)
#define CF_TRACE_GLR_FORK(branch, state, index) (branch)
#define CF_TRACE_GLR_MERGE(survivor, merged, state, index) ((void)0)
#define CF_TRACE_END(success) ((void)0)

#endif /* HAMMER_TRACE_AST */

#endif /* HAMMER_BACKENDS_TRACE__H */
