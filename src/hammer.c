/* Copyright (c) 2026 Riverside Research */
/* Parser combinators for binary formats.
 * Copyright (C) 2012  Meredith L. Patterson, Dan "TQ" Hirsch
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "hammer.h"

#include "allocator.h"
#include "glue.h"
#include "internal.h"
#include "parsers/parser_internal.h"
#include "trace.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>

static HParserBackendVTable *backends[PB_MAX + 1] = {
    &h__missing_backend_vtable, /* For PB_INVALID */
    &h__packrat_backend_vtable, /* For PB_PACKRAT */
    &h__regex_backend_vtable,   /* For PB_REGULAR */
    &h__llk_backend_vtable,     /* For PB_LL */
    &h__lalr_backend_vtable,    /* For PB_LALR */
    &h__glr_backend_vtable      /* For PB_GLR */
};

/* Helper function, since these lines appear in every parser */

typedef struct {
    const HParser *p1;
    const HParser *p2;
} HTwoParsers;

/* Backend-related inquiries */

int h_is_backend_available(HParserBackend backend) {
    if (backend >= PB_MIN && backend <= PB_MAX) {
        return (backends[backend] != &h__missing_backend_vtable) ? 1 : 0;
    } else
        return 0;
}

HParserBackend h_get_default_backend(void) {
    /* Call the inline version in internal.h */
    return h_get_default_backend__int();
}

HParserBackendVTable *h_get_default_backend_vtable(void) {
    return h_get_default_backend_vtable__int();
}

/*
 * Copy an HParserBackendWithParams, using the backend-supplied copy
 * method.
 */

HParserBackendWithParams *h_copy_backend_with_params(HParserBackendWithParams *be_with_params) {
    return h_copy_backend_with_params__m(&system_allocator, be_with_params);
}

HParserBackendWithParams *h_copy_backend_with_params__m(HAllocator *mm__,
                                                        HParserBackendWithParams *be_with_params) {
    HParserBackendWithParams *r = NULL;
    int s;

    if (be_with_params && be_with_params->backend >= PB_MIN && be_with_params->backend <= PB_MAX) {
        if (mm__ == NULL) {
            /* use the allocator from the input */
            mm__ = be_with_params->mm__;
        }

        /* got an allocator? */
        if (mm__) {
            r = h_new(HParserBackendWithParams, 1);
            if (r) {
                r->mm__ = mm__;
                r->requested_name = be_with_params->requested_name;
                r->backend = be_with_params->backend;
                r->backend_vtable = be_with_params->backend_vtable;
                if (be_with_params->backend_vtable != NULL &&
                    be_with_params->backend_vtable->copy_params) {
                    s = be_with_params->backend_vtable->copy_params(mm__, &(r->params),
                                                                    be_with_params->params);
                    if (s != 0) {
                        /* copy_params() failed */
                        h_free(r);
                        r = NULL;
                    }
                } else {
                    /* else just ignore it and set it to NULL */
                    r->params = NULL;
                }
            }
            /* else fail out */
        }
        /* else give up and return NULL */
    }
    /* else return NULL, nothing to copy */

    return r;
}

/*
 * copy_params for backends where the parameter is actually a number cast to
 * void *, such as LLk, LALR and GLR.  Use NULL for free_params in this
 * case.
 */

int h_copy_numeric_param(HAllocator *mm__, void **out, void *in) {
    int rv = 0;

    if (out)
        *out = in;
    else
        rv = -1; /* error return, nowhere to write result */

    return rv;
}

/* Free this HParserBackendWithParams, and free the params too */

void h_free_backend_with_params(HParserBackendWithParams *be_with_params) {
    HAllocator *mm__ = &system_allocator;

    if (be_with_params) {
        if (be_with_params->mm__)
            mm__ = be_with_params->mm__;

        if (h_is_backend_available(be_with_params->backend)) {
            if (be_with_params->backend_vtable != NULL &&
                be_with_params->backend_vtable->free_params) {
                be_with_params->backend_vtable->free_params(be_with_params->mm__,
                                                            be_with_params->params);
            }
        }

        if (be_with_params->requested_name != NULL) {
            h_free(be_with_params->requested_name);
        }

        h_free(be_with_params);
    }
}

/*
 * Internal function to query name or descriptive text; the logic is
 * the same in both cases.
 */

static const char *h_get_string_for_backend(HParserBackend be, int description) {
    /*
     * For names, failure to resolve should return null - those are
     * cases we can't look up by name either.  Human readable descriptions
     * should have human-readable error strings
     */
    const char *text = description ? "this is a bug!" : NULL;
    const char **ptr;

    if (be >= PB_MIN && be <= PB_MAX) {
        /* the invalid backend is special */
        if (be == PB_INVALID) {
            text = description ? "invalid backend" : NULL;
        } else {
            if (backends[be] != backends[PB_INVALID]) {
                /* Point to the name or description */
                ptr = description ? &(backends[be]->backend_description)
                                  : &(backends[be]->backend_short_name);
                /* See if the backend knows how to describe itself */
                if (*ptr != NULL) {
                    text = *ptr;
                } else {
                    /* nope */
                    text = description ? "no description available" : NULL;
                }
            } else {
                /*
                 * This is the case for backends which weren't compiled into this
                 * library.
                 */
                text = description ? "unsupported backend" : NULL;
            }
        }
    } else {
        text = description ? "bad backend number" : NULL;
    }

    return text;
}

/* Return an identifying name for this backend */

const char *h_get_name_for_backend(HParserBackend be) {
    /*
     * call h_get_string_for_backend(), 0 indicates name
     * rather than description requested
     */
    return h_get_string_for_backend(be, 0);
}

