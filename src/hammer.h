/* Parser combinators for binary formats.
 * Copyright (c) 2025 Riverside Research
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

#ifndef HAMMER_HAMMER__H
#define HAMMER_HAMMER__H

#include "compiler_specifics.h"

#ifndef HAMMER_INTERNAL__NO_STDARG_H
#include <stdarg.h>
#endif // HAMMER_INTERNAL__NO_STDARG_H
#include "allocator.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define BYTE_BIG_ENDIAN 0x1
#define BIT_BIG_ENDIAN 0x2
#define BIT_LITTLE_ENDIAN 0x0
#define BYTE_LITTLE_ENDIAN 0x0

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HParseState_ HParseState;

/**
 * @enum HParserBackend
 * @brief Available parser backend implementations
 */
typedef enum HParserBackend_ {
    PB_MIN = 0,
    PB_INVALID = PB_MIN, /**< Have a backend that always fails to pass around "no such backend"
                            indications */
    PB_PACKRAT,
    PB_REGULAR,
    PB_LLk,
    PB_LALR,
    PB_GLR,
    PB_MAX = PB_GLR
} HParserBackend;

typedef struct HParserBackendVTable_ HParserBackendVTable;

/**
 * @struct HParserBackendWithParams
 * @brief Backend configuration with parameters
 */
typedef struct HParserBackendWithParams_ {
    /**< Name of backend extracted from a string if the choice of backend was specified in a call
     * using a string */
    char *requested_name;

    /**< The backend (if backend is to be loaded from an external module set to invalid (?))*/
    HParserBackend backend;

    /**< Backend vtable (TODO: use this instead of the enum so we can get rid of that) */
    HParserBackendVTable *backend_vtable;

    /*
     * Backend-specific parameters - if this needs to be freed, the backend should provide a
     * free_params method in its vtable; currently no backends do this - PB_PACKRAT and PB_REGULAR
     * take no params, and PB_LLk, PB_LALR and PB_GLR take an integer cast to void*
     */
    void *params;

    /**< Allocator to use to free this (and the params if necessary) */
    HAllocator *mm__;
} HParserBackendWithParams;

/**
 * @enum HTokenType
 * @brief Token types for parsed results
 */
typedef enum HTokenType_ {
    TT_INVALID = 0,
    TT_NONE = 1,
    TT_BYTES = 2,
    TT_SINT = 4,
    TT_UINT = 8,
    TT_DOUBLE = 12,
    TT_FLOAT = 13,
    TT_SEQUENCE = 16,
    TT_RESERVED_1, /**< reserved for backend-specific internal use */
    TT_ERR = 32,
    TT_USER = 64,
    TT_MAX
} HTokenType;

/**
 * @struct HCountedArray
 * @brief Dynamic array of parsed tokens
 */
typedef struct HCountedArray_ {
    size_t capacity;
    size_t used;
    HArena *arena;
    struct HParsedToken_ **elements;
} HCountedArray;

/**
 * @struct HBytes
 * @brief Byte array representation
 */
typedef struct HBytes_ {
    const uint8_t *token;
    size_t len;
} HBytes;

#ifdef SWIG
typedef union {
  HBytes bytes;
  int64_t sint;
  uint64_t uint;
  double dbl;
  float flt;
  HCountedArray *seq;
  void *user;
} HTokenData;
#endif

/**
 * @struct HParsedToken
 * @brief Parsed token with type and value
 */
typedef struct HParsedToken_ {
    HTokenType token_type;
#ifndef SWIG
    union {
        HBytes bytes;
        int64_t sint;
        uint64_t uint;
        double dbl;
        float flt;
        HCountedArray *seq; /**< a sequence of HParsedToken's */
        void *user;
    };
#else
    HTokenData token_data;
#endif
    size_t index;
    size_t bit_length;
    char bit_offset;
} HParsedToken;

/**
 * @struct HParseResult
 * @brief The result of a successful parse. Note that this may reference the input string. If a
 * parse fails, the parse result will be NULL. If a parse is successful but there's nothing there
 * (i.e., if end_p succeeds) then there's a parse result but its ast is NULL.
 */
typedef struct HParseResult_ {
    const HParsedToken *ast; /**< Abstract syntax tree */
    int64_t bit_length;
    HArena *arena; /**< Memory arena for the parse result */
} HParseResult;

/**
 * TODO: document me.
 * Relevant functions: h_bit_writer_new, h_bit_writer_put, h_bit_writer_get_buffer,
 * h_bit_writer_free
 */
typedef struct HBitWriter_ HBitWriter;

typedef struct HCFChoice_ HCFChoice;
typedef struct HRVMProg_ HRVMProg;
typedef struct HParserVtable_ HParserVtable;

// TODO: Make this internal
typedef struct HParser_ {
    const HParserVtable *vtable;
    HParserBackend backend;
    HParserBackendVTable *backend_vtable;
    void *backend_data;
    void *env;
    HCFChoice *desugared; /**< if the parser can be desugared, its desugared form */
} HParser;

typedef struct HSuspendedParser_ HSuspendedParser;

/**
 * @typedef HAction
 * @brief Type of an action to apply to an AST, used in the action() parser. It can be any
 * (user-defined) function that takes a HParseResult* and returns a HParsedToken*. (This is so that
 * the user doesn't have to worry about memory allocation; action() does that for you.) Note that
 * the tagged union in HParsedToken* supports user-defined types, so you can create your own token
 * types (corresponding to, say, structs) and stuff values for them into the void* in the tagged
 * union in HParsedToken.
 *
 * @param p The parse result to apply the action to.
 * @param user_data Arbitrary user data pointer passed through from the action() parser.
 */
