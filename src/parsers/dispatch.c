/* Copyright (c) 2026 Riverside Research */
#include "parser_internal.h"

#include <limits.h>
#include <stdarg.h>
#include <stdint.h>

#if defined(__STDC_VERSION__) &&                                                                   \
    ((__STDC_VERSION__ >= 201112L && !defined(__STDC_NO_VLA__)) || (__STDC_VERSION__ >= 199901L))
#define STACK_VLA(type, name, size) type name[size]
#else
#if defined(_MSC_VER)
#include <malloc.h> // for _alloca
#define STACK_VLA(type, name, size) type *name = _alloca(size)
#else
#error "Missing VLA implementation for this compiler"
#endif
#endif

typedef struct {
    uint32_t opcode;
    HParser *parser;
    bool used;
} DispatchBucket;

typedef struct {
    HParser *discriminator;
    HParser *default_parser;
    OpcodeMap *map;
    size_t size;
} HDispatch;

// Helper functions
static size_t next_pow2(size_t n) {
    if (n == 0)
        return 1;
    // Check for overflow: if p would overflow, return SIZE_MAX to signal error
    size_t p = 1;
    while (p < n) {
        if (p > SIZE_MAX / 2) // would overflow
            return SIZE_MAX;
        p <<= 1;
    }
    return p;
}

static size_t extract_opcode(HParseResult *result) {
    if (!result || !result->ast)
        return (size_t)-1;
    size_t opcode;
    switch (result->ast->token_type) {
    case (TT_BYTES): {
        const HBytes b = result->ast->token_data.bytes;
        if (b.len == 0) {
            opcode = (size_t)-1;
            break;
        }
        size_t val = 0;
        for (size_t i = 0; i < b.len; i++) {
            if (val > (SIZE_MAX >> 8)) {
                opcode = (size_t)-1; // overflow: opcode can't be represented as type size_t
                break;
            }

            val = (val << 8) | b.token[i];
        }
        opcode = val;
        break;
    }
    case (TT_SINT):
        opcode = (size_t)(result->ast->token_data.sint);
        break;
    case (TT_UINT):
        opcode = (size_t)(result->ast->token_data.uint);
        break;
    case (TT_DOUBLE):
        opcode = (size_t)(result->ast->token_data.dbl);
        break;
    case (TT_FLOAT):
        opcode = (size_t)(result->ast->token_data.flt);
        break;
    default:
        opcode = -1;
    }
    return opcode;
}

static HParser *extract_parser(DispatchBucket *buckets, size_t bucket_count, size_t h, size_t mask,
                               size_t opcode, HParser *default_parser) {
    while (buckets[h].used) {
        if (buckets[h].opcode == opcode)
            return buckets[h].parser;
        h = (h + 1) & mask;
    }
    return default_parser;
}