/*
 * Return some human-readable descriptive text for this backend
 */

const char *h_get_descriptive_text_for_backend(HParserBackend be) {
    /*
     * call h_get_string_for_backend(), 1 indicates a
     * description is requested.
     */
    return h_get_string_for_backend(be, 1);
}

/*
 * Internal function to query name or descriptive text for a backend with
 * params; the logic is the same in both cases.
 */

static char *h_get_string_for_backend_with_params__m(HAllocator *mm__,
                                                     HParserBackendWithParams *be_with_params,
                                                     int description) {
    char *text = NULL;
    const char *generic_text = NULL;
    HParserBackend be = PB_INVALID;
    char *(**text_getter_func)(HAllocator *mm__, HParserBackend be, void *params) = NULL;

    if (mm__ == NULL || be_with_params == NULL)
        goto done;

    /* check if we can compute custom text with params for this backend */
    be = be_with_params->backend;
    if (be >= PB_MIN && be <= PB_MAX && be != PB_INVALID && backends[be] != backends[PB_INVALID]) {
        text_getter_func = description ? &(backends[be]->get_description_with_params)
                                       : &(backends[be]->get_short_name_with_params);

        if (*text_getter_func != NULL) {
            text = (*text_getter_func)(mm__, be, be_with_params->params);
        }
    }

    /* got it? */
    if (!text) {
        /* fall back to the generic descriptive text */
        generic_text = h_get_string_for_backend(be, description);
        if (generic_text) {
            size_t size = strlen(generic_text) + 1;
            text = h_new(char, size);
            strncpy(text, generic_text, size);
        }
    }

done:
    return text;
}
/* Allocate and return an identifying name for this backend
 * with parameters.  The caller is responsible for freeing the result.
 */
char *h_get_name_for_backend_with_params__m(HAllocator *mm__,
                                            HParserBackendWithParams *be_with_params) {
    return h_get_string_for_backend_with_params__m(mm__, be_with_params, 0);
}

char *h_get_name_for_backend_with_params(HParserBackendWithParams *be_with_params) {
    return h_get_name_for_backend_with_params__m(&system_allocator, be_with_params);
}
/*
 * Allocate and return some human-readable descriptive text for this backend
 * with parameters.  The caller is responsible for freeing the result.
 */

char *h_get_descriptive_text_for_backend_with_params__m(HAllocator *mm__,
                                                        HParserBackendWithParams *be_with_params) {
    return h_get_string_for_backend_with_params__m(mm__, be_with_params, 1);
}

char *h_get_descriptive_text_for_backend_with_params(HParserBackendWithParams *be_with_params) {
    return h_get_descriptive_text_for_backend_with_params__m(&system_allocator, be_with_params);
}

/* Helpers for the above for backends with no params */

static char *h_get_backend_text_with_no_params(HAllocator *mm__, HParserBackend be,
                                               int description) {
    char *text = NULL;
    const char *src = NULL;
    int size;

    if (!(mm__ != NULL && be != PB_INVALID && be >= PB_MIN && be <= PB_MAX))
        goto done;

    src = description ? backends[be]->backend_description : backends[be]->backend_short_name;

    if (src) {
        size = strlen(src) + 1;
        text = h_new(char, size);
        if (text)
            strncpy(text, src, size);
    }

done:
    return text;
}

/* Query a backend by short name; return PB_INVALID if no match */

HParserBackend h_query_backend_by_name(const char *name) {
    HParserBackend result = PB_INVALID, i;

    if (name != NULL) {
        /* Okay, iterate over the backends PB_MIN <= i <= PB_MAX and check */
        i = PB_MIN;
        do {
            if (i != PB_INVALID) {
                if (backends[i]->backend_short_name != NULL) {
                    if (strcmp(name, backends[i]->backend_short_name) == 0) {
                        result = i;
                    }
                }
            }
            ++i;
        } while (i <= PB_MAX && result == PB_INVALID);
    }

    return result;
}

char *h_get_description_with_no_params(HAllocator *mm__, HParserBackend be, void *params) {
    return h_get_backend_text_with_no_params(mm__, be, 1);
}

char *h_get_short_name_with_no_params(HAllocator *mm__, HParserBackend be, void *params) {
    return h_get_backend_text_with_no_params(mm__, be, 0);
}

/*TODO: possibly move to its own file?
 * If so, move include of glue.h as well
 * this parser is the only code using glue.h in here
 */

HParsedToken *act_backend_with_params(const HParseResult *p, void *user_data) {
    backend_with_params_t *be_with_params = H_ALLOC(backend_with_params_t);

    backend_name_t *name = H_FIELD(backend_name_t, 0);
    be_with_params->name = *name;

    HParsedToken *param_list = p->ast->token_data.seq->elements[1];
    if (param_list->token_type == TT_SEQUENCE && param_list->token_data.seq->used == 3) {
        backend_params_t *params = H_INDEX(backend_params_t, param_list, 1);
        be_with_params->params = *params;
    }

    return H_MAKE(backend_with_params_t, (void *)be_with_params);
}

HParsedToken *act_backend_name(const HParseResult *p, void *user_data) {
    backend_name_t *r = H_ALLOC(backend_name_t);

    HParsedToken *flat = h_act_flatten(p, user_data);

    r->len = h_seq_len(flat);
    r->name = h_arena_malloc(p->arena, r->len + 1);
    for (size_t i = 0; i < r->len; ++i) {

        r->name[i] = flat->token_data.seq->elements[i]->token_data.uint;
    }
    r->name[r->len] = 0;

    return H_MAKE(backend_name_t, r);
}