typedef HParsedToken *(*HAction)(const HParseResult *p, void *user_data);

/**
 * @typedef HPredicate
 * @brief Type of a boolean attribute-checking function, used in the attr_bool() parser. It can be
 * any (user-defined) function that takes a HParseResult* and returns true or false.
 */
typedef bool (*HPredicate)(HParseResult *p, void *user_data);

/**
 * @typedef HContinuation
 * @brief Type of a parser that depends on the result of a previous parser,used in h_bind(). The
 * void* argument is passed through from h_bind() and can be used to arbitrarily parameterize the
 * function further. The HAllocator* argument gives access to temporary memory and is to be used for
 * any allocations inside the function. Specifically, construction of any HParsers should use the
 * '__m' combinator variants with the given allocator. Anything allocated thus will be freed by
 * 'h_bind'.
 *
 * @param mm__ Allocator to use for any allocations needed to construct the returned parser.
 */
typedef HParser *(*HContinuation)(HAllocator *mm__, const HParsedToken *x, void *env);

enum BackendTokenType_ {
    TT_backend_with_params_t = TT_USER,
    TT_backend_name_t,
    TT_backend_param_t,
    TT_backend_param_name_t,
    TT_backend_param_with_name_t,
    TT_backend_params_t
};

typedef struct backend_param {
    size_t len;
    uint8_t *param;
    uint8_t *param_name;
} backend_param_t;

typedef struct backend_param_name {
    size_t len;
    uint8_t *param_name;
    size_t param_id;
} backend_param_name_t;

typedef struct backend_param_with_name {
    backend_param_name_t param_name;
    backend_param_t param;
} backend_param_with_name_t;

typedef struct {
    uint8_t *name;
    size_t len;
} backend_name_t;

typedef struct backend_params {
    backend_param_with_name_t *params;
    size_t len;
} backend_params_t;

typedef struct backend_with_params {
    backend_name_t name;
    backend_params_t params;
} backend_with_params_t;

/**
 * @defgroup benchmarking Benchmarking and Test Case Management
 * @{
 */

typedef struct HParserTestcase_ {
    unsigned char *input;
    size_t length;
    char *output_unambiguous;
} HParserTestcase;

#ifdef SWIG
typedef union {
  const char* actual_results;
  size_t parse_time;
} HResultTiming;
#endif

typedef struct HCaseResult_ {
    bool success;
#ifndef SWIG
    union {
        const char
            *actual_results; /**< on failure, filled in with the results of h_write_result_unamb */
        size_t parse_time;   /**< on success, filled in with time for a single parse, in nsec */
    };
#else
    HResultTiming timestamp;
#endif
    size_t length;
} HCaseResult;

typedef struct HBackendResults_ {
    HParserBackend backend;
    bool compile_success;
    size_t n_testcases;
    size_t failed_testcases; /**< actually a count... */
    HCaseResult *cases;
} HBackendResults;

typedef struct HBenchmarkResults_ {
    size_t len;
    HBackendResults *results;
} HBenchmarkResults;

/** @} */

/**
 * @defgroup backend_functions Backend Management
 * @{
 */

/**
 * @brief Check if backend is available
 * @param backend Backend to check
 * @return 1 if available, 0 otherwise
 */
int h_is_backend_available(HParserBackend backend);

/**
 * @brief Get default backend (currently PB_PACKRAT)
 * @return Default backend
 */
HParserBackend h_get_default_backend(void);

/**
 * @brief Get default backend vtable
 * @return Backend function table
 */
HParserBackendVTable *h_get_default_backend_vtable(void);

/**
 * @brief Copy backend configuration with parameters
 * @param be_with_params Backend configuration to copy
 * @return New backend configuration
 */
HParserBackendWithParams *h_copy_backend_with_params(HParserBackendWithParams *be_with_params);
HParserBackendWithParams *h_copy_backend_with_params__m(HAllocator *mm__,
                                                        HParserBackendWithParams *be_with_params);

/**
 * @brief Free backend configuration
 * @param be_with_params Backend configuration to free
 */
void h_free_backend_with_params(HParserBackendWithParams *be_with_params);

/**
 * @brief Get backend name string
 * @param be Backend type
 * @return Constant name string (do not free)
 */
const char *h_get_name_for_backend(HParserBackend be);

/**
 * @brief Get backend configuration name string
 * @param be_with_params Backend configuration
 * @return Allocated name string (caller must free)
 */
char *h_get_name_for_backend_with_params(HParserBackendWithParams *be_with_params);
char *h_get_name_for_backend_with_params__m(HAllocator *mm__,
                                            HParserBackendWithParams *be_with_params);

/**
 * @brief Get backend descriptive text
 * @param be Backend type
 * @return const char* (do not free)
 */
const char *h_get_descriptive_text_for_backend(HParserBackend be);

/**
 * @brief Get backend configuration descriptive text
 * @param be_with_params Backend configuration
 * @return Allocated descriptive text (caller must free)
 */
char *h_get_descriptive_text_for_backend_with_params(HParserBackendWithParams *be_with_params);
char *h_get_descriptive_text_for_backend_with_params__m(HAllocator *mm__,
                                                        HParserBackendWithParams *be_with_params);

