/* Internals for Hammer.
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

/*
 * NOTE: This is an internal header and installed for use by extensions. The
 * API is not guaranteed stable.
 */

#ifndef HAMMER_INTERNAL__H
#define HAMMER_INTERNAL__H
#include "hammer.h"
#include "platform.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* "Internal" in this case means "we're not ready to commit
 * to a public API." Many structures and routines here will be
 * useful in client programs.
 */

#ifdef NDEBUG
#define assert_message(check, message)                                                             \
    do {                                                                                           \
    } while (0)
#else
#define assert_message(check, message)                                                             \
    do {                                                                                           \
        if (!(check))                                                                              \
            h_platform_errx(1, "Assertion failed (programmer error): %s", message);                \
    } while (0)
#endif

/* Unconditional assertion for programmer errors — fires in all builds, including -DNDEBUG. */
#define HAMMER_ASSERT(cond)                                                                        \
    ((void)((cond) ||                                                                              \
            (fprintf(stderr, "Hammer assertion failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__),  \
             abort(), 0)))

#define HAMMER_FN_IMPL_NOARGS(rtype_t, name)                                                       \
    rtype_t name(void) { return name##__m(system_allocator); }                                     \
    rtype_t name##__m(HAllocator *mm__)
// Functions with arguments are difficult to forward cleanly. Alas, we will need to forward them
// manually.

#define h_new(type, count) ((type *)(h_alloc(mm__, sizeof(type) * (size_t)(count))))
#define h_free(addr) (mm__->free(mm__, (addr)))

#ifndef __cplusplus
#define false 0
#define true 1
#endif
#ifdef __cplusplus
extern "C" {
#endif

// This is going to be generally useful.
static inline void h_generic_free(HAllocator *allocator, void *ptr) {
    allocator->free(allocator, ptr);
}

extern HAllocator system_allocator;
typedef struct HCFStack_ HCFStack;

#define DEFAULT_ENDIANNESS (BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN)

typedef struct HInputStream_ {
    // This should be considered to be a really big value type.
    const uint8_t *input;
    size_t pos; // position of this chunk in a multi-chunk stream
    size_t index;
    size_t length;
    char bit_offset;
    char margin; // The number of bits on the end that is being read
                 // towards that should be ignored.
    char endianness;
    bool overrun;
    bool last_chunk;
} HInputStream;

typedef struct HSlistNode_ {
    void *elem;
    struct HSlistNode_ *next;
} HSlistNode;

typedef struct HSlist_ {
    HSlistNode *head;
    struct HArena_ *arena;
} HSlist;

// {{{ HSArray

typedef struct HSArrayNode_ {
    size_t elem;
    size_t index;
    void *content;
} HSArrayNode;

typedef struct HSArray_ {
    // Sparse array
    // Element n is valid iff arr->nodes[n].index < arr.used && arr.nodes[arr.nodes[n].index].elem
    // == n
    HSArrayNode *nodes; // content for node at index n is stored at position n.
    size_t capacity;
    size_t used;
    HAllocator *mm__;
} HSArray;

HSArray *h_sarray_new(HAllocator *mm__, size_t size);
void h_sarray_free(HSArray *arr);
static inline bool h_sarray_isset(HSArray *arr, size_t n) {
    assert(n < arr->capacity);
    return (arr->nodes[n].index < arr->used && arr->nodes[arr->nodes[n].index].elem == n);
}
static inline void *h_sarray_get(HSArray *arr, size_t n) {
    assert(n < arr->capacity);
    if (h_sarray_isset(arr, n))
        return arr->nodes[n].content;
    return NULL;
}

static inline void *h_sarray_set(HSArray *arr, size_t n, void *val) {
    assert(n < arr->capacity);
    arr->nodes[n].content = val;
    if (h_sarray_isset(arr, n))
        return val;
    arr->nodes[arr->used].elem = n;
    arr->nodes[n].index = arr->used++;
    return val;
}

static inline void h_sarray_clear(HSArray *arr) { arr->used = 0; }

#define H__APPEND2(a, b) a##b
#define H__APPEND(a, b) H__APPEND2(a, b)
#define H__INTVAR(pfx) H__APPEND(intvar__##pfx##__, __COUNTER__)

#define H_SARRAY_FOREACH_KV_(var, idx, arr, intvar)                                                \
    for (size_t intvar = 0, idx = (var = (arr)->nodes[(arr)->nodes[intvar].elem].content,          \
                                  (arr)->nodes[intvar].elem);                                      \
         intvar < (arr)->used; idx = (arr)->nodes[intvar].elem,                                    \
                var = (arr)->nodes[(arr)->nodes[intvar].elem].content, intvar = intvar + 1)

#define H_SARRAY_FOREACH_KV(var, index, arr) H_SARRAY_FOREACH_KV_(var, index, arr, H__INTVAR(idx))
#define H_SARRAY_FOREACH_V(var, arr) H_SARRAY_FOREACH_KV_(var, H__INTVAR(elem), arr, H__INTVAR(idx))
#define H_SARRAY_FOREACH_K(index, arr)                                                             \
    H_SARRAY_FOREACH_KV_(H__INTVAR(val), index, arr, H__INTVAR(idx))

// }}}

typedef unsigned int *HCharset;

static inline HCharset new_charset(HAllocator *mm__) {
    HCharset cs = h_new(unsigned int, 256 / (sizeof(unsigned int) * 8));
    memset(cs, 0, 32); // 32 bytes = 256 bits
    return cs;
}

static inline int charset_isset(HCharset cs, uint8_t pos) {
    return !!(cs[pos / (sizeof(*cs) * 8)] & (1 << (pos % (sizeof(*cs) * 8))));
}

static inline void charset_set(HCharset cs, uint8_t pos, int val) {
    cs[pos / (sizeof(*cs) * 8)] =
        val ? cs[pos / (sizeof(*cs) * 8)] | (1u << (pos % (sizeof(*cs) * 8)))
            : cs[pos / (sizeof(*cs) * 8)] & ~(1u << (pos % (sizeof(*cs) * 8)));
}

typedef unsigned int HHashValue;
typedef HHashValue (*HHashFunc)(const void *key);
typedef bool (*HEqualFunc)(const void *key1, const void *key2);

typedef struct HHashTableEntry_ {
    struct HHashTableEntry_ *next;
    const void *key;
    void *value;
    HHashValue hashval;
} HHashTableEntry;

typedef struct HHashTable_ {
    HHashTableEntry *contents;
    HHashFunc hashFunc;
    HEqualFunc equalFunc;
    size_t capacity;
    size_t used;
    HArena *arena;
} HHashTable;

/* The state of the parser.
 *
 * Members:
 *   cache - a hash table describing the state of the parse, including partial HParseResult's. It's
 * a hash table from HParserCacheKey to HParserCacheValue. input_stream - the input stream at this
 * state. arena - the arena that has been allocated for the parse this state is in. lr_stack - a
 * stack of HLeftRec's, used in Warth's recursion recursion_heads - table of recursion heads. Keys
 * are HParserCacheKey's with only an HInputStream (parser can be NULL), values are
 * HRecursionHead's. symbol_table - stack of tables of values that have been stashed in the context
 * of this parse.
 *
 */

struct HParseState_ {
    HHashTable *cache;
    HInputStream input_stream;
    HArena *arena;
    HSlist *lr_stack;
    HHashTable *recursion_heads;
    HSlist *symbol_table; // its contents are HHashTables
    struct HActionPlan_ *action_plan;
};

struct HSuspendedParser_ {
    HAllocator *mm__;
    const HParser *parser;
    void *backend_state;
    bool done;

    // input stream state
    size_t pos;
    uint8_t bit_offset;
    uint8_t endianness;
};

struct HParserBackendVTable_ {
    int (*compile)(HAllocator *mm__, HParser *parser, const void *params);
    HParseResult *(*parse)(HAllocator *mm__, const HParser *parser, HInputStream *stream);
    void (*free)(HParser *parser);

    void (*parse_start)(HSuspendedParser *s);
    // parse_start should allocate s->backend_state.
    bool (*parse_chunk)(HSuspendedParser *s, HInputStream *input);
    // if parser is done, return true. otherwise:
    // parse_chunk MUST consume all input, integrating it into s->backend_state.
    // parse_chunk will not be called again after it reports done.
    HParseResult *(*parse_finish)(HSuspendedParser *s);
    // parse_finish must free s->backend_state.
    // parse_finish will not be called before parse_chunk reports done.

    /* The backend knows how to free its params */
    void (*free_params)(HAllocator *mm__, void *p);
    /*
     * ..and how to copy them
     *
     * Since the backend params need not actually be an allocated object,
     * (and in fact no current backends use this, although it is permissible),
     * but might be some numeric constant cast to void * which
     * copy_params() should just pass through, we can't use returning NULL
     * to signal allocation failure.  Hence, passing the result out in a
     * void ** and returning a status code (0 indicates success).
     */
    int (*copy_params)(HAllocator *mm__, void **out, void *in);

    /* Description/name handling */
    const char *backend_short_name;
    const char *backend_description;
    char *(*get_description_with_params)(HAllocator *mm__, HParserBackend be, void *params);
    char *(*get_short_name_with_params)(HAllocator *mm__, HParserBackend be, void *params);

    /* extract params from the input string */
    int (*extract_params)(HParserBackendWithParams *be_with_params,
                          backend_with_params_t *be_with_params_t);
};

/* The (location, parser) tuple used to key the cache.
 */

typedef struct HParserCacheKey_ {
    HInputStream input_pos;
    const HParser *parser;
} HParserCacheKey;

typedef struct HActionPlan_ HActionPlan;

/* A value in the cache is either of value Left or Right (this is a
 * holdover from Scala, which used Either here). Left corresponds to
 * HLeftRec, which is for left recursion; Right corresponds to
 * HParseResult.
 */

typedef enum HParserCacheValueType_ { PC_LEFT, PC_RIGHT } HParserCacheValueType;

/* A recursion head.
 *
 * Members:
 *   head_parser - the parse rule that started this recursion
 *   involved_set - A list of rules (HParser's) involved in the recursion
 *   eval_set -
 */
typedef struct HRecursionHead_ {
    const HParser *head_parser;
    HSlist *involved_set;
    HSlist *eval_set;
} HRecursionHead;

/* A left recursion.
 *
 * Members:
 *   seed - the HResult yielded by rule
 *   rule - the HParser that produces seed
 *   head - the
 */
typedef struct HLeftRec_ {
    HParseResult *seed;
    HActionPlan *seed_plan;
    const HParser *rule;
    HRecursionHead *head;
} HLeftRec;

/* Tagged union for values in the cache: either HLeftRec's (Left) or
 * HParseResult's (Right).
 * Includes the position (input_stream) to advance to after using this value.
 */
typedef struct HParserCacheValue_t {
    HParserCacheValueType value_type;
    union {
        HLeftRec *left;
        HParseResult *right;
    } value;
    HInputStream input_stream;
    HActionPlan *action_plan;
} HParserCacheValue;

// This file provides the logical inverse of bitreader.c
struct HBitWriter_ {
    uint8_t *buf;
    HAllocator *mm__;
    size_t index;
    size_t capacity;
    char bit_offset; // unlike in bit_reader, this is always the number
                     // of used bits in the current byte. i.e., 0 always
                     // means that 8 bits are available for use.
    char flags;
    char error;
};

// }}}

// Backends {{{
extern HParserBackendVTable h__missing_backend_vtable;
extern HParserBackendVTable h__packrat_backend_vtable;
extern HParserBackendVTable h__regex_backend_vtable;
extern HParserBackendVTable h__llk_backend_vtable;
extern HParserBackendVTable h__lalr_backend_vtable;
extern HParserBackendVTable h__glr_backend_vtable;
// }}}

// TODO(thequux): Set symbol visibility for these functions so that they aren't exported.

/*
 * Helper functions for backend with params names and descriptions for
 * backends which take no params.
 */

char *h_get_description_with_no_params(HAllocator *mm__, HParserBackend be, void *params);
char *h_get_short_name_with_no_params(HAllocator *mm__, HParserBackend be, void *params);

int64_t h_read_bits(HInputStream *state, int count, char signed_p);
void h_skip_bits(HInputStream *state, size_t count);
void h_seek_bits(HInputStream *state, size_t pos);
static inline size_t h_input_stream_pos(HInputStream *state) {
    assert(state->pos <= SIZE_MAX - state->index);
    assert(state->pos + state->index < SIZE_MAX / 8);
    return (state->pos + state->index) * 8 + (size_t)(state->bit_offset) + (size_t)(state->margin);
}
static inline size_t h_input_stream_length(HInputStream *state) {
    assert(state->pos <= SIZE_MAX - state->length);
    assert(state->pos + state->length <= SIZE_MAX / 8);
    return (state->pos + state->length) * 8;
}
// need to decide if we want to make this public.
HParseResult *h_do_parse(const HParser *parser, HParseState *state);
void put_cached(HParseState *ps, const HParser *p, HParseResult *cached);

HActionPlan *h_action_plan_concat(HArena *arena, HActionPlan *left, HActionPlan *right);
HActionPlan *h_action_plan_stash(HArena *arena, const HParseResult *result,
                                 HParsedToken *placeholder, HAction action, void *user_data,
                                 HActionCollection *collection);
HActionPlan *h_action_plan_apply(HArena *arena, HActionCollection *collection, HActionPlan *child);
bool h_action_plan_execute(HArena *arena, HActionPlan *plan);

/*
 * Inline this for benefit of h_new_parser() below, then make
 * the API h_get_default_backend() call it.
 */
static inline HParserBackend h_get_default_backend__int(void) { return PB_PACKRAT; }

static inline HParserBackendVTable *h_get_default_backend_vtable__int(void) {
    return &h__packrat_backend_vtable;
}

static inline HParserBackendVTable *h_get_missing_backend_vtable__int(void) {
    return &h__missing_backend_vtable;
}

/* copy_params for backends where the parameter is not actually a pointer */

int h_copy_numeric_param(HAllocator *mm__, void **out, void *in);

static inline HParser *h_new_parser_with_free(HAllocator *mm__, const HParserVtable *vt, void *env,
                                              HParserEnvFree free_env) {
    HParser *p = h_new(HParser, 1);
    memset(p, 0, sizeof(*p));

    p->vtable = vt;
    p->env = env;
    p->free_env = free_env;
    p->backend = h_get_default_backend__int();
    p->backend_vtable = h_get_default_backend_vtable__int();
    p->owner_mm__ = mm__;

    return p;
}

static inline void h_free_env(HAllocator *allocator, void *environment) {
    allocator->free(allocator, environment);
}

static inline HParser *h_new_parser(HAllocator *mm__, const HParserVtable *vt, void *env) {
    return h_new_parser_with_free(mm__, vt, env, h_free_env);
}

HCFChoice *h_desugar(HAllocator *mm__, HCFStack *stk__, const HParser *parser);
HAllocator *h_desugar_context_allocator(HParser *parser);
void h_desugar_context_release(HDesugarContext *ctx);

/*
 * Correct Usage:
 *   - These data structures allocate all internal storage from the given HArena.
 *   - They do NOT free individual nodes, buffers, or entries; memory is reclaimed
 *     only when the arena itself is destroyed via h_delete_arena().
 *   - They are safe and leak-free ONLY when the arena has a well-defined,
 *     short lifetime (e.g., per-parse). Using them in long-lived arenas will
 *     cause memory to grow without being reclaimed.
 */
HCountedArray *h_carray_new_sized(HArena *arena, size_t size);
HCountedArray *h_carray_new(HArena *arena);
void h_carray_append(HCountedArray *array, void *item);

HSlist *h_slist_new(HArena *arena);
HSlist *h_slist_copy(HSlist *slist);
void *h_slist_pop(HSlist *slist);
void *h_slist_drop(HSlist *slist);
static inline void *h_slist_top(HSlist *sl) { return sl->head->elem; }
void h_slist_push(HSlist *slist, void *item);
bool h_slist_find(HSlist *slist, const void *item);
HSlist *h_slist_remove_all(HSlist *slist, const void *item);
void h_slist_free(HSlist *slist);
static inline bool h_slist_empty(const HSlist *sl) { return (sl->head == NULL); }

HHashTable *h_hashtable_new(HArena *arena, HEqualFunc equalFunc, HHashFunc hashFunc);
void *h_hashtable_get_precomp(const HHashTable *ht, const void *key, HHashValue hashval);
void *h_hashtable_get(const HHashTable *ht, const void *key);
void h_hashtable_put_precomp(HHashTable *ht, const void *key, void *value, HHashValue hashval);
void h_hashtable_put(HHashTable *ht, const void *key, void *value);
void h_hashtable_update(HHashTable *dst, const HHashTable *src);
void h_hashtable_merge(void *(*combine)(void *v1, const void *v2), HHashTable *dst,
                       const HHashTable *src);
int h_hashtable_present(const HHashTable *ht, const void *key);
void h_hashtable_del(HHashTable *ht, const void *key);
void h_hashtable_free(HHashTable *ht);
static inline bool h_hashtable_empty(const HHashTable *ht) { return (ht->used == 0); }
bool h_hashtable_equal(const HHashTable *a, const HHashTable *b, HEqualFunc value_eq);

typedef HHashTable HHashSet;
#define h_hashset_new(a, eq, hash) h_hashtable_new(a, eq, hash)
#define h_hashset_put(ht, el) h_hashtable_put(ht, el, NULL)
#define h_hashset_put_all(a, b) h_hashtable_update(a, b)
#define h_hashset_present(ht, el) h_hashtable_present(ht, el)
#define h_hashset_empty(ht) h_hashtable_empty(ht)
#define h_hashset_del(ht, el) h_hashtable_del(ht, el)
#define h_hashset_free(ht) h_hashtable_free(ht)
bool h_hashset_equal(const HHashSet *a, const HHashSet *b);

bool h_eq_ptr(const void *p, const void *q);
HHashValue h_hash_ptr(const void *p);
uint32_t h_djbhash(const uint8_t *buf, size_t len);

void h_symbol_put(HParseState *state, const char *key, void *value);
void *h_symbol_get(HParseState *state, const char *key);
void *h_symbol_free(HParseState *state, const char *key);

typedef struct HCFSequence_ HCFSequence;

typedef HParsedToken *(*HCFPlanAction)(const HParseResult *result, void *user_data,
                                       HActionPlan **plan);

struct HCFChoice_ {
    enum HCFChoiceType { HCF_END, HCF_CHOICE, HCF_CHARSET, HCF_CHAR } type;
    union {
        HCharset charset;
        HCFSequence **seq;
        uint8_t chr;
    } data;
    HAction reshape; // take CFG parse tree to HParsedToken of expected form.
                     // to execute before action and pred are applied.
    HAction action;
    HCFPlanAction plan_action;
    HPredicate pred;
    HParser *parser; // if this is a parser, then this is the parser that produced it.
    void *env;
    void *user_data;
    size_t dispatch_opcode;
};

struct HCFSequence_ {
    HCFChoice **items; // last one is NULL
};

// HCFStack - used for desugaring
struct HCFStack_ {
    HCFChoice **stack;
    int count;
    int cap;
    HCFChoice *last_completed; // Last completed choice.
                               // XXX is last_completed still needed?
    HCFChoice *prealloc;       // If not NULL, will be used for the outermost choice.
    char error;
};

// HCFS macros for desugaring
#ifndef UNUSED
#define UNUSED H_GCC_ATTRIBUTE((unused))
#endif

static inline HCFChoice *h_cfstack_new_choice_raw(HAllocator *mm__, HCFStack *stk__) UNUSED;
static inline void h_cfstack_begin_choice(HAllocator *mm__, HCFStack *stk__) UNUSED;
static HCFStack *h_cfstack_new(HAllocator *mm__) UNUSED;
static HCFStack *h_cfstack_new(HAllocator *mm__) {
    HCFStack *stack = h_new(HCFStack, 1);
    stack->count = 0;
    stack->cap = 4;
    stack->stack = h_new(HCFChoice *, stack->cap);
    stack->last_completed = NULL;
    stack->prealloc = NULL;
    stack->error = 0;
    return stack;
}

static void h_cfstack_free(HAllocator *mm__, HCFStack *stk__) UNUSED;
static void h_cfstack_free(HAllocator *mm__, HCFStack *stk__) {
    h_free(stk__->prealloc);
    h_free(stk__->stack);
    h_free(stk__);
}

static inline void h_cfstack_add_to_seq(HAllocator *mm__, HCFStack *stk__, HCFChoice *item) UNUSED;
static inline void h_cfstack_add_to_seq(HAllocator *mm__, HCFStack *stk__, HCFChoice *item) {
    HCFChoice *cur_top = stk__->stack[stk__->count - 1];
    assert(cur_top->type == HCF_CHOICE);
    assert(cur_top->data.seq[0] != NULL); // There must be at least one sequence...
    if (stk__->error)
        return;
    for (size_t i = 0;; i++) {
        if (cur_top->data.seq[i + 1] == NULL) {
            assert(cur_top->data.seq[i]->items != NULL);
            for (size_t j = 0;; j++) {
                if (j > SIZE_MAX / sizeof(HCFChoice *) - 2) {
                    h_platform_errx(1, "CF stack allocation size overflow");
                }
                if (cur_top->data.seq[i]->items[j] == NULL) {
                    size_t new_count = j + 2;
                    HCFChoice **new_items = h_realloc(mm__, cur_top->data.seq[i]->items,
                                                      sizeof(*new_items) * new_count);

                    cur_top->data.seq[i]->items = new_items;
                    new_items[j] = item;
                    new_items[j + 1] = NULL;
                    assert(!stk__->error);
                    stk__->last_completed = item;
                    return;
                }
            }
        }
    }
}

static inline HCFChoice *h_cfstack_new_choice_raw(HAllocator *mm__, HCFStack *stk__) {
    HCFChoice *ret = stk__->prealloc ? stk__->prealloc : h_new(HCFChoice, 1);
    stk__->prealloc = NULL;

    ret->reshape = NULL;
    ret->action = NULL;
    ret->plan_action = NULL;
    ret->pred = NULL;
    ret->parser = NULL;
    ret->env = NULL;
    ret->user_data = NULL;
    ret->dispatch_opcode = 0;
    ret->type = ~0; // invalid type
    // Add it to the current sequence...
    if (stk__->count > 0) {
        h_cfstack_add_to_seq(mm__, stk__, ret);
    }

    return ret;
}

static inline void h_cfstack_add_charset(HAllocator *mm__, HCFStack *stk__, HCharset charset) {
    HCFChoice *ni = h_cfstack_new_choice_raw(mm__, stk__);
    ni->type = HCF_CHARSET;
    ni->data.charset = charset;
    stk__->last_completed = ni;
}

static inline void h_cfstack_add_char(HAllocator *mm__, HCFStack *stk__, uint8_t chr) {
    HCFChoice *ni = h_cfstack_new_choice_raw(mm__, stk__);
    ni->type = HCF_CHAR;
    ni->data.chr = chr;
    stk__->last_completed = ni;
}

static inline void h_cfstack_add_end(HAllocator *mm__, HCFStack *stk__) {
    HCFChoice *ni = h_cfstack_new_choice_raw(mm__, stk__);
    ni->type = HCF_END;
    stk__->last_completed = ni;
}

static inline void h_cfstack_begin_choice(HAllocator *mm__, HCFStack *stk__) {
    HCFChoice *choice = h_cfstack_new_choice_raw(mm__, stk__);
    choice->type = HCF_CHOICE;
    choice->data.seq = h_new(HCFSequence *, 1);
    choice->data.seq[0] = NULL;

    if (stk__->count + 1 > stk__->cap) {
        assert(stk__->cap > 0);
        stk__->cap *= 2;
        stk__->stack = h_realloc(mm__, stk__->stack, (size_t)stk__->cap * sizeof(*stk__->stack));
    }
    assert(stk__->cap >= 1 && !stk__->error);
    stk__->stack[stk__->count++] = choice;
}

static inline void h_cfstack_begin_seq(HAllocator *mm__, HCFStack *stk__) {
    HCFChoice *top = stk__->stack[stk__->count - 1];
    for (size_t i = 0;; i++) {
        if (top->data.seq[i] == NULL) {
            top->data.seq = h_realloc(mm__, top->data.seq, sizeof(HCFSequence *) * (i + 2));
            HCFSequence *seq = top->data.seq[i] = h_new(HCFSequence, 1);
            top->data.seq[i + 1] = NULL;
            seq->items = h_new(HCFChoice *, 1);
            seq->items[0] = NULL;
            return;
        }
    }
}

static inline void h_cfstack_end_seq(HAllocator *mm__, HCFStack *stk__) UNUSED;
static inline void h_cfstack_end_seq(HAllocator *mm__, HCFStack *stk__) {
    // do nothing. You should call this anyway.
}

static inline void h_cfstack_end_choice(HAllocator *mm__, HCFStack *stk__) UNUSED;
static inline void h_cfstack_end_choice(HAllocator *mm__, HCFStack *stk__) {
    assert(stk__->count > 0);
    stk__->last_completed = stk__->stack[stk__->count - 1];
    stk__->count--;
}

#define HCFS_APPEND(choice) h_cfstack_add_to_seq(mm__, stk__, (choice))
#define HCFS_DESUGAR(parser) h_desugar(mm__, stk__, parser)
#define HCFS_ADD_CHARSET(charset) h_cfstack_add_charset(mm__, stk__, (charset))
#define HCFS_ADD_CHAR(chr) h_cfstack_add_char(mm__, stk__, (chr))
#define HCFS_ADD_END() h_cfstack_add_end(mm__, stk__)
// The semicolons on BEGIN macros are intentional; pretend that they
// are control structures.
#define HCFS_BEGIN_CHOICE() h_cfstack_begin_choice(mm__, stk__);
#define HCFS_BEGIN_SEQ() h_cfstack_begin_seq(mm__, stk__);
#define HCFS_END_CHOICE() h_cfstack_end_choice(mm__, stk__)
#define HCFS_END_SEQ() h_cfstack_end_seq(mm__, stk__)
#define HCFS_THIS_CHOICE (stk__->stack[stk__->count - 1])
#define HCFS_SET_DISPATCH_OPCODE(op) (HCFS_THIS_CHOICE->dispatch_opcode = (op))

struct HParserVtable_ {
    HParseResult *(*parse)(void *env, HParseState *state);
    bool (*isValidRegular)(void *env);
    bool (*isValidCF)(void *env);
    bool (*compile_to_rvm)(HRVMProg *prog, void *env);
    void (*desugar)(HAllocator *mm__, HCFStack *stk__, void *env);
    bool higher; // false if primitive
    /* Optional Packrat diagnostic metadata. Implementations return the byte
     * offset of the failure and add the bytes or EOF accepted there. */
    size_t (*trace_expectations)(void *env, size_t consumed, bool overrun, bool expected[256],
                                 bool *expected_eof);
};

// {{{ Token type registry internal

typedef struct HTTEntry_ {
    const char *name;
    HTokenType value;
    void (*unamb_sub)(const HParsedToken *tok, struct result_buf *buf);
    void (*pprint)(FILE *stream, const HParsedToken *tok, int indent, int delta);
} HTTEntry;

const HTTEntry *h_get_token_type_entry(HTokenType token_type);

// }}}

bool h_false(void *);
bool h_true(void *);
bool h_not_regular(HRVMProg *, void *);
// internal checks to verify parser type for error reporting
bool h_is_nothing_parser(const HParser *parser);
bool h_is_xor_parser(const HParser *parser);
bool h_is_difference_parser(const HParser *parser);
bool h_is_butnot_parser(const HParser *parser);
bool h_is_get_value_parser(const HParser *parser); // either h_get_value or h_free_value parser



#if 0
#include <stdlib.h>
#define h_arena_malloc(a, s) malloc(s)
#endif
#ifdef __cplusplus
} // extern "C"
#endif
#endif // #ifndef HAMMER_INTERNAL__H
