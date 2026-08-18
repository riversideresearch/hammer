/* Copyright (c) 2026 Riverside Research */
/* Arena allocator for Hammer.
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
#include "internal.h"

#include <setjmp.h>
#include <stdint.h>
#include <string.h>

typedef union HArenaAlignment_ {
    void *ptr;
    void (*fn)(void);
    long l;
    long long ll;
    double d;
    long double ld;
} HArenaAlignment;

#define ARENA_ALIGNMENT sizeof(HArenaAlignment)

struct arena_link {
    struct arena_link *next;
    uint8_t *block; // pointer to the data block
    size_t free;
    size_t used;
};

struct HArena_ {
    struct arena_link *head;
    struct HAllocator_ *mm__;
    /* does mm__ zero blocks for us? */
    bool malloc_zeros;
    size_t block_size;
    size_t used;
    size_t wasted;
#ifdef DETAILED_ARENA_STATS
    size_t mm_malloc_count, mm_malloc_bytes;
    size_t memset_count, memset_bytes;
    size_t arena_malloc_count, arena_malloc_bytes;
    size_t arena_su_malloc_count, arena_su_malloc_bytes;
    size_t arena_si_malloc_count, arena_si_malloc_bytes;
    size_t arena_lu_malloc_count, arena_lu_malloc_bytes;
    size_t arena_li_malloc_count, arena_li_malloc_bytes;
#endif

    jmp_buf *except;
};

static void *h_arena_malloc_raw(HArena *arena, size_t size, bool need_zero);

static size_t align_arena_size(size_t size) {
    size_t rem = size % ARENA_ALIGNMENT;
    if (rem == 0)
        return size;
    if (size > SIZE_MAX - (ARENA_ALIGNMENT - rem))
        h_platform_errx(1, "memory allocation failed (%zuB requested)\n", size);
    return size + (ARENA_ALIGNMENT - rem);
}

void *h_alloc(HAllocator *mm__, size_t size) {
    if (!mm__) {
        h_platform_errx(1, "memory manager doesn't exist\n");
        return NULL;
    }
    void *p = mm__->alloc(mm__, size);
    if (!p)
        h_platform_errx(1, "memory allocation failed (%zuB requested)\n", size);
    return p;
}

void *h_realloc(HAllocator *mm__, void *ptr, size_t size) {
    void *p = mm__->realloc(mm__, ptr, size);
    if (!p)
        h_platform_errx(1, "memory reallocation failed (%zuB requested)\n", size);
    return p;
}

/* Wrapper functions that dispatch to a vtable taking an environment pointer. */
static void *h_allocator_v_alloc(HAllocator *allocator, size_t size) {
    if (allocator->vt && allocator->vt->alloc)
        return allocator->vt->alloc(allocator->env, size);
    return NULL;
}

static void *h_allocator_v_realloc(HAllocator *allocator, void *ptr, size_t size) {
    if (allocator->vt && allocator->vt->realloc)
        return allocator->vt->realloc(allocator->env, ptr, size);
    return NULL;
}

static void h_allocator_v_free(HAllocator *allocator, void *ptr) {
    if (allocator->vt && allocator->vt->free)
        allocator->vt->free(allocator->env, ptr);
}

void h_allocator_wrap(HAllocator *out, HAllocatorVtable *vt, void *env) {
    out->alloc = h_allocator_v_alloc;
    out->realloc = h_allocator_v_realloc;
    out->free = h_allocator_v_free;
    out->vt = vt;
    out->env = env;
}