/**
 * @brief Look up backend by name
 * @param name Backend name
 * @return Backend type or PB_INVALID
 */
HParserBackend h_query_backend_by_name(const char *name);

/**
 * @brief Parse backend specification string
 * @param name_with_params String like "lalr(1)"
 * @return Backend configuration
 */
HParserBackendWithParams *h_get_backend_with_params_by_name(const char *name_with_params);
HParserBackendWithParams *h_get_backend_with_params_by_name__m(HAllocator *mm__,
                                                               const char *name_with_params);

/** @} */

/**
 * @defgroup parsing Parsing Functions
 * @{
 */

/**
 * @brief Top-level function to call a parser that has been built over some piece of input (of known
 * size).
 *
 * @param parser Parser to use
 * @param input Input data
 * @param length Length of input data
 * @return Parse result, or NULL on failure
 */
HParseResult *h_parse(const HParser *parser, const uint8_t *input, size_t length);
HParseResult *h_parse__m(HAllocator *mm__, const HParser *parser, const uint8_t *input,
                         size_t length);

/**
 * @brief Initialize a parser for iteratively consuming an input stream in chunks. This is only
 * supported by some backends.
 *
 * @param parser Parser to use
 * @return Result is NULL if not supported by the backend.
 */
HSuspendedParser *h_parse_start(const HParser *parser);
HSuspendedParser *h_parse_start__m(HAllocator *mm__, const HParser *parser);

/**
 * @brief Run a suspended parser (as returned by h_parse_start) on a chunk of input.
 * @param s Suspended parser state
 * @param input Input data chunk
 * @param length Length of input data chunk
 * @return Returns true if the parser is done (needs no more input).
 */
bool h_parse_chunk(HSuspendedParser *s, const uint8_t *input, size_t length);

/**
 * @brief Finish an iterative parse. Signals the end of input to the backend and returns the parse
 * result.
 *
 * @param s Suspended parser state
 * @return Parse result, or NULL on failure
 */
HParseResult *h_parse_finish(HSuspendedParser *s);

/** @} */

/**
 * @defgroup basic_parsers Basic Parser Combinators
 * @{
 */

/**
 * @brief Given a string, returns a parser that parses that string value.
 * @param str String to parse
 * @param len Length of string to parse
 * @return Result token type: TT_BYTES
 */
HParser *h_token(const uint8_t *str, const size_t len);
HParser *h_token__m(HAllocator *mm__, const uint8_t *str, const size_t len);

/**
 * @brief Parse literal string (macro convenience)
 * @param s String literal
 */
#define h_literal(s) h_token(((const uint8_t *)(s)), sizeof(s) - 1)

/**
 * @brief Given a single character, returns a parser that parses that character.
 * @param c Character to parse
 * @return Result token type: TT_UINT
 */
HParser *h_ch(const uint8_t c);
HParser *h_ch__m(HAllocator *mm__, const uint8_t c);

/**
 * @brief Parse character in range
 * @param lower Lower bound (inclusive)
 * @param upper Upper bound (inclusive)
 * @return Result token type: TT_UINT
 */
HParser *h_ch_range(const uint8_t lower, const uint8_t upper);
HParser *h_ch_range__m(HAllocator *mm__, const uint8_t lower, const uint8_t upper);

/**
 * @brief Parse integer in range
 * @param p Integer parser (e.g., h_int8(), h_uint32(), etc.)
 * @param lower Lower bound (inclusive)
 * @param upper Upper bound (inclusive)
 * @return Result token type: Same as p's result type
 */
HParser *h_int_range(const HParser *p, const int64_t lower, const int64_t upper);
HParser *h_int_range__m(HAllocator *mm__, const HParser *p, const int64_t lower,
                        const int64_t upper);

/**
 * @brief Parse fixed number of bits
 * @param len Number of bits
 * @param sign true for signed, false for unsigned
 * @return Result token type: TT_SINT if sign == true, TT_UINT if sign == false
 */
HParser *h_bits(size_t len, _Bool sign);
HParser *h_bits__m(HAllocator *mm__, size_t len, _Bool sign);

/**
 * @brief Parse fixed number of bytes
 * @param len Number of bytes
 * @return Result token type: TT_BYTES
 */
HParser *h_bytes(size_t len);
HParser *h_bytes__m(HAllocator *mm__, size_t len);

/** @} */

/**
 * @defgroup integer_parsers Integer Parsers
 * @{
 */

/**
 * @brief Parse signed 64-bit integer
 * @return Result token type: TT_SINT
 */
HParser *h_int64(void);
HParser *h_int64__m(HAllocator *mm__);

/**
 * @brief Parse signed 32-bit integer
 * @return Result token type: TT_SINT
 */
HParser *h_int32(void);
HParser *h_int32__m(HAllocator *mm__);

/**
 * @brief Parse signed 16-bit integer
 * @return Result token type: TT_SINT
 */
HParser *h_int16(void);
HParser *h_int16__m(HAllocator *mm__);

/**
 * @brief Parse signed 8-bit integer
 * @return Result token type: TT_SINT
 */
HParser *h_int8(void);
HParser *h_int8__m(HAllocator *mm__);

/**
 * @brief Parse unsigned 64-bit integer
 * @return Result token type: TT_UINT
 */
HParser *h_uint64(void);
HParser *h_uint64__m(HAllocator *mm__);

/**
 * @brief Parse unsigned 32-bit integer
 * @return Result token type: TT_UINT
 */
