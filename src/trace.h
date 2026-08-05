/* Copyright (c) 2026 Riverside Research */
#ifndef HAMMER_BACKENDS_TRACE__H
#define HAMMER_BACKENDS_TRACE__H

#include "internal.h" /* HParser, HParseState, HParseResult */

/* Compile-time master switch for the AST tracer.
 *
 * Default OFF. Enable at build time with -DHAMMER_TRACE_AST=1 (e.g. via the
 * scons --trace flag). The #ifndef guard lets that -D win without a
 * redefinition error under -Werror. Set the default below to 1 if you want
 * tracing compiled in unconditionally. */
#define HAMMER_TRACE_AST 1
#if HAMMER_TRACE_AST

void h_trace_set_enabled(bool enabled);
void h_trace_get_error(HParseError *out);
void h_trace_begin(const uint8_t *input, size_t input_len);
void h_trace_enter(const HParser *parser, HParseState *state);
void h_trace_exit(const HParser *parser, HParseState *state, HParseResult *res, const char *note);
void h_trace_end(HParseResult *res, HParseState *state);
void h_trace_file_context(const uint8_t *input, size_t length, size_t highlight_index);

#define TRACE_SET_ENABLED(b)  h_trace_set_enabled((b))
#define TRACE_GET_ERROR(out)  h_trace_get_error((out))
#define TRACE_BEGIN(input, len) h_trace_begin((input), (size_t)(len))
#define TRACE_ENTER(p, s)     h_trace_enter((p), (s))
#define TRACE_EXIT(p, s, res, note) h_trace_exit((p), (s), (res), (note))
#define TRACE_END(res, state) h_trace_end((res), (state))

#else /* tracing compiled out */

#define TRACE_SET_ENABLED(b)  ((void)0)
#define TRACE_GET_ERROR(out)  ((void)0)
#define TRACE_BEGIN(input, len) ((void)0)
#define TRACE_ENTER(p, s)     ((void)0)
#define TRACE_EXIT(p, s, res, note) ((void)0)
#define TRACE_END(res, state) ((void)0)

#endif /* HAMMER_TRACE_AST */

#endif /* HAMMER_BACKENDS_TRACE__H */
