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

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>
#include "hammer.h"
#include "internal.h"
#include "allocator.h"
#include "parsers/parser_internal.h"
#include "glue.h"

static HParserBackendVTable *backends[PB_MAX + 1] = {
  &h__missing_backend_vtable, /* For PB_INVALID */
  &h__packrat_backend_vtable, /* For PB_PACKRAT */
  &h__regex_backend_vtable, /* For PB_REGULAR */
  &h__llk_backend_vtable, /* For PB_LLk */
  &h__lalr_backend_vtable, /* For PB_LALR */
  &h__glr_backend_vtable /* For PB_GLR */
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
  } else return 0;
}

HParserBackend h_get_default_backend(void) {
  /* Call the inline version in internal.h */
  return h_get_default_backend__int();
}

HParserBackendVTable * h_get_default_backend_vtable(void){
  return h_get_default_backend_vtable__int();
}

/*
 * Copy an HParserBackendWithParams, using the backend-supplied copy
 * method.
 */

HParserBackendWithParams * h_copy_backend_with_params(
    HParserBackendWithParams *be_with_params) {
  return h_copy_backend_with_params__m(&system_allocator, be_with_params);
}

HParserBackendWithParams * h_copy_backend_with_params__m(HAllocator *mm__,
    HParserBackendWithParams *be_with_params) {
  HParserBackendWithParams *r = NULL;
  int s;

  if (be_with_params && be_with_params->backend >= PB_MIN &&
      be_with_params->backend <= PB_MAX) {
    if (mm__ == NULL) {
      /* use the allocator from the input */
      mm__ = be_with_params->mm__;
    }

    /* got an allocator? */
    if (mm__) {
      r = h_new(HParserBackendWithParams, 1);
      if (r) {
        r->mm__ = mm__;
        r->name = be_with_params->name;
        r->backend = be_with_params->backend;
        r->backend_vtable = be_with_params->backend_vtable;
        if (backends[be_with_params->backend]->copy_params) {
          s = backends[be_with_params->backend]->copy_params(mm__,
              &(r->params), be_with_params->params);
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

  if (out) *out = in; 
  else rv = -1; /* error return, nowhere to write result */

  return rv; 
}

/* Free this HParserBackendWithParams, and free the params too */

void h_free_backend_with_params(HParserBackendWithParams *be_with_params) {
  HAllocator *mm__ = &system_allocator;

  if (be_with_params) {
    if (be_with_params->mm__) mm__ = be_with_params->mm__;

    if (h_is_backend_available(be_with_params->backend)) {
      if (backends[be_with_params->backend]->free_params) {
        backends[be_with_params->backend]->
          free_params(be_with_params->mm__, be_with_params->params);
      }
    }

    if(be_with_params->name) {
    	h_free(be_with_params->name);
    }

    h_free(be_with_params);
  }
}

/*
 * Internal function to query name or descriptive text; the logic is
 * the same in both cases.
 */

static const char * h_get_string_for_backend(
    HParserBackend be, int description) {
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
        ptr = description ?
          &(backends[be]->backend_description) :
          &(backends[be]->backend_short_name);
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

const char * h_get_name_for_backend(HParserBackend be) {
  /*
   * call h_get_string_for_backend(), 0 indicates name
   * rather than description requested
   */
  return h_get_string_for_backend(be, 0);
}

/*
 * Return some human-readable descriptive text for this backend
 */

const char * h_get_descriptive_text_for_backend(HParserBackend be) {
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

static char * h_get_string_for_backend_with_params__m(HAllocator *mm__,
    HParserBackendWithParams *be_with_params, int description) {
  char *text = NULL;
  const char *generic_text = NULL;
  HParserBackend be = PB_INVALID;
  char * (**text_getter_func)(HAllocator *mm__,
                              HParserBackend be,
                              void *params) = NULL;


  if (mm__ == NULL || be_with_params == NULL) goto done;

  /* check if we can compute custom text with params for this backend */
  be = be_with_params->backend;
  if (be >= PB_MIN && be <= PB_MAX && be != PB_INVALID &&
      backends[be] != backends[PB_INVALID]) {
    text_getter_func = description ?
      &(backends[be]->get_description_with_params) :
      &(backends[be]->get_short_name_with_params);

    if (*text_getter_func != NULL) {
        text = (*text_getter_func)(mm__, be,
            be_with_params->params);
    }
  }

  /* got it? */
  if (!text) {
    /* fall back to the generic descriptive text */
    generic_text = h_get_string_for_backend(be, description);
    if (generic_text) {
      text = h_new(char, strlen(generic_text) + 1);
      strncpy(text, generic_text, strlen(generic_text) + 1);
    }
  }

 done:
  return text;
}

/*
 * Allocate and return some human-readable descriptive text for this backend
 * with parameters.  The caller is responsible for freeing the result.
 */

char * h_get_descriptive_text_for_backend_with_params__m(HAllocator *mm__,
    HParserBackendWithParams *be_with_params) {
  return h_get_string_for_backend_with_params__m(mm__, be_with_params, 1);
}

char * h_get_descriptive_text_for_backend_with_params(
    HParserBackendWithParams *be_with_params) {
  return h_get_descriptive_text_for_backend_with_params__m(
      &system_allocator, be_with_params);
}

/* Helpers for the above for backends with no params */

static char * h_get_backend_text_with_no_params(HAllocator *mm__,
                                                HParserBackend be,
                                                int description) {
  char *text = NULL;
  const char *src = NULL;
  int size;

  if (!(mm__ != NULL && be != PB_INVALID &&
        be >= PB_MIN && be <= PB_MAX)) goto done;

  src = description ?
    backends[be]->backend_description :
    backends[be]->backend_short_name;

  if (src) {
    size = strlen(src) + 1;
    text = h_new(char, size);
    if (text) strncpy(text, src, size);
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

char * h_get_description_with_no_params(HAllocator *mm__,
                                        HParserBackend be, void *params) {
  return h_get_backend_text_with_no_params(mm__, be, 1);
}

char * h_get_short_name_with_no_params(HAllocator *mm__,
                                           HParserBackend be, void *params) {
  return h_get_backend_text_with_no_params(mm__, be, 0);
}

char * extract_params_as_string(HParserBackendWithParams *be_with_params , char *remainder) {
	HAllocator *mm__ = &system_allocator;
	char * params_string = NULL;
	size_t params_len = 0;
	char *after_open_paren = remainder + 1;
	char *end_paren = NULL;

	end_paren =	strrchr(remainder, ')');

	if (be_with_params->mm__) mm__ = be_with_params->mm__;

	if(end_paren != NULL) {
		params_len = strlen(after_open_paren) - strlen(end_paren);

		if(params_len > 0) {
			params_string = h_new(char, params_len + 1);
			memset(params_string, '\0', params_len + 1);
			strncpy(params_string, after_open_paren, params_len);
		}
	} /* else error: to what degree do we care right now about enforcing format of the
	   * parameters section of the input, and how are we reporting usage errors like this? */

	return params_string;
}

HParserBackendWithParams * get_backend_with_params_by_name_using_strings(const char *name_with_params) {
	HAllocator *mm__ = &system_allocator;
	HParserBackendWithParams *result = NULL;
	HParserBackend be = PB_INVALID;
	char *remainder = NULL;
	char ** params_as_strings = NULL;
	size_t len, name_len, params_len, num_params;

	if(name_with_params != NULL) {

		result = h_new(HParserBackendWithParams, 1);


		if (result) {
			result->mm__ = mm__;
			result->name = NULL;

			params_len = 0;
			num_params = 0;
			len = strlen(name_with_params);
			remainder = strstr(name_with_params, "(");

			if(remainder != NULL) {

				name_len = len - strlen(remainder);

				/*TODO:
				 * If we are going to go with using string functions to parse this, we'd need to finish having code to split on the commas
				 * (I think long run we'll use the hammer parser approach really?)
				 */
				num_params = 1;
				params_as_strings = h_new(char*, num_params);
				params_as_strings[0] = extract_params_as_string(result, remainder);
				if(params_as_strings[0] != NULL)
					params_len = strlen(params_as_strings[0]);
			} else {
				name_len = len;
			}

			if(name_len > 0) {
				result->name = h_new(char, name_len+1);
				memset(result->name, '\0', name_len+1);
				strncpy(result->name, name_with_params, name_len);

				be = h_query_backend_by_name(result->name);

				result->backend = be;

				result->backend_vtable = NULL;
				if (be >= PB_MIN && be <= PB_MAX)
					result->backend_vtable = backends[be];
				/* use the backend supplied method to extract any params from the input */


				if(params_len > 0) {
					if (result->backend_vtable != NULL && be != PB_INVALID &&
							result->backend_vtable != backends[PB_INVALID]) {
						if (result->backend_vtable->extract_params) {
							result->backend_vtable->extract_params(&(result->params), params_as_strings);
						}
					}
				}
			}

			//free everything we don't need to return the result:
			if(params_as_strings != NULL) {
				for (size_t i = 0; i < num_params; i++) {
					h_free(params_as_strings[i]);
				}
				h_free(params_as_strings);
			}
		}
	}
	return result;
}

/*TODO: possibly move to its own file?
 * If so, move include of glue.h as well
 * this parser is the only code using glue.h in here
 */

HParsedToken *act_backend_with_params(const HParseResult *p, void* user_data)
{
	backend_with_params_t *be_with_params = H_ALLOC(backend_with_params_t);

	backend_name_t *name = H_FIELD(backend_name_t, 0);
	be_with_params->name = *name;

	backend_params_t *params = H_FIELD(backend_params_t, 2);
	be_with_params->params = *params;

	return H_MAKE(backend_with_params_t, (void*)be_with_params);
}

HParsedToken* act_backend_name(const HParseResult *p, void* user_data) {
	backend_name_t *r = H_ALLOC(backend_name_t);

	HParsedToken *flat = h_act_flatten(p, user_data);

	r->len = h_seq_len(flat);
	r->name = h_arena_malloc(p->arena, r->len + 1);
	for (size_t i=0; i<r->len; ++i) {

		r->name[i] = flat->seq->elements[i]->uint;
	}
	r->name[r->len] = 0;

	return H_MAKE(backend_name_t, r);
}

HParsedToken *act_param(const HParseResult *p, void* user_data)
{
	backend_param_t *r = H_ALLOC(backend_param_t);

	r->len = h_seq_len(p->ast);
	r->param = h_arena_malloc(p->arena, r->len + 1);
	for (size_t i=0; i<r->len; ++i)
		r->param[i] = H_FIELD_UINT(i);
	r->param[r->len] = 0;

	return H_MAKE(backend_param_t, r);

}

HParsedToken *act_backend_params(const HParseResult *p, void* user_data)
{
	HParsedToken *res = H_MAKE(backend_params_t, (void*)p->ast);

	backend_params_t *bp = H_ALLOC(backend_params_t);

	HParsedToken **fields = h_seq_elements(p->ast);

	bp->len  = h_seq_len(p->ast);
	bp->params = h_arena_malloc(p->arena, sizeof(backend_param_t)*bp->len);
	for(size_t i=0; i<bp->len; i++) {
		bp->params[i] = *H_INDEX(backend_param_t, p->ast, i);
	}

	return H_MAKE(backend_params_t, bp);

	return res;
}

static HParser * build_hparser_rule(void) {


	// H_RULE(digit, h_ch_range(0x30, 0x39));
	H_RULE(alpha, h_choice(h_ch_range('A', 'Z'), h_ch_range('a', 'z'), NULL));
	H_RULE(digit, h_ch_range(0x30, 0x39));
	H_RULE(sp, h_ch(' '));
	H_RULE(comma, h_ch(','));
	H_RULE(left_paren, h_ch('('));
	H_RULE(right_paren, h_ch(')'));

	H_RULE(alphas, h_many1(alpha));
	H_RULE(digits, h_many1(digit));

	H_ARULE(backend_name, h_sequence(
			alphas,
			h_many(h_choice(digit, alpha, NULL)),
			NULL));
	H_ARULE(param, h_choice(digits, alphas, NULL));
	H_ARULE(backend_params, h_sepBy(param,comma));

	//TODO: work out how to make this an optional part of the final parser
	//so calls can be of form "glr" not just "glr()" for no params
	//current issue is that the action for backend_params craps out on this form somehow
	// H_RULE(params, h_sequence(left_paren, backend_params, right_paren, NULL));
	//H_ARULE(backend_with_params, h_sequence(backend_name, h_optional(params), NULL));

	H_ARULE(backend_with_params, h_sequence(backend_name, left_paren, backend_params, right_paren, NULL));

	return backend_with_params;
}



static HParser * build_hparser(void) {

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


HParserBackendWithParams * get_backend_with_params_by_name_using_hammer_parser(const char *name_with_params) {
	HAllocator *mm__ = &system_allocator;
	HParserBackendWithParams *result = NULL;
	HParserBackend be = PB_INVALID;
	HParser *parser = NULL;
	HParseResult *r = NULL;
	char ** parsed_params = NULL;

	if(name_with_params != NULL) {
		result = h_new(HParserBackendWithParams, 1);
		if (result) {
			result->mm__ = mm__;
			result->name = NULL;

			parser = build_hparser();
			if (!parser) {
				return NULL;
			}

			r = h_parse(parser, (const uint8_t *)name_with_params, strlen(name_with_params));

			if (r) {

				backend_with_params_t *be_w_params = r->ast->user;


				backend_name_t *name = &be_w_params->name;

				backend_params_t *params = &be_w_params->params;

				result->name = h_new(char,name->len+1);
				memset(result->name, '\0', name->len+1);
				strncpy(result->name, (char*) name->name, name->len);

				parsed_params = h_new(char*, params->len);
				for (size_t i = 0; i < params->len; i++) {
					parsed_params[i] = h_new(char, params->params[i].len+1);
					memset(parsed_params[i], '\0', params->params[i].len+1);
					strncpy(parsed_params[i], (char*) params->params[i].param, params->params[i].len);
				}

				be = h_query_backend_by_name(result->name);

				result->backend = be;
				result->backend_vtable = NULL;
				if (be >= PB_MIN && be <= PB_MAX)
					result->backend_vtable =  backends[be];
				/* use the backend supplied method to extract any params from the input */
				result->params = NULL;
				if(params->len > 0) {
					if (result->backend_vtable != NULL && be != PB_INVALID &&
							result->backend_vtable != backends[PB_INVALID]) {
						if (result->backend_vtable->extract_params) {
							result->backend_vtable->extract_params(&(result->params), parsed_params);
						}
					}
				}
				// free the parsed parameters strings
				if(parsed_params != NULL) {
					for (size_t i = 0; i < params->len; i++) {
						h_free(parsed_params[i]);
					}
					h_free(parsed_params);
				}

			// free the parse result
			   h_parse_result_free(r);
			   r = NULL;

			  //free the parser
		      backends[parser->backend]->free(parser);
			}
		}
	}
	return result;
}

HParserBackendWithParams * h_get_backend_with_params_by_name(const char *name_with_params) {
    HParserBackendWithParams *result = NULL;

	result = get_backend_with_params_by_name_using_hammer_parser(name_with_params);
			//get_backend_with_params_by_name_using_strings(name_with_params);

	return result;
}


#define DEFAULT_ENDIANNESS (BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN)

HParseResult* h_parse(const HParser* parser, const uint8_t* input, size_t length) {
  return h_parse__m(&system_allocator, parser, input, length);
}
HParseResult* h_parse__m(HAllocator* mm__, const HParser* parser, const uint8_t* input, size_t length) {
  // Set up a parse state...
  HInputStream input_stream = {
    .pos = 0,
    .index = 0,
    .bit_offset = 0,
    .overrun = 0,
    .endianness = DEFAULT_ENDIANNESS,
    .length = length,
    .input = input,
    .last_chunk = true
  };
  
  return backends[parser->backend]->parse(mm__, parser, &input_stream);
}

void h_parse_result_free__m(HAllocator *alloc, HParseResult *result) {
  h_parse_result_free(result);
}

void h_parse_result_free(HParseResult *result) {
  if(result == NULL) return;
  h_delete_arena(result->arena);
}

bool h_false(void* env) {
  (void)env;
  return false;
}

bool h_true(void* env) {
  (void)env;
  return true;
}

bool h_not_regular(HRVMProg *prog, void *env) {
  (void)env;
  return false;
}

int h_compile_for_backend_with_params(HParser* parser, HParserBackendWithParams* be_with_params){
	HAllocator *mm__ = &system_allocator;

	if (be_with_params) {
	    if (be_with_params->mm__) mm__ = be_with_params->mm__;
	}

	return h_compile_for_backend_with_params__m(mm__, parser, be_with_params);
}

int h_compile_for_backend_with_params__m(HAllocator* mm__, HParser* parser, HParserBackendWithParams* be_with_params) {
	return h_compile__m(mm__, parser, be_with_params->backend, be_with_params->params);
}

int h_compile(HParser* parser, HParserBackend backend, const void* params) {
  return h_compile__m(&system_allocator, parser, backend, params);
}

int h_compile__m(HAllocator* mm__, HParser* parser, HParserBackend backend, const void* params) {
  if (parser->backend >= PB_MIN && parser->backend <= PB_MAX &&
      backends[parser->backend]->free != NULL) {
    backends[parser->backend]->free(parser);
  }
  int ret = backends[backend]->compile(mm__, parser, params);
  if (!ret)
    parser->backend = backend;
  return ret;
}


HSuspendedParser* h_parse_start(const HParser* parser) {
  return h_parse_start__m(&system_allocator, parser);
}
HSuspendedParser* h_parse_start__m(HAllocator* mm__, const HParser* parser) {
  if(!backends[parser->backend]->parse_start)
    return NULL;

  // allocate and init suspended state
  HSuspendedParser *s = h_new(HSuspendedParser, 1);
  if(!s)
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
  backends[parser->backend]->parse_start(s);

  return s;
}

bool h_parse_chunk(HSuspendedParser* s, const uint8_t* input, size_t length) {
  assert(backends[s->parser->backend]->parse_chunk != NULL);

  // no-op if parser is already done
  if(s->done)
    return true;

  // input 
  HInputStream input_stream = {
    .pos = s->pos,
    .index = 0,
    .bit_offset = 0,
    .overrun = 0,
    .endianness = s->endianness,
    .length = length,
    .input = input,
    .last_chunk = false
  };

  // process chunk
  s->done = backends[s->parser->backend]->parse_chunk(s, &input_stream);
  s->endianness = input_stream.endianness;
  s->pos += input_stream.index;
  s->bit_offset = input_stream.bit_offset;

  return s->done;
}

HParseResult* h_parse_finish(HSuspendedParser* s) {
  assert(backends[s->parser->backend]->parse_chunk != NULL);
  assert(backends[s->parser->backend]->parse_finish != NULL);

  HAllocator *mm__ = s->mm__;

  // signal end of input if parser is not already done
  if(!s->done) {
    HInputStream empty = {
      .pos = s->pos,
      .index = 0,
      .bit_offset = 0,
      .overrun = 0,
      .endianness = s->endianness,
      .length = 0,
      .input = NULL,
      .last_chunk = true
    };

    s->done = backends[s->parser->backend]->parse_chunk(s, &empty);
    assert(s->done);
  }

  // extract result
  HParseResult *r = backends[s->parser->backend]->parse_finish(s);
  if(r)
    r->bit_length = s->pos * 8 + s->bit_offset;

  // NB: backend should have freed backend_state
  h_free(s);

  return r;
}