HParser *h_uint32(void);
HParser *h_uint32__m(HAllocator *mm__);

/**
 * @brief Parse unsigned 16-bit integer
 * @return Result token type: TT_UINT
 */
HParser *h_uint16(void);
HParser *h_uint16__m(HAllocator *mm__);

/**
 * @brief Parse unsigned 8-bit integer
 * @return Result token type: TT_UINT
 */
HParser *h_uint8(void);
HParser *h_uint8__m(HAllocator *mm__);

/** @} */

/** @defgroup combinators Parser Combinators
 * @{
 */

/**
 * @brief Given another parser, p, returns a parser that skips any whitespace and then applies p.
 * @param p Parser to apply after whitespace
 * @return Result token type: p's result type
 */
HParser *h_whitespace(const HParser *p);
HParser *h_whitespace__m(HAllocator *mm__, const HParser *p);

/**
 * @brief Given two parsers, p and q, returns a parser that parses them in sequence but only returns
 * p's result.
 *
 * @param p First parser
 * @param q Second parser
 * @return Result token type: p's result type
 */
HParser *h_left(const HParser *p, const HParser *q);
HParser *h_left__m(HAllocator *mm__, const HParser *p, const HParser *q);

/**
 * @brief Given two parsers, p and q, returns a parser that parses them in sequence but only returns
 * q's result.
 *
 * @param p First parser
 * @param q Second parser
 * @return Result token type: q's result type
 */
HParser *h_right(const HParser *p, const HParser *q);
HParser *h_right__m(HAllocator *mm__, const HParser *p, const HParser *q);

/**
 * @brief Given three parsers, p, x, and q, returns a parser that parses them in sequence but only
 * returns x's result
 * @param p First parser
 * @param x Middle parser
 * @param q Last parser
 * @return Result token type: x's result type
 */
HParser *h_middle(const HParser *p, const HParser *x, const HParser *q);
HParser *h_middle__m(HAllocator *mm__, const HParser *p, const HParser *x, const HParser *q);

/**
 * @brief Given another parser, p, and a function f, returns a parser that applies p, then applies f
 * to everything in the AST of p's result
 * @param p Parser to wrap
 * @param a Action function
 * @param user_data Context for action
 * @return Result token type: any
 */
HParser *h_action(const HParser *p, const HAction a, void *user_data);
HParser *h_action__m(HAllocator *mm__, const HParser *p, const HAction a, void *user_data);

/**
 * @brief Parse a single character in the given charset
 * @param charset Character set
 * @param length Charset length
 * @return Result token type: TT_UINT
 */
HParser *h_in(const uint8_t *charset, size_t length);
HParser *h_in__m(HAllocator *mm__, const uint8_t *charset, size_t length);

/**
 * @brief Parse a single character *NOT* in the given charset
 * @param charset Character set to exclude
 * @param length Charset length
 * @return Result token type: TT_UINT
 */
HParser *h_not_in(const uint8_t *charset, size_t length);
HParser *h_not_in__m(HAllocator *mm__, const uint8_t *charset, size_t length);

/**
 * @brief A no-argument parser that succeeds if there is no more input to parse.
 * @return Result token type: None. The HParseResult exists but its AST is NULL.
 */
HParser *h_end_p(void);
HParser *h_end_p__m(HAllocator *mm__);

/**
 * @brief This parser always fails.
 * @return Result token type: NULL. Always.
 */
HParser *h_nothing_p(void);
HParser *h_nothing_p__m(HAllocator *mm__);

/**
 * @brief Given a null-terminated list of parsers, apply each parser in order. The parse succeeds
 * only if all parsers succeed.
 *
 * @result Result token type: TT_SEQUENCE
 */
HParser *h_sequence(HParser *p, ...) __attribute__((sentinel));
HParser *h_sequence__m(HAllocator *mm__, HParser *p, ...) __attribute__((sentinel));
HParser *h_sequence__mv(HAllocator *mm__, HParser *p, va_list ap);
HParser *h_sequence__v(HParser *p, va_list ap);
HParser *h_sequence__a(void *args[]);
HParser *h_sequence__ma(HAllocator *mm__, void *args[]);

#define h_drop_from(p, ...) h_drop_from_(p, __VA_ARGS__, -1)

/**
 * @brief Given an `h_sequence` and a list of indices, returns a parser that parses the sequence but
 * returns it without the results at the dropped indices. If a negative integer appears in the
 * middle of the list, this combinator will silently ignore the rest of the list.
 * @param p Sequence parser
 * @param ... Indices of elements to drop
 * @return Result token type: TT_SEQUENCE
 */
HParser *h_drop_from_(HParser *p, ...);
HParser *h_drop_from___m(HAllocator *mm__, HParser *p, ...);
HParser *h_drop_from___mv(HAllocator *mm__, HParser *p, va_list ap);
HParser *h_drop_from___v(HParser *p, va_list ap);
HParser *h_drop_from___a(void *args[]);
HParser *h_drop_from___ma(HAllocator *mm__, void *args[]);

/**
 * @brief Given an array of parsers, p_array, apply each parser in order. The first parser to
 * succeed is the result; if no parsers succeed, the parse fails.
 * @param p_array Array of parsers
 * @return Result token type: The type of the first successful parser's result.
 */