HArena *h_new_arena(HAllocator *mm__, size_t block_size) {
    if (block_size == 0)
        block_size = 4096;
    block_size = align_arena_size(block_size);
    struct HArena_ *ret = h_new(struct HArena_, 1);
    assert(ret != NULL);
    struct arena_link *link = (struct arena_link *)h_alloc(mm__, sizeof(struct arena_link));
    if (link == NULL) {
        return NULL;
    }
    uint8_t *block = (uint8_t *)h_alloc(mm__, block_size);
    if (block == NULL) {
        h_free(link);
        return NULL;
    }
    link->block = block;
    link->free = block_size;
    link->used = 0;
    link->next = NULL;
    ret->head = link;
    ret->block_size = block_size;
    ret->used = 0;
    ret->mm__ = mm__;
#ifdef DETAILED_ARENA_STATS
    ret->mm_malloc_count = 3;
    ret->mm_malloc_bytes = sizeof(*ret) + sizeof(struct arena_link) + block_size;
    ret->memset_count = 0;
    ret->memset_bytes = 0;
    ret->arena_malloc_count = ret->arena_malloc_bytes = 0;
    ret->arena_su_malloc_count = ret->arena_su_malloc_bytes = 0;
    ret->arena_si_malloc_count = ret->arena_si_malloc_bytes = 0;
    ret->arena_lu_malloc_count = ret->arena_lu_malloc_bytes = 0;
    ret->arena_li_malloc_count = ret->arena_li_malloc_bytes = 0;
#endif
    /* XXX provide a mechanism to indicate mm__ returns zeroed blocks */
    ret->malloc_zeros = false;
    ret->wasted = sizeof(struct arena_link) + sizeof(struct HArena_) + block_size;
    ret->except = NULL;
    return ret;
}

void h_arena_set_except(HArena *arena, jmp_buf *except) { arena->except = except; }

static void *alloc_block(HArena *arena, size_t size) {
    void *block = arena->mm__->alloc(arena->mm__, size);
    if (!block) {
        if (arena->except)
            longjmp(*arena->except, 1);
        h_platform_errx(1, "memory allocation failed (%uB requested)\n", (unsigned int)size);
    }
    return block;
}

void *h_arena_malloc_noinit(HArena *arena, size_t size) {
    return h_arena_malloc_raw(arena, size, false);
}

void *h_arena_malloc(HArena *arena, size_t size) { return h_arena_malloc_raw(arena, size, true); }

static void *h_arena_malloc_raw(HArena *arena, size_t size, bool need_zero) {
    struct arena_link *link = NULL;
    void *ret = NULL;
    size_t alloc_size = align_arena_size(size);

    if (alloc_size <= arena->head->free) {
        /* fast path.. */
        ret = arena->head->block + arena->head->used;
        arena->used += alloc_size;
        arena->wasted -= alloc_size;
        arena->head->used += alloc_size;
        arena->head->free -= alloc_size;

#ifdef DETAILED_ARENA_STATS
        ++(arena->arena_malloc_count);
        arena->arena_malloc_bytes += size;
        if (need_zero) {
            ++(arena->arena_si_malloc_count);
            arena->arena_si_malloc_bytes += size;
        } else {
            ++(arena->arena_su_malloc_count);
            arena->arena_su_malloc_bytes += size;
        }
#endif
    } else if (alloc_size > arena->block_size) {
        /*
         * Keep dedicated blocks behind the current head so they do not displace
         * a partially filled standard block. Attach the link before allocating
         * its block so h_delete_arena() owns it if allocation longjmps.
         */
        link = (struct arena_link *)alloc_block(arena, sizeof(*link));
        if (!link)
            return NULL;

        link->block = NULL;
        link->next = arena->head->next;
        arena->head->next = link;

        uint8_t *block = (uint8_t *)alloc_block(arena, alloc_size);
        link->block = block;
        link->used = alloc_size;
        link->free = 0;
        arena->used += alloc_size;
        arena->wasted += sizeof(struct arena_link);
        ret = link->block;

#ifdef DETAILED_ARENA_STATS
        ++(arena->arena_malloc_count);
        arena->arena_malloc_bytes += size;
        if (need_zero) {
            ++(arena->arena_li_malloc_count);
            arena->arena_li_malloc_bytes += size;
        } else {
            ++(arena->arena_lu_malloc_count);
            arena->arena_lu_malloc_bytes += size;
        }
#endif
    } else {
        /* we just need to allocate an ordinary new block. */
        link = (struct arena_link *)alloc_block(arena, sizeof(*link));
        if (!link)
            return NULL;

        link->block = NULL;
        link->next = arena->head;
        arena->head = link;

        uint8_t *block = (uint8_t *)alloc_block(arena, arena->block_size);
        link->block = block;
        link->free = arena->block_size - alloc_size;
        link->used = alloc_size;
#ifdef DETAILED_ARENA_STATS
        arena->mm_malloc_count += 2; /* link and block allocations */
        arena->mm_malloc_bytes += sizeof(struct arena_link) + arena->block_size;
#endif
        arena->used += alloc_size;
        arena->wasted += sizeof(struct arena_link) + arena->block_size - alloc_size;
        ret = link->block;

#ifdef DETAILED_ARENA_STATS
        ++(arena->arena_malloc_count);
        arena->arena_malloc_bytes += size;
        if (need_zero) {
            ++(arena->arena_si_malloc_count);
            arena->arena_si_malloc_bytes += size;
        } else {
            ++(arena->arena_su_malloc_count);
            arena->arena_su_malloc_bytes += size;
        }
#endif
    }

    /*
     * Zeroize if necessary
     */
    if (need_zero && !(arena->malloc_zeros)) {
        memset(ret, 0, size);
#ifdef DETAILED_ARENA_STATS
        ++(arena->memset_count);
        arena->memset_bytes += size;
#endif
    }

    return ret;
}

