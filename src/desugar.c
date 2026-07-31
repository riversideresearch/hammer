/* Copyright (c) 2026 Riverside Research */
#include "hammer.h"
#include "internal.h"

static void *desugar_arena_alloc(void *env, size_t size) {
    HDesugarContext *ctx = env;
    return h_arena_malloc_noinit(ctx->arena, size);
}

static void *desugar_arena_realloc(void *env, void *ptr, size_t size) {
    HDesugarContext *ctx = env;
    return h_arena_realloc(ctx->arena, ptr, size);
}

static void desugar_arena_free(void *env, void *ptr) {
    (void)env;
    (void)ptr;
}

static HAllocatorVtable desugar_arena_vtable = {
    .alloc = desugar_arena_alloc,
    .realloc = desugar_arena_realloc,
    .free = desugar_arena_free,
};

static HDesugarContext *desugar_context_from_allocator(HAllocator *mm__) {
    if (mm__ && mm__->vt == &desugar_arena_vtable)
        return mm__->env;
    return NULL;
}

static HDesugarContext *desugar_context_new(HAllocator *owner_mm__) {
    HDesugarContext *ctx = h_alloc(owner_mm__, sizeof(*ctx));
    if (!ctx)
        return NULL;

    ctx->arena = h_new_arena(owner_mm__, 0);
    if (!ctx->arena) {
        owner_mm__->free(owner_mm__, ctx);
        return NULL;
    }

    h_allocator_wrap(&ctx->allocator, &desugar_arena_vtable, ctx);
    ctx->owner_mm__ = owner_mm__;
    ctx->refs = 0;
    return ctx;
}

static void desugar_context_attach(HDesugarContext *ctx, HParser *parser) {
    if (parser->desugar_ctx)
        return;

    parser->desugar_ctx = ctx;
    ++ctx->refs;
}

HAllocator *h_desugar_context_allocator(HParser *parser) {
    HDesugarContext *ctx = parser->desugar_ctx;
    if (!ctx) {
        ctx = desugar_context_new(parser->owner_mm__);
        if (!ctx)
            return NULL;
        desugar_context_attach(ctx, parser);
    }
    return &ctx->allocator;
}

void h_desugar_context_release(HDesugarContext *ctx) {
    if (!ctx)
        return;

    assert(ctx->refs > 0);
    if (--ctx->refs == 0) {
        h_delete_arena(ctx->arena);
        ctx->owner_mm__->free(ctx->owner_mm__, ctx);
    }
}

HCFChoice *h_desugar(HAllocator *mm__, HCFStack *stk__, const HParser *parser) {
    HCFStack *nstk__ = stk__;
    if (parser->desugared == NULL) {
        HParser *mutable_parser = (HParser *)parser;
        HDesugarContext *ctx = desugar_context_from_allocator(mm__);
        HAllocator *cfg_mm__;

        if (ctx) {
            desugar_context_attach(ctx, mutable_parser);
            cfg_mm__ = &ctx->allocator;
        } else {
            cfg_mm__ = h_desugar_context_allocator(mutable_parser);
        }
        if (!cfg_mm__)
            return NULL;

        if (nstk__ == NULL) {
            nstk__ = h_cfstack_new(cfg_mm__);
        }
        if (nstk__->prealloc == NULL) {
            nstk__->prealloc = h_alloc(cfg_mm__, sizeof(HCFChoice));
        }
        // cast away the const to memoize
        assert(parser->vtable->desugar != NULL);
        mutable_parser->desugared = nstk__->prealloc;
        parser->vtable->desugar(cfg_mm__, nstk__, parser->env);
        if (stk__ == NULL) {
            h_cfstack_free(cfg_mm__, nstk__);
        }
    } else if (stk__ != NULL) {
        HCFS_APPEND(parser->desugared);
    }

    return parser->desugared;
}