HParser *h_choice(HParser *p, ...) __attribute__((sentinel));
HParser *h_choice__m(HAllocator *mm__, HParser *p, ...) __attribute__((sentinel));
HParser *h_choice__mv(HAllocator *mm__, HParser *p, va_list ap);
HParser *h_choice__v(HParser *p, va_list ap);
HParser *h_choice__a(void *args[]);
HParser *h_choice__ma(HAllocator *mm__, void *args[]);

/**
 * @brief Given a null-terminated list of parsers, match a permutation phrase of these parsers, i.e.
 * match all parsers exactly once in any order.
 *
 * If multiple orders would match, the lexically smallest permutation is used; in other words, at
 * any step the remaining available parsers are tried in the order in which they appear in the
 * arguments.
 *
 * As an exception, 'h_optional' parsers (actually those that return a result of token type
 * TT_NONE) are detected and the algorithm will try to match them with a non-empty result.
 * Specifically, a result of TT_NONE is treated as a non-match as long as any other argument
 * matches.
 *
 * Other parsers that succeed on any input (e.g. h_many), that match the same input as others, or
 * that match input which is a prefix of another match can lead to unexpected results and should
 * probably not be used as arguments.
 *
 * The result is a sequence of the same length as the argument list. Each parser's result is placed
 * at that parser's index in the arguments. The permutation itself (the order in which the
 * arguments were matched) is not returned.
 *
 * @param p Null-terminated list of parsers
 * @return Result token type: TT_SEQUENCE
 */
HParser *h_permutation(HParser *p, ...) __attribute__((sentinel));
HParser *h_permutation__m(HAllocator *mm__, HParser *p, ...) __attribute__((sentinel));
HParser *h_permutation__mv(HAllocator *mm__, HParser *p, va_list ap);
HParser *h_permutation__v(HParser *p, va_list ap);
HParser *h_permutation__a(void *args[]);
HParser *h_permutation__ma(HAllocator *mm__, void *args[]);

/**
 * @brief Given two parsers, p1 and p2, this parser succeeds in the following cases:
 * - if p1 succeeds and p2 fails
 * - if both succeed but p1's result is as long as or longer than p2's
 *
 * @param p1 First parser
 * @param p2 Second parser
 * @return Result token type: p1's result type.
 */
HParser *h_butnot(const HParser *p1, const HParser *p2);
HParser *h_butnot__m(HAllocator *mm__, const HParser *p1, const HParser *p2);

/**
 * @brief Given two parsers, p1 and p2, this parser succeeds in the following cases:
 * - if p1 succeeds and p2 fails
 * - if both succeed but p2's result is shorter than p1's
 *
 * @param p1 First parser
 * @param p2 Second parser
 * @return Result token type: p1's result type.
 */
HParser *h_difference(const HParser *p1, const HParser *p2);
HParser *h_difference__m(HAllocator *mm__, const HParser *p1, const HParser *p2);

/**
 * @brief Given two parsers, p1 and p2, this parser succeeds if *either* p1 or p2 succeed, but not
 * if they both do.
 *
 * @param p1 First parser
 * @param p2 Second parser
 * @return Result token type: The type of the result of whichever parser succeeded.
 */
HParser *h_xor(const HParser *p1, const HParser *p2);
HParser *h_xor__m(HAllocator *mm__, const HParser *p1, const HParser *p2);

/**
 * @brief Given a parser, p, this parser succeeds for zero or more repetitions of p.
 * @param p Parser to repeat
 * @return Result token type: TT_SEQUENCE
 */
HParser *h_many(const HParser *p);
HParser *h_many__m(HAllocator *mm__, const HParser *p);

/**
 * @brief Given a parser, p, this parser succeeds for one or more repetitions of p.
 * @param p Parser to repeat
 * @return Result token type: TT_SEQUENCE
 */
HParser *h_many1(const HParser *p);
HParser *h_many1__m(HAllocator *mm__, const HParser *p);

/**
 * @brief Given a parser, p, this parser succeeds for exactly N repetitions of p.
 * @param p Parser to repeat
 * @param n Number of repetitions
 * @return Result token type: TT_SEQUENCE
 */
HParser *h_repeat_n(const HParser *p, const size_t n);
HParser *h_repeat_n__m(HAllocator *mm__, const HParser *p, const size_t n);

/**
 * @brief Given a parser, p, this parser succeeds with the value p parsed or with an empty result.
 * @param p Parser to apply optionally
 * @return Result token type: If p succeeded, the type of its result; if not, TT_NONE.
 */
HParser *h_optional(const HParser *p);
HParser *h_optional__m(HAllocator *mm__, const HParser *p);

/**
 * @brief Given a parser, p, this parser succeeds if p succeeds, but doesn't include p's result in
 * the result.
 *
 * @param p Parser to ignore
 * @return Result token type: None. The HParseResult exists but its AST is NULL.
 */
HParser *h_ignore(const HParser *p);
HParser *h_ignore__m(HAllocator *mm__, const HParser *p);

/**
 * @brief Given a parser, p, and a parser for a separator, sep, this parser matches a (possibly
 * empty) list of things that p can parse, separated by sep. For example, if p is
 * repeat1(range('0','9')) and sep is ch(','), sepBy(p, sep) will match a comma-separated list of
 * integers.
 *
 * @param p Parser for list elements
 * @param sep Parser for separator
 * @return Result token type: TT_SEQUENCE
 */
HParser *h_sepBy(const HParser *p, const HParser *sep);
HParser *h_sepBy__m(HAllocator *mm__, const HParser *p, const HParser *sep);

