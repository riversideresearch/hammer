#ifndef HAMMER_TSEARCH__H
#define HAMMER_TSEARCH__H

#if defined(_MSC_VER)
typedef enum { preorder, postorder, endorder, leaf } VISIT;

/* find or insert datum into search tree */
void *tsearch(const void *vkey, void **vrootp,
              int (*compar)(const void *, const void *));

/* delete node with given key */
void * tdelete(const void *vkey, void **vrootp,
               int (*compar)(const void *, const void *));

/* Walk the nodes of a tree */
void twalk(const void *vroot, void (*action)(const void *, VISIT, int));

/* find a node, or return 0 */
void *tfind(const void *vkey, void * const *vrootp,
            int (*compar)(const void *, const void *));

#else
#include <search.h>
#endif

#endif /* HAMMER_TSEARCH__H */