static HParseResult *parse_dispatch(void *env, HParseState *state) {
    HDispatch *d = (HDispatch *)env;

    // Parse discriminator
    HParseResult *disc_result = h_do_parse(d->discriminator, state);
    if (!disc_result) {
        return NULL;
    }

    // Extract opcode value from discriminator
    size_t opcode = extract_opcode(disc_result);

    size_t bucket_count = next_pow2(d->size * 2);
    if (bucket_count == SIZE_MAX || bucket_count < d->size) {
        return NULL;
    }

    DispatchBucket *buckets = h_arena_malloc(state->arena, bucket_count * sizeof(*buckets));
    if (!buckets) {
        return NULL;
    }
    memset(buckets, 0, bucket_count * sizeof(*buckets));

    size_t mask = bucket_count - 1;
    for (size_t i = 0; i < d->size; ++i) {
        uint32_t op = d->map[i].opcode;
        HParser *p = d->map[i].parser; // preserve the original parser pointer from the map
        size_t h = (op ^ (op >> 16)) & mask;
        size_t probe_count = 0;
        while (buckets[h].used && probe_count < bucket_count) {
            h = (h + 1) & mask;
            probe_count++;
        }
        if (probe_count >= bucket_count) {
            return NULL;
        }
        buckets[h].opcode = op;
        buckets[h].parser = p;
        buckets[h].used = true;
    }

    size_t h = (opcode ^ (opcode >> 16)) & mask;
    HParser *body = extract_parser(buckets, bucket_count, h, mask, opcode, d->default_parser);
    if (!body) {
        return NULL;
    }

    HParseResult *body_result = h_do_parse(body, state);
    if (!body_result) {
        return NULL;
    }

    // Dispatch-owned result to avoid mutating cached body_result
    HCountedArray *seq = h_carray_new(state->arena);
    h_carray_append(seq, (void *)disc_result->ast);
    h_carray_append(seq, (void *)body_result->ast);
    HParsedToken *tok = a_new(HParsedToken, 1);
    tok->token_type = TT_SEQUENCE;
    tok->token_data.seq = seq;
    tok->bit_length = body_result->bit_length + disc_result->bit_length;

    HParseResult *dispatch_result = a_new(HParseResult, 1);
    if (!dispatch_result) {
        return NULL;
    }

    dispatch_result->ast = tok;
    dispatch_result->arena = state->arena;
    dispatch_result->bit_length = body_result->bit_length + disc_result->bit_length;

    return dispatch_result;
}

static bool dispatch_isValidRegular(void *env) {
    HDispatch *d = (HDispatch *)env;

    if (!d->discriminator->vtable->isValidRegular(d->discriminator->env))
        return false;
    for (size_t i = 0; i < d->size; ++i) {
        HParser *p = d->map[i].parser;
        if (!p->vtable->isValidRegular(p->env))
            return false;
    }
    return true;
}

static void desugar_dispatch(HAllocator *mm__, HCFStack *stk__, void *env) {
    HDispatch *d = (HDispatch *)env;
    HCFS_BEGIN_CHOICE() {
        for (size_t i = 0; i < d->size; ++i) {
            HParser *p = d->map[i].parser; // preserve the exact parser pointer from the map
            HCFS_BEGIN_SEQ() {
                HCFS_DESUGAR(d->discriminator);
                HCFS_DESUGAR(p);
            }
            HCFS_END_SEQ();
            HCFS_SET_DISPATCH_OPCODE(d->map[i].opcode);
        }
        HCFS_THIS_CHOICE->reshape = h_act_second;
    }
    HCFS_END_CHOICE();
}

static const HParserVtable dispatch_vt = {
    .parse = parse_dispatch,
    .isValidRegular = dispatch_isValidRegular,
    .isValidCF = h_false,
    // Dispatch involves runtime value-dependent branching (based on discriminator opcode).
    // CF grammars cannot soundly represent this constraint: which opcode maps to which
    // parser body is determined at parse time, not statically in the grammar.
    // Desugaring would create choice(seq(discriminator, parser0), seq(discriminator, parser1), ...)
    // but CF backends would see all combinations as valid, leading to unsound analysis.
    // Therefore, dispatch is not valid for CF analysis.
    .desugar = desugar_dispatch,
    .higher = true,
};

HParser *h_dispatch__s(HParser *discriminator, const OpcodeMap *map, size_t size,
                       HParser *default_parser) {
    if (map == NULL || size == 0) {
        return NULL;
    }
    HParser *ret = h_dispatch__m(&system_allocator, discriminator, map, size, default_parser);
    return ret;
}

HParser *h_dispatch__m(HAllocator *mm__, HParser *discriminator, const OpcodeMap *map, size_t size,
                       HParser *default_parser) {

    HDispatch *env = h_new(HDispatch, 1);
    if (!env) {
        return NULL;
    }
    env->map = h_new(OpcodeMap, size);
    if (!env->map) {
        return NULL;
    }
    for (size_t i = 0; i < size; ++i) {
        env->map[i].opcode = map[i].opcode;
        env->map[i].parser = map[i].parser;
    }

    env->discriminator = discriminator;
    env->default_parser = default_parser;
    env->size = size;

    return h_new_parser(mm__, &dispatch_vt, env);
}