/**
 * @brief Given a parser, p, and a parser for a separator, sep, this parser matches a list of things
 * that p can parse, separated by sep. Unlike sepBy, this ensures that the result has at least one
 * element. For example, if p is repeat1(range('0','9')) and sep is ch(','), sepBy1(p, sep) will
 * match a comma-separated list of integers.
 *
 * @param p Parser for list elements
 * @param sep Parser for separator
 * @return Result token type: TT_SEQUENCE
 */
HParser *h_sepBy1(const HParser *p, const HParser *sep);
HParser *h_sepBy1__m(HAllocator *mm__, const HParser *p, const HParser *sep);

/**
 * @brief This parser always returns a zero length match, i.e., empty string.
 * @return Result token type: None. The HParseResult exists but its AST is NULL.
 */
HParser *h_epsilon_p(void);
HParser *h_epsilon_p__m(HAllocator *mm__);

/**
 * @brief This parser applies its first argument to read an unsigned integer value, then applies its
 * second argument that many times. length should parse an unsigned integer value; this is checked
 * at runtime. Specifically, the token_type of the returned token must be TT_UINT. In future we
 * might relax this to include TT_USER but don't count on it.
 *
 * @param length Parser to read length
 * @param value Parser to apply length times
 * @return Result token type: TT_SEQUENCE
 */
HParser *h_length_value(const HParser *length, const HParser *value);
HParser *h_length_value__m(HAllocator *mm__, const HParser *length, const HParser *value);

/**
 * @brief This parser attaches a predicate function, which returns true or false, to a parser. The
 * function is evaluated over the parser's result.
 *
 * The parse only succeeds if the attribute function returns true.
 *
 * attr_bool will check whether p's result exists and whether p's result AST exists; you do not need
 * to check for this in your predicate function.
 *
 * @param p Parser to wrap
 * @param pred Predicate function
 * @param user_data Context for predicate
 * @return Result token type: p's result type if pred succeeded, NULL otherwise.
 */
HParser *h_attr_bool(const HParser *p, HPredicate pred, void *user_data);
HParser *h_attr_bool__m(HAllocator *mm__, const HParser *p, HPredicate pred, void *user_data);

/**
 * @brief The 'and' parser asserts that a conditional syntax is satisfied, but doesn't consume that
 * conditional syntax. This is useful for lookahead. As an example:
 *
 * Suppose you already have a parser, hex_p, that parses numbers in hexadecimal format (including
 * the leading '0x'). Then `sequence(and(token((const uint8_t*)"0x", 2)), hex_p)` checks to see
 * whether there is a leading "0x", *does not* consume the "0x", and then applies hex_p to parse the
 * hex-formatted number.
 *
 * 'and' succeeds if p succeeds, and fails if p fails.
 *
 * @param p Parser to apply
 * @return Result token type: None. The HParseResult exists but its AST is NULL.
 */
HParser *h_and(const HParser *p);
HParser *h_and__m(HAllocator *mm__, const HParser *p);

/**
 * @brief The 'not' parser asserts that a conditional syntax is *not* satisfied, but doesn't consume
 * that conditional syntax. As a somewhat contrived example:
 *
 * Since 'choice' applies its arguments in order, the following parser: `sequence(ch('a'),
 * choice(ch('+'), token((const uint8_t*)"++"), NULL), ch('b'), NULL)` will not parse "a++b",
 * because once choice() has succeeded, it will not backtrack and try other alternatives if a later
 * parser in the sequence fails. Instead, you can force the use of the second alternative by turning
 * the ch('+') alternative into a sequence with not:
 *   `sequence(ch('a'), choice(sequence(ch('+'), not(ch('+')), NULL), token((const uint8_t*)"++")),
 * ch('b'), NULL)` If the input string is "a+b", the first alternative is applied; if the input
 * string is "a++b", the second alternative is applied.
 *
 * @param p Parser to apply
 * @return Result token type: None. The HParseResult exists but its AST is NULL.
 */
HParser *h_not(const HParser *p);
HParser *h_not__m(HAllocator *mm__, const HParser *p);

/**
 * @brief Create a parser that just calls out to another, as yet unknown, parser.
 * Note that the inner parser gets bound later, with bind_indirect. This can be used to create
 * recursive parsers.
 *
 * @return Result token type: the type of whatever parser is bound to it with bind_indirect().
 */
HParser *h_indirect(void);
HParser *h_indirect__m(HAllocator *mm__);

/**
 * @brief Set the inner parser of an indirect. See comments on indirect for details.
 * @param indirect Parser created with h_indirect()
 * @param inner Parser to bind to indirect
 */
void h_bind_indirect(HParser *indirect, const HParser *inner);
void h_bind_indirect__m(HAllocator *mm__, HParser *indirect, const HParser *inner);

/**
 * @brief This parser runs its argument parser with the given endianness setting.
 *
 * The value of 'endianness' should be a bit-wise or of the constants
 * BYTE_BIG_ENDIAN/BYTE_LITTLE_ENDIAN and BIT_BIG_ENDIAN/BIT_LITTLE_ENDIAN.
 *
 * @param endianness Endianness setting
 * @param p Parser to run with given endianness
 * @return Result token type: p's result type.
 */
HParser *h_with_endianness(char endianness, const HParser *p);
HParser *h_with_endianness__m(HAllocator *mm__, char endianness, const HParser *p);

