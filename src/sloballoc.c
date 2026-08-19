/* Copyright (c) 2026 Riverside Research */
// first-fit SLOB (simple list of blocks) allocator

#include "sloballoc.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

typedef union SLOBAlignment_ {
    void *ptr;
    void (*fn)(void);
    long l;
    long long ll;
    double d;
    long double ld;
} SLOBAlignment;

#define SLOB_ALIGNMENT sizeof(SLOBAlignment)

struct alloc {
    size_t size;
    size_t padding;
    uint8_t data[];
};

struct block {
    size_t size; // read: struct alloc
    struct block *next;
};

struct slob {
    size_t size;
    struct block *head;
    uint8_t data[];
};

static size_t slob_align_up(size_t size) {
    size_t rem = size % SLOB_ALIGNMENT;
    if (rem == 0)
        return size;
    if (size > SIZE_MAX - (SLOB_ALIGNMENT - rem))
        return 0;
    return size + (SLOB_ALIGNMENT - rem);
}

static size_t slob_align_down(size_t size) { return size - (size % SLOB_ALIGNMENT); }

static void *slob_align_region(void *mem, size_t *size) {
    uintptr_t addr = (uintptr_t)mem;
    size_t rem = addr % SLOB_ALIGNMENT;
    size_t padding = rem == 0 ? 0 : SLOB_ALIGNMENT - rem;
    if (padding > *size)
        return NULL;
    *size -= padding;
    return (uint8_t *)mem + padding;
}

SLOB *slobinit(void *mem, size_t size) {
    SLOB *slob = slob_align_region(mem, &size);

    if (!slob || size < sizeof(SLOB) + sizeof(struct block))
        return NULL;
    if (size >= UINTPTR_MAX - (uintptr_t)slob)
        return NULL;

    slob->size = slob_align_down(size - sizeof(SLOB));
    slob->head = (struct block *)((uint8_t *)slob + sizeof(SLOB));
    slob->head->size = slob->size - sizeof(struct alloc);
    slob->head->next = NULL;

    return slob;
}

void *sloballoc(SLOB *slob, size_t size) {
    struct block *b, **p;
    size_t fitblock, remblock;

    // size must be enough to extend to a struct block in case of free
    fitblock = sizeof(struct block) > sizeof(struct alloc)
                   ? sizeof(struct block) - sizeof(struct alloc)
                   : SLOB_ALIGNMENT;
    if (size < fitblock)
        size = fitblock;
    size = slob_align_up(size);
    if (size == 0)
        return NULL;

    // need this much to fit another block in the remaining space
    remblock = size + sizeof(struct block);
    if (remblock < size)
        return NULL; // overflow

    // scan list for the first block of sufficient size
    for (p = &slob->head; (b = *p); p = &b->next) {
        if (b->size >= remblock) {
            // cut from the end of the block
            b->size -= sizeof(struct alloc) + size;
            struct alloc *a = (struct alloc *)(((struct alloc *)b)->data + b->size);
            a->size = size;
            return a->data;
        } else if (b->size >= size) {
            // when a block fills, it converts directly to a struct alloc
            *p = b->next; // unlink
            return ((struct alloc *)b)->data;
        }
    }

    return NULL;
}

void *slobrealloc(SLOB *slob, void *a_, size_t size) {
    // Null input handling
    if (slob == NULL) {
        return NULL;
    }
    if (a_ == NULL) {
        return sloballoc(slob, size);
    }
    if (size == 0) {
        slobfree(slob, a_);
        return NULL;
    }
    size = slob_align_up(size);
    if (size == 0)
        return NULL;
    struct alloc *a = (struct alloc *)((uint8_t *)a_ - sizeof(struct alloc));
    assert((uint8_t *)a >= slob->data);
    assert(a->data + a->size <= slob->data + slob->size);
    size_t old_size = a->size;

    // Try in-place shrink
    if (size < old_size) {
        size_t remaining = old_size - size;
        if (remaining >= sizeof(struct block) + sizeof(struct alloc)) {
            uint8_t *tail = (uint8_t *)a + sizeof(struct alloc) + size;
            struct block *b = (struct block *)tail;
            b->size = remaining - sizeof(struct alloc);
            void *b_payload = tail + sizeof(struct alloc);
            if (b->size > 0)
                memset(b_payload, 0, b->size); // zeroize payload
            slobfree(slob, b_payload);
            a->size = size;
            return a_;
        }
    }

    void *new = sloballoc(slob, size);
    if (!new)
        return NULL;

    memcpy(new, a_, MIN(old_size, size));
    memset(a_, 0, old_size); // zeroize
    slobfree(slob, a_);
    return new;
}