HParsedToken *act_param(const HParseResult *p, void *user_data) {
    backend_param_t *r = H_ALLOC(backend_param_t);

    r->len = h_seq_len(p->ast);
    r->param = h_arena_malloc(p->arena, r->len + 1);
    for (size_t i = 0; i < r->len; ++i)
        r->param[i] = H_FIELD_UINT(i);
    r->param[r->len] = 0;

    return H_MAKE(backend_param_t, r);
}

HParsedToken *act_param_name(const HParseResult *p, void *user_data) {
    backend_param_name_t *r = H_ALLOC(backend_param_name_t);

    HParsedToken *flat = h_act_flatten(p, user_data);

    r->len = h_seq_len(flat);
    r->param_name = h_arena_malloc(p->arena, r->len + 1);
    for (size_t i = 0; i < r->len; ++i)
        r->param_name[i] = flat->token_data.seq->elements[i]->token_data.uint;
    r->param_name[r->len] = 0;

    return H_MAKE(backend_param_name_t, r);
}

HParsedToken *act_param_with_name(const HParseResult *p, void *user_data) {
    backend_param_with_name_t *backend_param_with_name = H_ALLOC(backend_param_with_name_t);

    HParsedToken *tok = p->ast->token_data.seq->elements[0];

    if (tok->token_type == TT_SEQUENCE) {
        backend_param_name_t *param_name = H_INDEX(backend_param_name_t, tok, 0);
        backend_param_with_name->param_name = *param_name;
    }

    backend_param_t *param = H_FIELD(backend_param_t, 1);
    backend_param_with_name->param = *param;

    return H_MAKE(backend_param_with_name_t, backend_param_with_name);
}

HParsedToken *act_backend_params(const HParseResult *p, void *user_data) {
    HParsedToken *res = H_MAKE(backend_params_t, (void *)p->ast);

    backend_params_t *bp = H_ALLOC(backend_params_t);

    HParsedToken **fields = h_seq_elements(p->ast);

    bp->len = h_seq_len(p->ast);
    bp->params = h_arena_malloc(p->arena, sizeof(backend_param_with_name_t) * bp->len);
    for (size_t i = 0; i < bp->len; i++) {
        bp->params[i] = *H_INDEX(backend_param_with_name_t, p->ast, i);
    }

    return H_MAKE(backend_params_t, bp);

    return res;
}

static HParser *build_hparser_rule(void) {

    H_RULE(alpha, h_choice(h_ch_range('A', 'Z'), h_ch_range('a', 'z'), NULL));
    H_RULE(digit, h_ch_range(0x30, 0x39));
    H_RULE(sp, h_ch(' '));
    H_RULE(eq, h_ch('='));
    H_RULE(comma, h_ch(','));
    H_RULE(sep, h_sequence(comma, h_optional(sp), NULL));
    H_RULE(left_paren, h_ch('('));
    H_RULE(right_paren, h_ch(')'));

    H_RULE(alphas, h_many1(alpha));
    H_RULE(digits, h_many1(digit));

    H_ARULE(backend_name, h_sequence(alphas, h_many(h_choice(digit, alpha, NULL)), NULL));
    H_ARULE(param, h_choice(digits, alphas, NULL));
    H_ARULE(param_name, h_sequence(alphas, h_optional(digits), NULL));
    H_ARULE(param_with_name, h_sequence(h_optional(h_sequence(param_name, eq, NULL)), param, NULL));
    H_ARULE(backend_params, h_sepBy(param_with_name, sep));

    H_RULE(param_list, h_sequence(left_paren, backend_params, right_paren, NULL));

    H_ARULE(backend_with_params, h_sequence(backend_name, h_optional(param_list), NULL));

    return backend_with_params;
}

static HParser *build_hparser(void) {

    HParser *p = NULL;
    int r;
    p = build_hparser_rule();
    r = h_compile(p, PB_PACKRAT, NULL);

    if (r == 0) {
        return p;
    } else {
        printf("Compiling parser failed\n");
        return NULL;
    }
}

HParserBackendWithParams *h_get_backend_with_params_by_name(const char *name_with_params) {
    HAllocator *mm__ = &system_allocator;
    HParserBackendWithParams *result = NULL;

    HParser *parser = NULL;
    HParseResult *r = NULL;

    if (name_with_params != NULL) {
        result = h_new(HParserBackendWithParams, 1);
        if (result) {
            result->mm__ = mm__;
            result->requested_name = NULL;

            parser = build_hparser();
            if (!parser) {
                return NULL;
            }

            r = h_parse(parser, (const uint8_t *)name_with_params, strlen(name_with_params));

            if (r) {

                backend_with_params_t *be_w_params = r->ast->token_data.user;

                backend_name_t *name = &be_w_params->name;

                backend_params_t *params = &be_w_params->params;

                result->requested_name = h_new(char, name->len + 1);
                memset(result->requested_name, '\0', name->len + 1);
                strncpy(result->requested_name, (char *)name->name, name->len);

                result->backend = h_query_backend_by_name(result->requested_name);

                if (result->backend >= PB_MIN && result->backend <= PB_MAX)
                    result->backend_vtable = backends[result->backend];
                else
                    result->backend_vtable = backends[PB_INVALID];

                /* use the backend supplied method to extract any params from the input */
                result->params = NULL;
                bool invalid_params = false;
                if (params->len > 0) {
                    if (result->backend_vtable->extract_params) {
                        int extract_status =
                            result->backend_vtable->extract_params(result, be_w_params);
                        if (extract_status < 1) {
                            invalid_params = true;
                        }
                    }
                }

                if (invalid_params) {
                    result->backend = PB_INVALID;
                    result->backend_vtable = backends[PB_INVALID];
                    result->params = NULL;
                }

                // free the parse result
                h_parse_result_free(r);
                r = NULL;

                // free the memory used by the backend for parser data
                //(TODO: code to completely free the memory used for this parser)
                parser->backend_vtable->free(parser);
            }
        }
    }

    return result;
}