/**
 * @brief The 'h_put_value' combinator stashes the result of the parser it wraps in a symbol table
 * in the parse state, so that non- local actions and predicates can access this value.
 *
 * Attempting to use h_put with a name that was already in the symbol table will return NULL (and
 * parse failure)
 *
 * @param p Parser whose result to stash
 * @param name Name to stash the result under (must be unique)
 * @return Result token type: p's token type if name was not already in the symbol table.
 */
HParser *h_put_value(const HParser *p, const char *name);
HParser *h_put_value__m(HAllocator *mm__, const HParser *p, const char *name);

/**
 * @brief The 'h_get_value' combinator retrieves a named HParseResult that was previously stashed in
 * the parse state.
 *
 * @param name Name to retrieve
 * @return Result token type: whatever the stashed HParseResult is, if present. If absent, NULL (and
 * thus parse failure).
 */
HParser *h_get_value(const char *name);
HParser *h_get_value__m(HAllocator *mm__, const char *name);

/**
 * @brief The 'h_free_value' combinator retrieves a named HParseResult that was previously stashed
 * in the parse state and deletes it from the symbol table
 *
 * @param name Name to retrieve and delete
 * @return Result token type: whatever the stashed HParseResult is, if present. If absent, NULL (and
 * thus parse failure).
 */
HParser *h_free_value(const char *name);
HParser *h_free_value__m(HAllocator *mm__, const char *name);

/**
 * @brief Monadic bind for HParsers, i.e.:
 * Sequencing where later parsers may depend on the result(s) of earlier ones.
 *
 * Run p and call the result x. Then run k(env,x).  Fail if p fails or if k(env,x) fails or if
 * k(env,x) is NULL.
 *
 * @param p Parser to run first
 * @param k Continuation function
 * @param env Context to pass to k
 * @return Result: the result of k(x,env).
 */
HParser *h_bind(const HParser *p, HContinuation k, void *env);
HParser *h_bind__m(HAllocator *mm__, const HParser *p, HContinuation k, void *env);

/**
 * @brief This parser skips 'n' bits of input.
 * @param n Number of bits to skip
 * @return Result: None. The HParseResult exists but its AST is NULL.
 */
HParser *h_skip(size_t n);
HParser *h_skip__m(HAllocator *mm__, size_t n);

/**
 * @brief The HParser equivalent of fseek(), 'h_seek' modifies the parser's input position.  Note
 * that contrary to 'fseek', offsets are in bits, not bytes. The 'whence' argument uses the same
 * values and semantics: SEEK_SET, SEEK_CUR, SEEK_END.
 *
 * Fails if the new input position would be negative or past the end of input.
 *
 * @param offset Offset in bits
 * @param whence SEEK_SET, SEEK_CUR, or SEEK_END
 * @return Result: TT_UINT. The new input position.
 */
HParser *h_seek(ssize_t offset, int whence);
HParser *h_seek__m(HAllocator *mm__, ssize_t offset, int whence);

/**
 * @brief Report the current position in bits. Consumes no input.
 * @return Result: TT_UINT. The current input position.
 */
HParser *h_tell(void);
HParser *h_tell__m(HAllocator *mm__);

/**
 * @brief Free the memory allocated to an HParseResult when it is no longer needed.
 * @param result Result to free
 */
void h_parse_result_free(HParseResult *result);
void h_parse_result_free__m(HAllocator *mm__, HParseResult *result);

/** @} */

/** @defgroup debugging_aids Debugging Aids
 * @{
 */

/**
 * @brief Format token into a compact unambiguous form. Useful for parser test cases.
 * @param tok Token to format
 * @return Allocated string (caller must free)
 */
char *h_write_result_unamb(const HParsedToken *tok);

/**
 * @brief Format token to the given output stream. Indent starting at [indent] spaces, with [delta]
 * spaces between levels.
 *
 * Note: This function does not print a trailing newline. It also does not print any spaces to
 * indent the initial line of output. This makes it suitable for recursive use in the condensed
 * output of larger structures.
 *
 * @param stream Output stream
 * @param tok Token to format
 * @param indent Initial indentation level (in spaces)
 * @param delta Number of spaces between indentation levels
 */
void h_pprint(FILE *stream, const HParsedToken *tok, int indent, int delta);

/**
 * @brief Format token to the given output. Print a trailing newline.
 *
 * This function assumes an initial indentation of 0 and uses 2 spaces between indentation levels.
 * It is equivalent to 'h_pprint(stream, tok, 0, 2)' followed by 'fputc('\n', stream)' and is
 * provided for convenience.
 *
 * @param stream Output stream
 * @param tok Token to format
 */
void h_pprintln(FILE *stream, const HParsedToken *tok);

/** @} */

/** @defgroup compilation Parser Compilation
 * @{
 */

/**
 * @brief Build parse tables for the given parser backend. See the documentation for the parser
 * backend in question for information about the [params] parameter, or just pass in NULL for the
 * defaults.
 *
 * Returns a nonzero value on error; 0 otherwise. Common return codes include:
 *
 *  -1: parser uses a combinator that is incompatible with the chosen backend.
 *  -2: parser could not be compiled with the chosen parameters.
 *  >0: unexpected internal errors.
 *
 * Consult each backend for details.
 *
 * @param parser Parser to compile
 * @param be_with_params Backend configuration
 * @return 0 on success, error code otherwise
 */
int h_compile_for_backend_with_params(HParser *parser, HParserBackendWithParams *be_with_params);
int h_compile_for_backend_with_params__m(HAllocator *mm__, HParser *parser,
                                         HParserBackendWithParams *be_with_params);

