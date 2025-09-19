#include "parser_internal.h"

typedef struct {
    const HParser *p;
    HContinuation k;
    void *env;
} BindEnv;

// an HAllocator backed by an HArena
typedef struct {
    HAllocator allocator; // inherit  XXX is this the proper way to do it?
    HArena *arena;
} ArenaAllocator;

static void *aa_alloc(HAllocator *allocator, size_t size) {
    HArena *arena = ((ArenaAllocator *)allocator)->arena;
    return h_arena_malloc(arena, size);
}

static void *aa_realloc(HAllocator *allocator, void *ptr, size_t size) {
    HArena *arena = ((ArenaAllocator *)allocator)->arena;
    return h_arena_realloc(arena, ptr, size);
}

static void aa_free(HAllocator *allocator, void *ptr) {
    HArena *arena = ((ArenaAllocator *)allocator)->arena;
    h_arena_free(arena, ptr);
}

static HParseResult *parse_bind(void *be_, HParseState *state) {
    BindEnv *be = be_;

    HParseResult *res = h_do_parse(be->p, state);
    if (!res)
        return NULL;

    // create a wrapper arena allocator for the continuation
    ArenaAllocator aa = {{aa_alloc, aa_realloc, aa_free}, state->arena};

    HParser *kx = be->k((HAllocator *)&aa, res->ast, be->env);
    if (!kx) {
        return NULL;
    }

    HParseResult *res2 = h_do_parse(kx, state);
    if (res2)
        res2->bit_length = 0; // recalculate

    return res2;
}

static const HParserVtable bind_vt = {
    .parse = parse_bind,
    .isValidRegular = h_false,
    .isValidCF = h_false,
    .higher = true,
};

HParser *h_bind(const HParser *p, HContinuation k, void *env) {
    return h_bind__m(&system_allocator, p, k, env);
}

HParser *h_bind__m(HAllocator *mm__, const HParser *p, HContinuation k, void *env) {
    BindEnv *be = h_new(BindEnv, 1);

    be->p = p;
    be->k = k;
    be->env = env;

    return h_new_parser(mm__, &bind_vt, be);
}