HParseResult *h_parse(const HParser *parser, const uint8_t *input, size_t length) {
    return h_parse__m(&system_allocator, parser, input, length);
}
HParseResult *h_parse__m(HAllocator *mm__, const HParser *parser, const uint8_t *input,
                         size_t length) {
    // Set up a parse state...
    HInputStream input_stream = {.pos = 0,
                                 .index = 0,
                                 .bit_offset = 0,
                                 .overrun = 0,
                                 .endianness = DEFAULT_ENDIANNESS,
                                 .length = length,
                                 .input = input,
                                 .last_chunk = true};

    return parser->backend_vtable->parse(mm__, parser, &input_stream);
}

// Twin of h_parse() that turns on backend tracing for the duration
// of this one parse, then switches it back off. Identical parsing behavior and
// return value; the only difference is the trace/diagnostics emitted to
// stderr/stdout. When the library is built without tracing (HAMMER_TRACE_AST
// off) TRACE_SET_ENABLED is a no-op and this behaves exactly like h_parse().
//
// If `error` is non-NULL it also receives the furthest-failure record in
// structured form (see HParseError), so callers can react to failures without
// scraping the textual trace. It is zeroed up front so the compiled-out case
// (and a NULL trace) leaves well-defined, empty contents.
HParseResult *h_parse_debug(const HParser *parser, const uint8_t *input, size_t length,
                            HParseError *error, bool dumpExecutionTrace, bool dumpInputContext) {
    return h_parse_debug__m(&system_allocator, parser, input, length, error, dumpExecutionTrace,
                            dumpInputContext);
}
HParseResult *h_parse_debug__m(HAllocator *mm__, const HParser *parser, const uint8_t *input,
                               size_t length, HParseError *error, bool dumpExecutionTrace,
                               bool dumpInputContext) {
    if (error)
        memset(error, 0, sizeof(*error));
    TRACE_SET_ENABLED(true, dumpExecutionTrace, dumpInputContext);
    HParseResult *res = h_parse__m(mm__, parser, input, length);
    TRACE_SET_ENABLED(false, false, false);
    if (!res)
        TRACE_GET_ERROR(error);
    return res;
}

HParseResult *h_parse_debug_ex(const HParser *parser, const uint8_t *input, size_t length,
                               HParseDiagnostic **diagnostic, bool dumpExecutionTrace,
                               bool dumpInputContext) {
    if (diagnostic)
        *diagnostic = NULL;
    TRACE_SET_ENABLED(true, dumpExecutionTrace, dumpInputContext);
    HParseResult *res = h_parse__m(&system_allocator, parser, input, length);
    TRACE_SET_ENABLED(false, false, false);
    if (!res && diagnostic)
        TRACE_GET_DIAGNOSTIC(diagnostic);
    return res;
}

void h_parse_result_free__m(HAllocator *alloc, HParseResult *result) {
    h_parse_result_free(result);
}

void h_parse_result_free(HParseResult *result) {
    if (result == NULL)
        return;
    h_delete_arena(result->arena);
}
void h_parse_error_free(HParseError *error) {
    if (!error)
        return;
    for (size_t i = 0; i < error->n_deepest; i++) {
        free((void *)error->deepest_parsers[i]); // cast drops the const for free()
        error->deepest_parsers[i] = NULL;
    }
    error->n_deepest = 0;
    free((void *)error->parser);
    error->parser = NULL;
    free((void *)error->message);
    error->message = NULL;
    for (size_t i = 0; i < error->n_context; i++) {
        free((void *)error->context[i]);
        error->context[i] = NULL;
    }
    error->n_context = 0;
    if (error->source) {
        free((void *)error->source->file_name);
        free((void *)error->source->function_name);
        free((void *)error->source);
        error->source = NULL;
    }
}

const HParseError *h_parse_diagnostic_error(const HParseDiagnostic *diagnostic) {
    return diagnostic ? &diagnostic->error : NULL;
}

size_t h_parse_diagnostic_expected_count(const HParseDiagnostic *diagnostic) {
    if (!diagnostic)
        return 0;
    size_t count = diagnostic->expected_eof ? 1 : 0;
    for (size_t lo = 0; lo < 256;) {
        if (!diagnostic->expected_bytes[lo]) {
            lo++;
            continue;
        }
        count++;
        do {
            lo++;
        } while (lo < 256 && diagnostic->expected_bytes[lo]);
    }
    return count;
}

bool h_parse_diagnostic_expected(const HParseDiagnostic *diagnostic, size_t index,
                                 HParseExpectation *expectation) {
    if (!diagnostic || !expectation)
        return false;
    size_t current = 0;
    for (size_t lo = 0; lo < 256;) {
        if (!diagnostic->expected_bytes[lo]) {
            lo++;
            continue;
        }
        size_t hi = lo;
        while (hi + 1 < 256 && diagnostic->expected_bytes[hi + 1])
            hi++;
        if (current++ == index) {
            expectation->kind = H_PARSE_EXPECT_BYTE_RANGE;
            expectation->lower = (uint8_t)lo;
            expectation->upper = (uint8_t)hi;
            return true;
        }
        lo = hi + 1;
    }
    if (diagnostic->expected_eof && current == index) {
        expectation->kind = H_PARSE_EXPECT_END_OF_INPUT;
        expectation->lower = expectation->upper = 0;
        return true;
    }
    return false;
}

