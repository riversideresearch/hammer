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

static HDesugarContext *desugar_context_root(HDesugarContext *ctx) {
    while (ctx->group_parent)
        ctx = ctx->group_parent;
    return ctx;
}

static void desugar_context_merge(HDesugarContext *host, HDesugarContext *child) {
    HDesugarContext *host_root = desugar_context_root(host);
    HDesugarContext *child_root = desugar_context_root(child);

    if (host_root == child_root)
        return;

    host_root->refs += child_root->refs;
    host_root->group_tail->group_next = child_root;
    host_root->group_tail = child_root->group_tail;
    child_root->group_parent = host_root;
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
    ctx->group_parent = NULL;
    ctx->group_next = NULL;
    ctx->group_tail = ctx;
    return ctx;
}

static void desugar_context_attach(HDesugarContext *ctx, HParser *parser) {
    if (parser->desugar_ctx) {
        desugar_context_merge(ctx, parser->desugar_ctx);
        return;
    }

    parser->desugar_ctx = ctx;
    ++desugar_context_root(ctx)->refs;
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

    HDesugarContext *root = desugar_context_root(ctx);
    assert(root->refs > 0);
    if (--root->refs == 0) {
        for (HDesugarContext *member = root; member;) {
            HDesugarContext *next = member->group_next;
            h_delete_arena(member->arena);
            member->owner_mm__->free(member->owner_mm__, member);
            member = next;
        }
    }
}

static void desugar_stack_store(HAllocator *mm__, HCFStack *stk__, HCFChoice *choice) {
    if (stk__->count > 0) {
        h_cfstack_add_to_seq(mm__, stk__, choice);
    } else {
        assert(stk__->cap > 0);
        stk__->stack[0] = choice;
        stk__->last_completed = choice;
    }
}

HCFChoice *h_desugar(HAllocator *mm__, HCFStack *stk__, const HParser *parser) {
    HCFStack *nstk__ = stk__;
    if (parser->desugared == NULL) {
        HParser *mutable_parser = (HParser *)parser;
        HDesugarContext *ctx = desugar_context_from_allocator(mm__);
        HAllocator *cfg_mm__;
        if (stk__ != NULL && !ctx) {
            HCFChoice *choice = h_desugar(mm__, NULL, parser);
            if (choice != NULL)
                desugar_stack_store(mm__, stk__, choice);
            return choice;
        }
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
        if (mutable_parser->desugared)
            mutable_parser->desugared->parser = mutable_parser;
        if (stk__ == NULL) {
            h_cfstack_free(cfg_mm__, nstk__);
        }
    } else if (stk__ != NULL) {
        HDesugarContext *ctx = desugar_context_from_allocator(mm__);
        if (ctx && parser->desugar_ctx)
            desugar_context_merge(ctx, parser->desugar_ctx);
        desugar_stack_store(mm__, stk__, parser->desugared);
    }

    return parser->desugared;
}