/**
 * @brief Build parse tables for the given parser backend.
 *
 * Returns a nonzero value on error; 0 otherwise. Common return codes include:
 *
 *  -1: parser uses a combinator that is incompatible with the chosen backend.
 *  -2: parser could not be compiled with the chosen parameters.
 *  >0: unexpected internal errors.
 *
 * @param parser Parser to compile
 * @param backend Backend to use
 * @param params Parameters for the backend, or NULL for defaults
 * @return 0 on success, error code otherwise
 */
int h_compile(HParser *parser, HParserBackend backend, const void *params);
int h_compile__m(HAllocator *mm__, HParser *parser, HParserBackend backend, const void *params);

/** @} */

/**
 * @defgroup bitwriter Bit Writer Functions
 * @{
 */

/**
 * @brief Create a new bit writer.
 *
 * Allocates and initializes an `HBitWriter` that accumulates bits into an internal growable buffer.
 * The writer starts empty, with both byte and bit endianness set to big-endian. Use with
 * `h_bit_writer_put` to append bits and `h_bit_writer_get_buffer` to access the resulting byte
 * buffer.
 *
 * @param mm__ Allocator to use, or NULL for default
 * @return New bit writer, or NULL on allocation failure
 */
HBitWriter *h_bit_writer_new(HAllocator *mm__);

/**
 * @brief Append bits to the writer.
 *
 * Appends the lowest `nbits` bits from `data` to the internal buffer, growing it as needed. The
 * order in which bits are taken from `data` and packed into bytes is determined by the writer's
 * endianness flags.
 *
 * - Byte order: if byte-endian is big, bits are taken from most-significant to least-significant;
 * if little, bits are taken from least-significant upward.
 * - Bit order within a byte: if bit-endian is big, bits are appended toward the most-significant
 * positions; if little, toward the least-significant.
 *
 * Preconditions: `w` is non-NULL and `nbits > 0` (up to 64).
 *
 * @param w Bit writer
 * @param data Bits to append (in the lowest `nbits` bits)
 * @param nbits Number of bits to append (1 to 64)
 */
void h_bit_writer_put(HBitWriter *w, uint64_t data, size_t nbits);

/**
 * @brief Get a pointer to the internal byte buffer.
 *
 * Returns a pointer to the writer's internal buffer and stores its current length in bytes in
 * `len`. The buffer is owned by `w` and remains valid until more data is written or
 * `h_bit_writer_free` is called. The writer must be at a whole-byte boundary (i.e., no partial byte
 * pending).
 *
 * Must not free `w` until you're done with the result. `len` is in bytes.
 *
 * @param w Bit writer
 * @param len Output parameter for buffer length in bytes
 * @return Pointer to internal buffer, or NULL if `w` is NULL or not at a whole-byte boundary
 */
const uint8_t *h_bit_writer_get_buffer(HBitWriter *w, size_t *len);

/**
 * @brief Free the bit writer and its buffer.
 *
 * Releases all memory associated with `w`. Any buffer pointer obtained via
 * `h_bit_writer_get_buffer` becomes invalid after this call.
 *
 * @param w Bit writer to free
 */
void h_bit_writer_free(HBitWriter *w);

// General-purpose actions for use with h_action
// XXX to be consolidated with glue.h when merged upstream
HParsedToken *h_act_first(const HParseResult *p, void *userdata);
HParsedToken *h_act_second(const HParseResult *p, void *userdata);
HParsedToken *h_act_last(const HParseResult *p, void *userdata);
HParsedToken *h_act_flatten(const HParseResult *p, void *userdata);
HParsedToken *h_act_ignore(const HParseResult *p, void *userdata);

/** @defgroup benchmark_functions Benchmark functions
 * @{
 */

HBenchmarkResults *h_benchmark(HParser *parser, HParserTestcase *testcases);
HBenchmarkResults *h_benchmark__m(HAllocator *mm__, HParser *parser, HParserTestcase *testcases);

void h_benchmark_report(FILE *stream, HBenchmarkResults *results);
// void h_benchmark_dump_optimized_code(FILE* stream, HBenchmarkResults* results);

/** @} */

/** @defgroup result_buf_printers Result Buffer Printers
 * @{
 */

struct result_buf;

bool h_append_buf(struct result_buf *buf, const char *input, int len);
bool h_append_buf_c(struct result_buf *buf, char v);
bool h_append_buf_formatted(struct result_buf *buf, char *format, ...);

/** @} */

/** @defgroup token_type_registry Token Type Registry
 * @{
 */

/* Allocate a new, unused (as far as this function knows) token type. */
HTokenType h_allocate_token_type(const char *name);

/* Allocate a new token type with an unambiguous print function. */
HTokenType h_allocate_token_new(const char *name,
                                void (*unamb_sub)(const HParsedToken *tok, struct result_buf *buf),
                                void (*pprint)(FILE *stream, const HParsedToken *tok, int indent,
                                               int delta));

/* Get the token type associated with name. Returns -1 if name is unknown */
HTokenType h_get_token_type_number(const char *name);

/* Get the name associated with token_type. Returns NULL if the token type is unknown */
const char *h_get_token_type_name(HTokenType token_type);
/** @} */

/** Make an allocator that draws from the given memory area. */
HAllocator *h_sloballoc(void *mem, size_t size);

#ifdef __cplusplus
}
#endif

#endif // #ifndef HAMMER_HAMMER__H