static void diagnostic_print_byte(FILE *stream, uint8_t byte) {
    if (byte == '\'' || byte == '\\')
        fprintf(stream, "'\\%c'", byte);
    else if (isprint(byte))
        fprintf(stream, "'%c'", byte);
    else
        fprintf(stream, "0x%02x", byte);
}

void h_parse_diagnostic_fprint(FILE *stream, const HParseDiagnostic *diagnostic) {
    if (!stream || !diagnostic)
        return;
    const HParseError *error = &diagnostic->error;
    size_t last_index = error->end_index > error->index ? error->end_index - 1 : error->end_index;
    fputs("error: ", stream);
    if (error->source) {
        if (error->source->file_name)
            fprintf(stream, "%s", error->source->file_name);
        else
            fputs("<unknown source>", stream);
        if (error->source->line)
            fprintf(stream, ":%zu", error->source->line);
        if (error->source->column)
            fprintf(stream, ":%zu", error->source->column);
        if (error->source->function_name)
            fprintf(stream, " in %s", error->source->function_name);
        fputs(": ", stream);
    }
    bool choice_failure = diagnostic->choice_root != H_TRACE_CHOICE_NONE &&
                          diagnostic->choice_root < diagnostic->choice_node_count;
    if (error->message)
        fprintf(stream, "%s", error->message);
    else if (choice_failure)
        fprintf(stream, "no alternative matched at index %zu", error->index);
    else if (error->kind == H_PARSE_ERROR_RANGE &&
             diagnostic->numeric_range.kind == H_TRACE_NUMERIC_RANGE_SINT)
        fprintf(stream, "unexpected int %" PRId64, diagnostic->numeric_range.actual.sint);
    else if (error->kind == H_PARSE_ERROR_RANGE &&
             diagnostic->numeric_range.kind == H_TRACE_NUMERIC_RANGE_UINT)
        fprintf(stream, "unexpected int %" PRIu64, diagnostic->numeric_range.actual.uint);
    else if (error->kind == H_PARSE_ERROR_RANGE &&
             diagnostic->numeric_range.kind == H_TRACE_NUMERIC_RANGE_FLOAT)
        fprintf(stream, "unexpected float %.17g", diagnostic->numeric_range.actual.floating);
    else if (error->kind == H_PARSE_ERROR_RANGE)
        fputs("mismatched token type", stream);
    else if (error->kind == H_PARSE_ERROR_SEMANTIC_PREDICATE)
        fputs("semantic predicate failed", stream);
    else if (error->kind == H_PARSE_ERROR_ACTION)
        fputs("semantic action failed", stream);
    else if (error->kind == H_PARSE_ERROR_EXPLICIT_FAILURE)
        fprintf(stream, "parser always fails at index %zu", error->index);
    else if (error->kind == H_PARSE_ERROR_XOR)
        fputs("both XOR alternatives matched; exactly one must match", stream);
    else if (error->kind == H_PARSE_ERROR_DIFFERENCE)
        fputs("difference rejected a longer right-hand match", stream);
    else if (error->kind == H_PARSE_ERROR_BUTNOT)
        fputs("but-not rejected a right-hand match that was not shorter", stream);
    else if (error->kind == H_PARSE_ERROR_NO_VALUE)
        fputs("no value to retrieve from provided name", stream);
    else if (error->kind == H_PARSE_ERROR_DISPATCH && diagnostic->dispatch_failure.has_opcode)
        fprintf(stream, "no dispatch case for opcode %zu", diagnostic->dispatch_failure.opcode);
    else if (error->kind == H_PARSE_ERROR_DISPATCH)
        fputs("dispatch discriminator produced an invalid opcode", stream);
    else if (!error->has_actual)
        fprintf(stream, "unexpected end of input at index %zu", error->index);
    else {
        fputs("unexpected byte ", stream);
        diagnostic_print_byte(stream, error->actual);
        fprintf(stream, " (0x%02x = %u) at index %zu", error->actual, error->actual, error->index);
    }

    if (error->message)
        fprintf(stream, " at index %zu", error->index);
    if (error->bit_offset)
        fprintf(stream, ".%ub", error->bit_offset);

    if (!error->message &&
        (error->kind == H_PARSE_ERROR_RANGE || error->kind == H_PARSE_ERROR_SEMANTIC_PREDICATE ||
         error->kind == H_PARSE_ERROR_ACTION || error->kind == H_PARSE_ERROR_XOR ||
         error->kind == H_PARSE_ERROR_DIFFERENCE || error->kind == H_PARSE_ERROR_BUTNOT ||
         error->kind == H_PARSE_ERROR_DISPATCH)) {
        if (error->index != last_index)
            fprintf(stream, " from index %zu to index %zu", error->index, last_index);
        else
            fprintf(stream, " at index %zu", error->index);
    }

    if (!error->message && error->kind == H_PARSE_ERROR_RANGE) {
        if (diagnostic->numeric_range.kind == H_TRACE_NUMERIC_RANGE_SINT ||
            diagnostic->numeric_range.kind == H_TRACE_NUMERIC_RANGE_UINT)
            fprintf(stream, "; expected value between %" PRId64 " and %" PRId64,
                    diagnostic->numeric_range.expected.integer.lower,
                    diagnostic->numeric_range.expected.integer.upper);
        else if (diagnostic->numeric_range.kind == H_TRACE_NUMERIC_RANGE_FLOAT)
            fprintf(stream, "; expected value between %.17g and %.17g",
                    diagnostic->numeric_range.expected.floating.lower,
                    diagnostic->numeric_range.expected.floating.upper);
    }

    if (!error->message && error->kind == H_PARSE_ERROR_DISPATCH &&
        diagnostic->dispatch_failure.expected_count > 0) {
        fputs("; expected opcode ", stream);
        for (size_t i = 0; i < diagnostic->dispatch_failure.expected_count; i++) {
            if (i)
                fputs(", ", stream);
            fprintf(stream, "%" PRIu32, diagnostic->dispatch_failure.expected[i]);
        }
        if (diagnostic->dispatch_failure.expected_truncated)
            fputs(", ...", stream);
    }

    size_t count = error->message ? 0 : h_parse_diagnostic_expected_count(diagnostic);
    if (count > 0) {
        fputs("; expected ", stream);
        for (size_t i = 0; i < count; i++) {
            HParseExpectation expected = {0};
            if (!h_parse_diagnostic_expected(diagnostic, i, &expected))
                continue;
            if (i)
                fputs(", ", stream);
            if (expected.kind == H_PARSE_EXPECT_END_OF_INPUT) {
                fputs("end of input", stream);
            } else {
                diagnostic_print_byte(stream, expected.lower);
                if (expected.upper != expected.lower) {
                    fputc('-', stream);
                    diagnostic_print_byte(stream, expected.upper);
                }
            }
        }
    }
    if (error->n_deepest > 0) {
        fputs(" while running [", stream);
        for (size_t i = 0; i < error->n_deepest; i++) {
            const char *name = error->deepest_parsers[i];
            fprintf(stream, "%s", i ? ", " : "");
            if (name && strncmp(name, "parse_", 6) == 0)
                fprintf(stream, "h%s", name + 5);
            else
                fprintf(stream, "%s", name ? name : "?(no parser)");
        }
        fputc(']', stream);
    } else if (error->parser) {
        if (strncmp(error->parser, "parse_", 6) == 0)
            fprintf(stream, " while running [h%s]", error->parser + 5);
        else
            fprintf(stream, " while running [%s]", error->parser);
    }
    fputc('\n', stream);
    if (choice_failure)
        h_trace_fprint_choice(stream, diagnostic->choice_nodes, diagnostic->choice_node_count,
                              diagnostic->choice_alternatives,
                              diagnostic->choice_alternative_count, diagnostic->choice_root);
}

