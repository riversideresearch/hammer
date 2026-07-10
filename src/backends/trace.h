/* Copyright (c) 2026 Riverside Research */
#ifndef HAMMER_BACKENDS_TRACE__H
#define HAMMER_BACKENDS_TRACE__H

#include "../internal.h" /* HParser, HParseState, HParseResult */

/* Compile-time master switch for the AST tracer.
 *
 * Default OFF. Enable at build time with -DHAMMER_TRACE_AST=1 (e.g. via the
 * scons --trace flag). The #ifndef guard lets that -D win without a
 * redefinition error under -Werror. Set the default below to 1 if you want
 * tracing compiled in unconditionally. */
#define HAMMER_TRACE_AST 1
#if HAMMER_TRACE_AST

void h_trace_begin(size_t input_len);
void h_trace_enter(const HParser *parser, HParseState *state);
void h_trace_exit(HParseResult *res, const char *note);
void h_trace_end(HParseResult *res, HParseState *state);

#define TRACE_BEGIN(len)      h_trace_begin((size_t)(len))
#define TRACE_ENTER(p, s)     h_trace_enter((p), (s))
#define TRACE_EXIT(res, note) h_trace_exit((res), (note))
#define TRACE_END(res, state) h_trace_end((res), (state))

#else /* tracing compiled out */

#define TRACE_BEGIN(len)      ((void)0)
#define TRACE_ENTER(p, s)     ((void)0)
#define TRACE_EXIT(res, note) ((void)0)
#define TRACE_END(res, state) ((void)0)

#endif /* HAMMER_TRACE_AST */

#endif /* HAMMER_BACKENDS_TRACE__H */