void h_delete_arena(HArena *arena) {
    HAllocator *mm__ = arena->mm__;
    struct arena_link *link = arena->head;
    while (link) {
        struct arena_link *next = link->next;
        // Free the block and link separately
        h_free(link->block);
        h_free(link);
        link = next;
    }
    h_free(arena);
}

void h_allocator_stats(HArena *arena, HArenaStats *stats) {
    stats->used = arena->used;
    stats->wasted = arena->wasted;
#ifdef DETAILED_ARENA_STATS
    stats->mm_malloc_count = arena->mm_malloc_count;
    stats->mm_malloc_bytes = arena->mm_malloc_bytes;
    stats->memset_count = arena->memset_count;
    stats->memset_bytes = arena->memset_bytes;
    stats->arena_malloc_count = arena->arena_malloc_count;
    stats->arena_malloc_bytes = arena->arena_malloc_bytes;
    stats->arena_su_malloc_count = arena->arena_su_malloc_count;
    stats->arena_su_malloc_bytes = arena->arena_su_malloc_bytes;
    stats->arena_si_malloc_count = arena->arena_si_malloc_count;
    stats->arena_si_malloc_bytes = arena->arena_si_malloc_bytes;
    stats->arena_lu_malloc_count = arena->arena_lu_malloc_count;
    stats->arena_lu_malloc_bytes = arena->arena_lu_malloc_bytes;
    stats->arena_li_malloc_count = arena->arena_li_malloc_count;
    stats->arena_li_malloc_bytes = arena->arena_li_malloc_bytes;
#endif
}

void *h_arena_realloc(HArena *arena, void *ptr, size_t n) {
    struct arena_link *link;
    void *ret;
    size_t ncopy;

    // XXX this is really wasteful, but maybe better than nothing?
    //
    // first, we walk the blocks to find our ptr. since we don't know how large
    // the original allocation was, we must always make a new one and copy as
    // much data from the old block as there could have been.

    for (link = arena->head; link; link = link->next) {
        if (ptr >= (void *)link->block && ptr <= (void *)(link->block + link->used))
            break; /* found it */
    }
    assert(link != NULL);

    ncopy = link->used - (size_t)((uint8_t *)ptr - link->block);
    if (n < ncopy)
        ncopy = n;

    ret = h_arena_malloc_noinit(arena, n);
    assert(ret != NULL);
    memcpy(ret, ptr, ncopy);
    // free arena?

    return ret;
}