void h_parse_diagnostic_free(HParseDiagnostic *diagnostic) {
    if (!diagnostic)
        return;
    h_parse_error_free(&diagnostic->error);
    for (size_t i = 0; i < diagnostic->choice_alternative_count; i++)
        h_parse_error_free(&diagnostic->choice_alternatives[i].error);
    free(diagnostic);
}

bool h_false(void *env) {
    (void)env;
    return false;
}

bool h_true(void *env) {
    (void)env;
    return true;
}

bool h_not_regular(HRVMProg *prog, void *env) {
    (void)env;
    return false;
}

int h_compile_for_backend_with_params(HParser *parser, HParserBackendWithParams *be_with_params) {
    HAllocator *mm__ = &system_allocator;

    if (be_with_params) {
        if (be_with_params->mm__)
            mm__ = be_with_params->mm__;
    }

    return h_compile_for_backend_with_params__m(mm__, parser, be_with_params);
}

int h_compile_for_backend_with_params__m(HAllocator *mm__, HParser *parser,
                                         HParserBackendWithParams *be_with_params) {
    int ret = h_compile__m(mm__, parser, be_with_params->backend, be_with_params->params);
    if (!ret)
        be_with_params->backend_vtable = parser->backend_vtable;
    return ret;
}

int h_compile(HParser *parser, HParserBackend backend, const void *params) {
    return h_compile__m(&system_allocator, parser, backend, params);
}

int h_compile__m(HAllocator *mm__, HParser *parser, HParserBackend backend, const void *params) {
    if (!parser) {
        return -1;
    }
    if (backend < PB_MIN || backend > PB_MAX) {
        return -1;
    }
    if (parser->backend >= PB_MIN && parser->backend <= PB_MAX &&
        backends[parser->backend]->free != NULL) {
        backends[parser->backend]->free(parser);
    }
    int ret = backends[backend]->compile(mm__, parser, params);
    if (!ret) {
        parser->backend = backend;
        parser->backend_vtable = backends[backend];
    } else if (backend == PB_LALR && ret == -2 && parser->backend_data != NULL &&
               backends[backend]->free != NULL) {
        /* GLR keeps this table, but a failed direct LALR compile does not. */
        backends[backend]->free(parser);
    }
    return ret;
}

HSuspendedParser *h_parse_start(const HParser *parser) {
    return h_parse_start__m(&system_allocator, parser);
}
HSuspendedParser *h_parse_start__m(HAllocator *mm__, const HParser *parser) {
    if (!parser->backend_vtable || !parser->backend_vtable->parse_start)
        return NULL;

    // allocate and init suspended state
    HSuspendedParser *s = h_new(HSuspendedParser, 1);
    if (!s)
        return NULL;
    s->mm__ = mm__;
    s->parser = parser;
    s->backend_state = NULL;
    s->done = false;
    s->pos = 0;
    s->bit_offset = 0;
    s->endianness = DEFAULT_ENDIANNESS;

    // backend-specific initialization
    // should allocate s->backend_state
    parser->backend_vtable->parse_start(s);

    return s;
}

