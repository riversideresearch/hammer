// first-fit SLOB (simple list of blocks) allocator

#include "sloballoc.h"
#include <stdint.h>
#include <assert.h>

struct alloc {
    size_t size;
    uint8_t data[];
};

struct block {
    struct alloc alloc;
    struct block *next;
};

struct slob {
    size_t size;
    struct block *head;
    uint8_t data[];
};


SLOB *slobinit(void *mem, size_t size)
{
    SLOB *slob = mem;

    assert(size >= sizeof(SLOB) + sizeof(struct block));
    assert(size < UINTPTR_MAX - (uintptr_t)mem);

    slob = mem;
    slob->size = size - sizeof(SLOB);
    slob->head = (struct block *)((uint8_t *)mem + sizeof(SLOB));
    slob->head->alloc.size = slob->size - sizeof(struct alloc);
    slob->head->next = NULL;

    return slob;
}

void *sloballoc(SLOB *slob, size_t size)
{
    struct block *b, **p;
    size_t fitblock, remblock;

    // size must be enough to extend to a struct block in case of free
    fitblock = sizeof(struct block) - sizeof(struct alloc);
    if(size < fitblock) size = fitblock;

    // need this much to fit another block in the remaining space
    remblock = size + sizeof(struct block);
    if(remblock < size) return NULL;    // overflow

    // scan list for the first block of sufficient size
    for(p=&slob->head; (b=*p); p=&b->next) {
        if(b->alloc.size >= remblock) {
            // cut from the end of the block
            b->alloc.size -= sizeof(struct alloc) + size;
            struct alloc *a = (struct alloc *)(b->alloc.data + b->alloc.size);
            a->size = size;
            return a->data;
        } else if(b->alloc.size >= size) {
            // when a block fills, it converts directly to a struct alloc
            *p = b->next;       // unlink
            return b->alloc.data;
        }
    }

    return NULL;
}

void slobfree(SLOB *slob, void *a_)
{
    struct alloc *a = (struct alloc *)((uint8_t *)a_ - sizeof(struct alloc));
    struct block *b, **p, *left=NULL, *right=NULL, **rightp=NULL;

    // sanity check: a lies inside slob
    assert((uint8_t *)a >= slob->data);
    assert(a->data + a->size <= slob->data + slob->size);

    // scan list for blocks adjacent to a
    for(p=&slob->head; (b=*p); p=&b->next) {
        if((uint8_t *)a == b->alloc.data + b->alloc.size) {
            assert(!left);
            left = b;
        }
        if(a->data + a->size == (uint8_t *)b) {
            assert(!right);
            right = b;
            rightp = p;
        }

        if(left && right) {
            // extend left and unlink right
            left->alloc.size += sizeof(*a) + a->size +
                                sizeof(right->alloc) + right->alloc.size;
            *rightp = right->next;
            return;
        }
    }

    if(left) {
        // extend left to absorb a
        left->alloc.size += sizeof(*a) + a->size;
    } else if(right) {
        // shift and extend right to absorb a
        right->alloc.size += sizeof(*a) + a->size;
        *rightp = (struct block *)a; **rightp = *right;
    } else {
        // spawn new block over a
        struct block *b = (struct block *)a;
        b->next = slob->head; slob->head = b;
    }
}

int slobcheck(SLOB *slob)
{
    // invariants:
    // 1. memory area is divided seamlessly and exactly into n blocks
    // 2. every block is large enough to hold a 'struct block'.
    // 3. free list has at most n elements.
    // 4. every element of the free list is one of the valid blocks.
    // 5. every block appears at most once in the free list.

    uint8_t *p;
    size_t nblocks=0, nfree=0;

    #define FORBLOCKS \
        for(p = slob->data; \
            p != slob->data + slob->size; \
            p += sizeof(struct alloc) + ((struct alloc *)p)->size)

    // 1. memory area is divided seamlessly and exactly into n blocks
    FORBLOCKS {
        if(p < slob->data)
            return 1;
        if(p > slob->data + slob->size)
            return 2;
        nblocks++;

        struct alloc *a = (struct alloc *)p;
        if(a->size > UINTPTR_MAX - (uintptr_t)p)
            return 3;

        // 2. every block is large enough to hold a 'struct block'.
        if(a->size + sizeof(struct alloc) < sizeof(struct block))
            return 4;
    }

    // 3. free list has at most n elements.
    for(struct block *b=slob->head; b; b=b->next) {
        nfree++;
        if(nfree > nblocks)
            return 5;

        // 4. every element of the free list is one of the valid blocks.
        FORBLOCKS
            if(p == (uint8_t *)b) break;
        if(!p)
            return 6;
    }

    // 5. every block appears at most once in the free list.
    FORBLOCKS {
        size_t count=0;
        for(struct block *b=slob->head; b; b=b->next)
            if(p == (uint8_t *)b) count++;
        if(count > 1)
            return 7;
    }

    #undef FORBLOCKS
    return 0;
}


// hammer interface

#include "hammer.h"

static void *h_slob_alloc(HAllocator *mm, size_t size)
{
    SLOB *slob = (SLOB *)(mm+1);
    return sloballoc(slob, size);
}

static void h_slob_free(HAllocator *mm, void *p)
{
    SLOB *slob = (SLOB *)(mm+1);
    slobfree(slob, p);
}

static void *h_slob_realloc(HAllocator *mm, void *p, size_t size)
{
    SLOB *slob = (SLOB *)(mm+1);

    assert(((void)"XXX need realloc for SLOB allocator", 0));
    return NULL;
}

HAllocator *h_sloballoc(void *mem, size_t size)
{
    if(size < sizeof(HAllocator))
        return NULL;

    HAllocator *mm = mem;
    SLOB *slob = slobinit((uint8_t *)mem + sizeof(HAllocator), size - sizeof(HAllocator));
    if(!slob)
        return NULL;
    assert(slob == (SLOB *)(mm+1));

    mm->alloc = h_slob_alloc;
    mm->realloc = h_slob_realloc;
    mm->free = h_slob_free;

    return mm;
}