void slobfree(SLOB *slob, void *a_) {
    struct alloc *a = (struct alloc *)((uint8_t *)a_ - sizeof(struct alloc));
    struct block *b, **p, *left = NULL, *right = NULL, **rightp = NULL;

    // sanity check: a lies inside slob
    assert((uint8_t *)a >= slob->data);
    assert(a->data + a->size <= slob->data + slob->size);

    // scan list for blocks adjacent to a
    for (p = &slob->head; (b = *p); p = &b->next) {
        if ((uint8_t *)a == ((struct alloc *)b)->data + b->size) {
            assert(!left);
            left = b;
        }
        if (a->data + a->size == (uint8_t *)b) {
            assert(!right);
            right = b;
            rightp = p;
        }

        if (left && right) {
            // extend left and unlink right
            left->size += sizeof(*a) + a->size + sizeof(struct alloc) + right->size;
            *rightp = right->next;
            return;
        }
    }

    if (left) {
        // extend left to absorb a
        left->size += sizeof(*a) + a->size;
    } else if (right) {
        // shift and extend right to absorb a
        right->size += sizeof(*a) + a->size;
        *rightp = (struct block *)a;
        **rightp = *right;
    } else {
        // spawn new block over a
        struct block *b = (struct block *)a;
        b->next = slob->head;
        slob->head = b;
    }
}

int slobcheck(SLOB *slob) {
    // invariants:
    // 1. memory area is divided seamlessly and exactly into n blocks
    // 2. every block is large enough to hold a 'struct block'.
    // 3. free list has at most n elements.
    // 4. every element of the free list is one of the valid blocks.
    // 5. every block appears at most once in the free list.

    uint8_t *p;
    size_t nblocks = 0, nfree = 0;

#define FORBLOCKS                                                                                  \
    for (p = slob->data; p != slob->data + slob->size;                                             \
         p += sizeof(struct alloc) + ((struct alloc *)p)->size)

    // 1. memory area is divided seamlessly and exactly into n blocks
    FORBLOCKS {
        if (p < slob->data)
            return 1;
        if (p > slob->data + slob->size)
            return 2;
        nblocks++;

        struct alloc *a = (struct alloc *)p;
        if (a->size > UINTPTR_MAX - (uintptr_t)p)
            return 3;

        // 2. every block is large enough to hold a 'struct block'.
        if (a->size + sizeof(struct alloc) < sizeof(struct block))
            return 4;
    }

    // 3. free list has at most n elements.
    for (struct block *b = slob->head; b; b = b->next) {
        nfree++;
        if (nfree > nblocks)
            return 5;

        // 4. every element of the free list is one of the valid blocks.
        FORBLOCKS
        if (p == (uint8_t *)b)
            break;
        if (!p)
            return 6;
    }

    // 5. every block appears at most once in the free list.
    FORBLOCKS {
        size_t count = 0;
        for (struct block *b = slob->head; b; b = b->next)
            if (p == (uint8_t *)b)
                count++;
        if (count > 1)
            return 7;
    }

#undef FORBLOCKS
    return 0;
}

// hammer interface

#include "hammer.h"

static size_t h_slob_offset(void) { return slob_align_up(sizeof(HAllocator)); }

static SLOB *h_slob_get(HAllocator *mm) { return (SLOB *)((uint8_t *)mm + h_slob_offset()); }

static void *h_slob_alloc(HAllocator *mm, size_t size) {
    SLOB *slob = h_slob_get(mm);
    return sloballoc(slob, size);
}

static void h_slob_free(HAllocator *mm, void *p) {
    SLOB *slob = h_slob_get(mm);
    slobfree(slob, p);
}

static void *h_slob_realloc(HAllocator *mm, void *p, size_t size) {
    SLOB *slob = h_slob_get(mm);

    return slobrealloc(slob, p, size);
}

HAllocator *h_sloballoc(void *mem, size_t size) {
    HAllocator *mm = slob_align_region(mem, &size);
    size_t slob_offset = h_slob_offset();
    if (!mm || size < slob_offset)
        return NULL;

    SLOB *slob = slobinit((uint8_t *)mm + slob_offset, size - slob_offset);
    if (!slob)
        return NULL;
    assert(slob == h_slob_get(mm));

    mm->alloc = h_slob_alloc;
    mm->realloc = h_slob_realloc;
    mm->free = h_slob_free;

    return mm;
}