bool h_parse_chunk(HSuspendedParser *s, const uint8_t *input, size_t length) {
    assert(s->parser->backend_vtable->parse_chunk != NULL);

    // no-op if parser is already done
    if (s->done)
        return true;

    // input
    HInputStream input_stream = {.pos = s->pos,
                                 .index = 0,
                                 .bit_offset = 0,
                                 .overrun = 0,
                                 .endianness = s->endianness,
                                 .length = length,
                                 .input = input,
                                 .last_chunk = false};

    // process chunk
    s->done = s->parser->backend_vtable->parse_chunk(s, &input_stream);
    s->endianness = input_stream.endianness;
    s->pos += input_stream.index;
    s->bit_offset = input_stream.bit_offset;

    return s->done;
}

HParseResult *h_parse_finish(HSuspendedParser *s) {
    assert(s->parser->backend_vtable->parse_chunk != NULL);
    assert(s->parser->backend_vtable->parse_finish != NULL);

    HAllocator *mm__ = s->mm__;

    // signal end of input if parser is not already done
    if (!s->done) {
        HInputStream empty = {.pos = s->pos,
                              .index = 0,
                              .bit_offset = 0,
                              .overrun = 0,
                              .endianness = s->endianness,
                              .length = 0,
                              .input = NULL,
                              .last_chunk = true};

        s->done = s->parser->backend_vtable->parse_chunk(s, &empty);
        assert(s->done);
    }

    // extract result
    HParseResult *r = s->parser->backend_vtable->parse_finish(s);
    if (r)
        r->bit_length = s->pos * 8 + s->bit_offset;

    // NB: backend should have freed backend_state
    h_free(s);

    return r;
}

void h_parser_free(HParser *parser) {
    if (parser == NULL)
        return;

    h_parser_free__m(&system_allocator, parser);

    return;
}

void h_parser_free__m(HAllocator *mm__, HParser *parser) {
    if (parser == NULL || mm__ == NULL) {
        return;
    }
    if (parser->backend_vtable != NULL && parser->backend_vtable->free != NULL) {
        parser->backend_vtable->free(parser);
    }

    if (parser->free_env != NULL) // callback handles explicit environment clenaup
        parser->free_env(mm__, parser->env);
    mm__->free(mm__, parser->diagnostic_label);
    mm__->free(mm__, parser->diagnostic_message);
    if (parser->diagnostic_source) {
        mm__->free(mm__, (void *)parser->diagnostic_source->file_name);
        mm__->free(mm__, (void *)parser->diagnostic_source->function_name);
        mm__->free(mm__, parser->diagnostic_source);
    }
    h_desugar_context_release(parser->desugar_ctx);
    mm__->free(mm__, parser);
}

static bool h_parser_set_diagnostic_text(HParser *parser, char **field, const char *text) {
    if (!parser || !field || !parser->owner_mm__)
        return false;

    HAllocator *allocator = parser->owner_mm__;
    char *copy = NULL;
    if (text) {
        size_t length = strlen(text) + 1;
        copy = allocator->alloc(allocator, length);
        if (!copy)
            return false;
        memcpy(copy, text, length);
    }

    allocator->free(allocator, *field);
    *field = copy;
    return true;
}

bool h_parser_set_label(HParser *parser, const char *label) {
    return parser && h_parser_set_diagnostic_text(parser, &parser->diagnostic_label, label);
}

bool h_parser_set_error_message(HParser *parser, const char *message) {
    return parser && h_parser_set_diagnostic_text(parser, &parser->diagnostic_message, message);
}

static char *h_parser_copy_diagnostic_text(HAllocator *allocator, const char *text) {
    if (!text)
        return NULL;

    size_t length = strlen(text) + 1;
    char *copy = allocator->alloc(allocator, length);
    if (copy)
        memcpy(copy, text, length);
    return copy;
}

static void h_parser_free_source_location(HAllocator *allocator, HSourceLocation *source) {
    if (!source)
        return;
    allocator->free(allocator, (void *)source->file_name);
    allocator->free(allocator, (void *)source->function_name);
    allocator->free(allocator, source);
}

static HSourceLocation *h_parser_copy_source_location(HAllocator *allocator,
                                                      const HSourceLocation *source) {
    HSourceLocation *copy = allocator->alloc(allocator, sizeof(*copy));
    if (!copy)
        return NULL;

    *copy = *source;
    copy->file_name = h_parser_copy_diagnostic_text(allocator, source->file_name);
    if (source->file_name && !copy->file_name) {
        allocator->free(allocator, copy);
        return NULL;
    }

    copy->function_name = h_parser_copy_diagnostic_text(allocator, source->function_name);
    if (source->function_name && !copy->function_name) {
        allocator->free(allocator, (void *)copy->file_name);
        allocator->free(allocator, copy);
        return NULL;
    }
    return copy;
}

typedef struct HContextEnv_ {
    const HParser *child;
    HParser *wrapper;
} HContextEnv;

typedef struct HContextCFClone_ {
    const HCFChoice *source;
    HCFChoice *clone;
    struct HContextCFClone_ *next;
} HContextCFClone;

static HParseResult *parse_context(void *env, HParseState *state) {
    return h_do_parse(((HContextEnv *)env)->child, state);
}

static bool context_is_valid_regular(void *env) {
    const HParser *child = ((HContextEnv *)env)->child;
    return child->vtable->isValidRegular(child->env);
}

static bool context_is_valid_cf(void *env) {
    const HParser *child = ((HContextEnv *)env)->child;
    return child->vtable->isValidCF(child->env);
}

static bool context_compile_to_rvm(HRVMProg *prog, void *env) {
    HContextEnv *context = env;
    const HDiagnosticContext *parent_context = prog->current_context;
    size_t depth = 0;
    for (const HDiagnosticContext *item = parent_context; item; item = item->next)
        depth++;
    HDiagnosticContext *path = h_rvm_alloc(prog, (depth + 1) * sizeof(*path));
    size_t i = 0;
    for (const HDiagnosticContext *item = parent_context; item; item = item->next, i++) {
        path[i] = *item;
        path[i].next = &path[i + 1];
    }
    path[depth].parser = context->wrapper;
    path[depth].choice = NULL;
    path[depth].choice_alternative = 0;
    path[depth].choice_id = 0;
    path[depth].next = NULL;
    prog->current_context = path;
    bool result = h_compile_regex(prog, context->child);
    prog->current_context = parent_context;
    return result;
}

static HCFChoice *context_clone_cf_choice(HAllocator *mm__, const HCFChoice *source,
                                          const HParser *context, HContextCFClone **seen) {
    for (HContextCFClone *entry = *seen; entry; entry = entry->next)
        if (entry->source == source)
            return entry->clone;

    HCFChoice *clone = h_new(HCFChoice, 1);
    HContextCFClone *entry = h_new(HContextCFClone, 1);
    if (!clone || !entry)
        return NULL;
    *clone = *source;
    HDiagnosticContext *provenance = h_new(HDiagnosticContext, 1);
    if (!provenance)
        return NULL;
    provenance->parser = context;
    provenance->choice = NULL;
    provenance->choice_alternative = 0;
    provenance->choice_id = 0;
    provenance->next = source->diagnostic_context;
    clone->diagnostic_context = provenance;
    entry->source = source;
    entry->clone = clone;
    entry->next = *seen;
    *seen = entry;

    if (source->type != HCF_CHOICE)
        return clone;

    size_t alternative_count = 0;
    while (source->data.seq[alternative_count])
        alternative_count++;
    clone->data.seq = h_new(HCFSequence *, alternative_count + 1);
    if (!clone->data.seq)
        return NULL;

    for (size_t i = 0; i < alternative_count; i++) {
        HCFSequence *source_sequence = source->data.seq[i];
        size_t item_count = 0;
        while (source_sequence->items[item_count])
            item_count++;

        HCFSequence *clone_sequence = h_new(HCFSequence, 1);
        if (!clone_sequence)
            return NULL;
        clone_sequence->items = h_new(HCFChoice *, item_count + 1);
        if (!clone_sequence->items)
            return NULL;
        for (size_t j = 0; j < item_count; j++) {
            clone_sequence->items[j] =
                context_clone_cf_choice(mm__, source_sequence->items[j], context, seen);
            if (!clone_sequence->items[j])
                return NULL;
        }
        clone_sequence->items[item_count] = NULL;
        clone->data.seq[i] = clone_sequence;
    }
    clone->data.seq[alternative_count] = NULL;
    return clone;
}

static void desugar_context(HAllocator *mm__, HCFStack *stk__, void *env) {
    HContextEnv *context = env;
    HCFChoice *child = h_desugar(mm__, NULL, context->child);
    HContextCFClone *seen = NULL;
    HCFChoice *clone = child ? context_clone_cf_choice(mm__, child, context->wrapper, &seen) : NULL;

    HCFS_BEGIN_CHOICE() {
        HCFS_BEGIN_SEQ() {
            if (clone)
                HCFS_APPEND(clone);
        }
        HCFS_END_SEQ();
        HCFS_THIS_CHOICE->reshape = h_act_first;
        HDiagnosticContext *provenance = h_new(HDiagnosticContext, 1);
        if (provenance) {
            provenance->parser = context->wrapper;
            provenance->choice = NULL;
            provenance->choice_alternative = 0;
            provenance->choice_id = 0;
            provenance->next = NULL;
            HCFS_THIS_CHOICE->diagnostic_context = provenance;
        }
    }
    HCFS_END_CHOICE();
}

static const HParserVtable context_vt = {
    .parse = parse_context,
    .isValidRegular = context_is_valid_regular,
    .isValidCF = context_is_valid_cf,
    .compile_to_rvm = context_compile_to_rvm,
    .desugar = desugar_context,
    .higher = true,
};

bool h_is_context_parser(const HParser *parser) { return parser && parser->vtable == &context_vt; }

const HParser *h_context_parser_child(const HParser *parser) {
    return h_is_context_parser(parser) ? ((const HContextEnv *)parser->env)->child : NULL;
}

HParser *h_with_context(HParser *parser, const char *label, const HSourceLocation *source) {
    if (!parser || !source || !parser->owner_mm__)
        return NULL;

    HAllocator *allocator = parser->owner_mm__;
    HContextEnv *env = allocator->alloc(allocator, sizeof(*env));
    if (!env)
        return NULL;
    env->child = parser;
    env->wrapper = NULL;

    HParser *wrapper = h_new_parser(allocator, &context_vt, env);
    if (!wrapper) {
        allocator->free(allocator, env);
        return NULL;
    }
    env->wrapper = wrapper;

    HSourceLocation *copy = h_parser_copy_source_location(allocator, source);
    if (!copy) {
        h_parser_free__m(allocator, wrapper);
        return NULL;
    }

    const char *effective_label = label ? label : parser->diagnostic_label;
    if (effective_label &&
        !h_parser_set_diagnostic_text(wrapper, &wrapper->diagnostic_label, effective_label)) {
        h_parser_free_source_location(allocator, copy);
        h_parser_free__m(allocator, wrapper);
        return NULL;
    }
    if (parser->diagnostic_message &&
        !h_parser_set_diagnostic_text(wrapper, &wrapper->diagnostic_message,
                                      parser->diagnostic_message)) {
        h_parser_free_source_location(allocator, copy);
        h_parser_free__m(allocator, wrapper);
        return NULL;
    }

    wrapper->diagnostic_source = copy;
    return wrapper;
}
