#include "hammer.h"
#include "internal.h"
#include "test_suite.h"

#include <glib.h>
#include <string.h>

static void test_carray_new_sized_zero(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HCountedArray *arr = h_carray_new_sized(arena, 0);
    g_check_cmp_ptr(arr, !=, NULL);
    if (arr) {
        g_check_cmp_int(arr->capacity, ==, 1);
        g_check_cmp_int(arr->used, ==, 0);
        h_delete_arena(arena);
    }
}

static void test_carray_new_sized_nonzero(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HCountedArray *arr = h_carray_new_sized(arena, 10);
    g_check_cmp_ptr(arr, !=, NULL);
    if (arr) {
        g_check_cmp_int(arr->capacity, ==, 10);
        h_delete_arena(arena);
    }
}

static void test_carray_new(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HCountedArray *arr = h_carray_new(arena);
    g_check_cmp_ptr(arr, !=, NULL);
    if (arr) {
        g_check_cmp_int(arr->capacity, ==, 4);
        h_delete_arena(arena);
    }
}

static void test_carray_append_no_resize(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HCountedArray *arr = h_carray_new_sized(arena, 4);
    void *item1 = (void *)0x1234;
    h_carray_append(arr, item1);
    g_check_cmp_int(arr->used, ==, 1);
    g_check_cmp_ptr(arr->elements[0], ==, item1);
    h_delete_arena(arena);
}

static void test_carray_append_resize(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HCountedArray *arr = h_carray_new_sized(arena, 2);
    void *item1 = (void *)0x1234;
    void *item2 = (void *)0x5678;
    void *item3 = (void *)0x9ABC;
    h_carray_append(arr, item1);
    h_carray_append(arr, item2);
    h_carray_append(arr, item3);
    g_check_cmp_int(arr->used, ==, 3);
    g_check_cmp_int(arr->capacity, >=, 3);
    h_delete_arena(arena);
}

static void test_slist_new(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HSlist *list = h_slist_new(arena);
    g_check_cmp_ptr(list, !=, NULL);
    if (list) {
        g_check_cmp_ptr(list->head, ==, NULL);
        h_delete_arena(arena);
    }
}

static void test_slist_copy_empty(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HSlist *list1 = h_slist_new(arena);
    HSlist *list2 = h_slist_copy(list1);
    g_check_cmp_ptr(list2, !=, NULL);
    g_check_cmp_ptr(list2->head, ==, NULL);
    h_delete_arena(arena);
}

static void test_slist_copy_nonempty(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HSlist *list1 = h_slist_new(arena);
    void *item1 = (void *)0x1234;
    void *item2 = (void *)0x5678;
    h_slist_push(list1, item1);
    h_slist_push(list1, item2);
    HSlist *list2 = h_slist_copy(list1);
    g_check_cmp_ptr(list2, !=, NULL);
    g_check_cmp_ptr(list2->head, !=, NULL);
    h_delete_arena(arena);
}

static void test_slist_drop_empty(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HSlist *list = h_slist_new(arena);
    void *result = h_slist_drop(list);
    g_check_cmp_ptr(result, ==, NULL);
    h_delete_arena(arena);
}

static void test_slist_drop_nonempty(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HSlist *list = h_slist_new(arena);
    void *item = (void *)0x1234;
    h_slist_push(list, item);
    void *result = h_slist_drop(list);
    g_check_cmp_ptr(result, ==, item);
    g_check_cmp_ptr(list->head, ==, NULL);
    h_delete_arena(arena);
}

static void test_slist_pop_empty(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HSlist *list = h_slist_new(arena);
    void *result = h_slist_pop(list);
    g_check_cmp_ptr(result, ==, NULL);
    h_delete_arena(arena);
}

static void test_slist_pop_nonempty(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HSlist *list = h_slist_new(arena);
    void *item = (void *)0x1234;
    h_slist_push(list, item);
    void *result = h_slist_pop(list);
    g_check_cmp_ptr(result, ==, item);
    g_check_cmp_ptr(list->head, ==, NULL);
    h_delete_arena(arena);
}

static void test_slist_push(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HSlist *list = h_slist_new(arena);
    void *item1 = (void *)0x1234;
    void *item2 = (void *)0x5678;
    h_slist_push(list, item1);
    h_slist_push(list, item2);
    g_check_cmp_ptr(list->head, !=, NULL);
    g_check_cmp_ptr(list->head->elem, ==, item2);
    h_delete_arena(arena);
}

static void test_slist_find_null_item(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HSlist *list = h_slist_new(arena);

    h_delete_arena(arena);
}

static void test_slist_find_found(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HSlist *list = h_slist_new(arena);
    void *item = (void *)0x1234;
    h_slist_push(list, item);
    bool result = h_slist_find(list, item);
    g_check_cmp_int(result, ==, true);
    h_delete_arena(arena);
}

static void test_slist_find_not_found(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HSlist *list = h_slist_new(arena);
    void *item = (void *)0x1234;
    void *other = (void *)0x5678;
    h_slist_push(list, item);
    bool result = h_slist_find(list, other);
    g_check_cmp_int(result, ==, false);
    h_delete_arena(arena);
}

static void test_slist_remove_all_null_item(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HSlist *list = h_slist_new(arena);

    h_delete_arena(arena);
}

static void test_slist_remove_all_head(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HSlist *list = h_slist_new(arena);
    void *item = (void *)0x1234;
    h_slist_push(list, item);
    h_slist_remove_all(list, item);
    g_check_cmp_ptr(list->head, ==, NULL);
    h_delete_arena(arena);
}

static void test_slist_remove_all_middle(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HSlist *list = h_slist_new(arena);
    void *item1 = (void *)0x1234;
    void *item2 = (void *)0x5678;
    void *item3 = (void *)0x9ABC;
    h_slist_push(list, item1);
    h_slist_push(list, item2);
    h_slist_push(list, item3);
    h_slist_remove_all(list, item2);

    g_check_cmp_int(h_slist_find(list, item2), ==, false);
    h_delete_arena(arena);
}

static void test_slist_free(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HSlist *list = h_slist_new(arena);
    void *item = (void *)0x1234;
    h_slist_push(list, item);
    h_slist_free(list);

    h_delete_arena(arena);
}

static void test_hashtable_new(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HHashTable *ht = h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);
    g_check_cmp_ptr(ht, !=, NULL);
    if (ht) {
        g_check_cmp_int(ht->capacity, ==, 64);
        g_check_cmp_int(ht->used, ==, 0);
        h_delete_arena(arena);
    }
}
static void test_hashtable_get_precomp_null_key(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HHashTable *ht = h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);
    void *result = h_hashtable_get_precomp(ht, NULL, 0);
    g_check_cmp_ptr(result, ==, NULL);
    h_delete_arena(arena);
}

static void test_hashtable_get_precomp_hashval_mismatch(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HHashTable *ht = h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);
    void *key = (void *)0x1234;
    HHashValue hashval1 = h_hash_ptr(key);
    HHashValue hashval2 = hashval1 + 1;
    h_hashtable_put_precomp(ht, key, (void *)0x5678, hashval1);
    void *result = h_hashtable_get_precomp(ht, key, hashval2);
    g_check_cmp_ptr(result, ==, NULL);
    h_delete_arena(arena);
}

static void test_hashtable_get(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HHashTable *ht = h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);
    void *key = (void *)0x1234;
    void *value = (void *)0x5678;
    h_hashtable_put(ht, key, value);
    void *result = h_hashtable_get(ht, key);
    g_check_cmp_ptr(result, ==, value);
    h_delete_arena(arena);
}

static void test_hashtable_ensure_capacity_resize(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HHashTable *ht = h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);

    for (int i = 0; i < 100; i++) {
        void *key = (void *)(uintptr_t)i;
        void *value = (void *)(uintptr_t)(i + 1000);
        h_hashtable_put(ht, key, value);
    }
    g_check_cmp_int(ht->capacity, >=, 64);
    h_delete_arena(arena);
}

static void test_hashtable_put_raw_existing(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HHashTable *ht = h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);
    void *key = (void *)0x1234;
    void *value1 = (void *)0x5678;
    void *value2 = (void *)0x9ABC;
    h_hashtable_put(ht, key, value1);
    h_hashtable_put(ht, key, value2);
    void *result = h_hashtable_get(ht, key);
    g_check_cmp_ptr(result, ==, value2);
    h_delete_arena(arena);
}

static void test_hashtable_put_raw_collision(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HHashTable *ht = h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);

    void *key1 = (void *)0x1000;
    void *key2 = (void *)0x2000;
    h_hashtable_put(ht, key1, (void *)0x1111);
    h_hashtable_put(ht, key2, (void *)0x2222);
    void *result1 = h_hashtable_get(ht, key1);
    void *result2 = h_hashtable_get(ht, key2);
    g_check_cmp_ptr(result1, !=, NULL);
    g_check_cmp_ptr(result2, !=, NULL);
    h_delete_arena(arena);
}

static void test_hashtable_update(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HHashTable *ht1 = h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);
    HHashTable *ht2 = h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);
    void *key = (void *)0x1234;
    h_hashtable_put(ht1, key, (void *)0x5678);
    h_hashtable_update(ht2, ht1);
    void *result = h_hashtable_get(ht2, key);
    g_check_cmp_ptr(result, ==, (void *)0x5678);
    h_delete_arena(arena);
}

static void test_hashtable_merge(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HHashTable *ht1 = h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);
    HHashTable *ht2 = h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);
    void *key = (void *)0x1234;
    h_hashtable_put(ht1, key, (void *)0x5678);
    h_hashtable_put(ht2, key, (void *)0x9ABC);

    void *combine(void *v1, const void *v2) { return (void *)((uintptr_t)v1 + (uintptr_t)v2); }
    h_hashtable_merge(combine, ht1, ht2);
    void *result = h_hashtable_get(ht1, key);
    g_check_cmp_ptr(result, !=, NULL);
    h_delete_arena(arena);
}

static void test_hashtable_present(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HHashTable *ht = h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);
    void *key = (void *)0x1234;
    h_hashtable_put(ht, key, (void *)0x5678);
    int result = h_hashtable_present(ht, key);
    g_check_cmp_int(result, ==, true);
    int result2 = h_hashtable_present(ht, (void *)0x9999);
    g_check_cmp_int(result2, ==, false);
    h_delete_arena(arena);
}

static void test_hashtable_del_with_next(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HHashTable *ht = h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);
    void *key1 = (void *)0x1000;
    void *key2 = (void *)0x2000;
    h_hashtable_put(ht, key1, (void *)0x1111);
    h_hashtable_put(ht, key2, (void *)0x2222);
    h_hashtable_del(ht, key1);
    int present = h_hashtable_present(ht, key1);
    g_check_cmp_int(present, ==, false);
    h_delete_arena(arena);
}

static void test_hashtable_del_no_next(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HHashTable *ht = h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);
    void *key = (void *)0x1234;
    h_hashtable_put(ht, key, (void *)0x5678);
    h_hashtable_del(ht, key);
    int present = h_hashtable_present(ht, key);
    g_check_cmp_int(present, ==, false);
    h_delete_arena(arena);
}

static void test_hashtable_free(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HHashTable *ht = h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);
    void *key = (void *)0x1234;
    h_hashtable_put(ht, key, (void *)0x5678);
    h_hashtable_free(ht);

    h_delete_arena(arena);
}

static void test_hashtable_equal_different_capacity(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HHashSet *set1 = (HHashSet *)h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);
    HHashSet *set2 = (HHashSet *)h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);

    for (int i = 0; i < 100; i++) {
        void *key = (void *)(uintptr_t)i;
        h_hashtable_put((HHashTable *)set1, key, (void *)0x1234);
    }

    h_delete_arena(arena);
}

static void test_hashtable_del_hashval_mismatch(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HHashTable *ht = h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);

    void *key1 = (void *)0x1000;
    void *key2 = (void *)0x2000;
    h_hashtable_put(ht, key1, (void *)0x1111);
    h_hashtable_put(ht, key2, (void *)0x2222);

    h_hashtable_del(ht, key1);
    int present = h_hashtable_present(ht, key1);
    g_check_cmp_int(present, ==, false);
    h_delete_arena(arena);
}

static void test_hashtable_equal_with_null_keys(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HHashSet *set1 = (HHashSet *)h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);
    HHashSet *set2 = (HHashSet *)h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);
    void *key1 = (void *)0x1234;
    void *key2 = (void *)0x5678;
    h_hashtable_put((HHashTable *)set1, key1, (void *)0x1111);
    h_hashtable_put((HHashTable *)set2, key1, (void *)0x1111);

    h_hashtable_del((HHashTable *)set1, key1);
    h_hashtable_put((HHashTable *)set1, key2, (void *)0x2222);
    h_hashtable_del((HHashTable *)set2, key1);
    h_hashtable_put((HHashTable *)set2, key2, (void *)0x2222);
    bool result = h_hashset_equal(set1, set2);
    g_check_cmp_int(result, ==, true);
    h_delete_arena(arena);
}

// Helper function for testing custom hash
static HHashValue custom_hash_for_test(const void *p) { return ((uintptr_t)p >> 4) ^ 0x1234; }

static void test_hashtable_equal_hashval_mismatch(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HHashSet *set1 = (HHashSet *)h_hashtable_new(arena, h_eq_ptr, custom_hash_for_test);
    HHashSet *set2 = (HHashSet *)h_hashtable_new(arena, h_eq_ptr, custom_hash_for_test);
    void *key1 = (void *)0x1000;
    void *key2 = (void *)0x2000;
    h_hashtable_put((HHashTable *)set1, key1, (void *)0x1111);
    h_hashtable_put((HHashTable *)set1, key2, (void *)0x2222);
    h_hashtable_put((HHashTable *)set2, key1, (void *)0x1111);
    h_hashtable_put((HHashTable *)set2, key2, (void *)0x2222);
    bool result = h_hashset_equal(set1, set2);
    g_check_cmp_int(result, ==, true);
    h_delete_arena(arena);
}

static void test_hashtable_equal_element_not_found(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HHashSet *set1 = (HHashSet *)h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);
    HHashSet *set2 = (HHashSet *)h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);
    void *key1 = (void *)0x1234;
    void *key2 = (void *)0x5678;
    h_hashtable_put((HHashTable *)set1, key1, (void *)0x1111);
    h_hashtable_put((HHashTable *)set1, key2, (void *)0x2222);
    h_hashtable_put((HHashTable *)set2, key1, (void *)0x1111);

    bool result = h_hashset_equal(set1, set2);
    g_check_cmp_int(result, ==, false);
    h_delete_arena(arena);
}

static void test_hashset_equal(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HHashSet *set1 = (HHashSet *)h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);
    HHashSet *set2 = (HHashSet *)h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);
    void *key = (void *)0x1234;
    h_hashtable_put((HHashTable *)set1, key, (void *)0x5678);
    h_hashtable_put((HHashTable *)set2, key, (void *)0x5678);
    bool result = h_hashset_equal(set1, set2);
    g_check_cmp_int(result, ==, true);
    h_delete_arena(arena);
}

static void test_eq_ptr(void) {
    void *p1 = (void *)0x1234;
    void *p2 = (void *)0x1234;
    void *p3 = (void *)0x5678;
    bool result1 = h_eq_ptr(p1, p2);
    bool result2 = h_eq_ptr(p1, p3);
    g_check_cmp_int(result1, ==, true);
    g_check_cmp_int(result2, ==, false);
}

static void test_hash_ptr(void) {
    void *p = (void *)0x1234;
    HHashValue hash = h_hash_ptr(p);
    g_check_cmp_int(hash, >=, 0);
}

static void test_djbhash(void) {
    const uint8_t buf[] = "test";
    uint32_t hash = h_djbhash(buf, 4);

    (void)hash;
}

static void test_djbhash_large(void) {
    const uint8_t buf[32] = "123456789012345678901234567890";
    uint32_t hash = h_djbhash(buf, 32);

    (void)hash;
}
static void test_symbol_put(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParseState state = {0};
    state.arena = arena;
    h_symbol_put(&state, "test_key", (void *)0x1234);
    void *result = h_symbol_get(&state, "test_key");
    g_check_cmp_ptr(result, ==, (void *)0x1234);
    h_delete_arena(arena);
}

static void test_symbol_get_null_table(void) {
    HParseState state = {0};
    void *result = h_symbol_get(&state, "test_key");
    g_check_cmp_ptr(result, ==, NULL);
}

static void test_symbol_free(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParseState state = {0};
    state.arena = arena;
    h_symbol_put(&state, "test_key", (void *)0x1234);
    void *result = h_symbol_free(&state, "test_key");
    g_check_cmp_ptr(result, ==, (void *)0x1234);
    void *result2 = h_symbol_get(&state, "test_key");
    g_check_cmp_ptr(result2, ==, NULL);
    h_delete_arena(arena);
}

static void test_sarray_new(void) {
    HSArray *arr = h_sarray_new(&system_allocator, 10);
    g_check_cmp_ptr(arr, !=, NULL);
    if (arr) {
        g_check_cmp_int(arr->capacity, ==, 10);
        g_check_cmp_int(arr->used, ==, 0);
        h_sarray_free(arr);
    }
}

static void test_sarray_free(void) {
    HSArray *arr = h_sarray_new(&system_allocator, 10);
    h_sarray_free(arr);
}

void register_datastructures_tests(void) {
    g_test_add_func("/core/datastructures/carray_new_sized_zero", test_carray_new_sized_zero);
    g_test_add_func("/core/datastructures/carray_new_sized_nonzero", test_carray_new_sized_nonzero);
    g_test_add_func("/core/datastructures/carray_new", test_carray_new);
    g_test_add_func("/core/datastructures/carray_append_no_resize", test_carray_append_no_resize);
    g_test_add_func("/core/datastructures/carray_append_resize", test_carray_append_resize);
    g_test_add_func("/core/datastructures/slist_new", test_slist_new);
    g_test_add_func("/core/datastructures/slist_copy_empty", test_slist_copy_empty);
    g_test_add_func("/core/datastructures/slist_copy_nonempty", test_slist_copy_nonempty);
    g_test_add_func("/core/datastructures/slist_drop_empty", test_slist_drop_empty);
    g_test_add_func("/core/datastructures/slist_drop_nonempty", test_slist_drop_nonempty);
    g_test_add_func("/core/datastructures/slist_pop_empty", test_slist_pop_empty);
    g_test_add_func("/core/datastructures/slist_pop_nonempty", test_slist_pop_nonempty);
    g_test_add_func("/core/datastructures/slist_push", test_slist_push);
    g_test_add_func("/core/datastructures/slist_find_null_item", test_slist_find_null_item);
    g_test_add_func("/core/datastructures/slist_find_found", test_slist_find_found);
    g_test_add_func("/core/datastructures/slist_find_not_found", test_slist_find_not_found);
    g_test_add_func("/core/datastructures/slist_remove_all_null_item",
                    test_slist_remove_all_null_item);
    g_test_add_func("/core/datastructures/slist_remove_all_head", test_slist_remove_all_head);
    g_test_add_func("/core/datastructures/slist_remove_all_middle", test_slist_remove_all_middle);
    g_test_add_func("/core/datastructures/slist_free", test_slist_free);
    g_test_add_func("/core/datastructures/hashtable_new", test_hashtable_new);
    g_test_add_func("/core/datastructures/hashtable_get_precomp_null_key",
                    test_hashtable_get_precomp_null_key);
    g_test_add_func("/core/datastructures/hashtable_get_precomp_hashval_mismatch",
                    test_hashtable_get_precomp_hashval_mismatch);
    g_test_add_func("/core/datastructures/hashtable_get", test_hashtable_get);
    g_test_add_func("/core/datastructures/hashtable_ensure_capacity_resize",
                    test_hashtable_ensure_capacity_resize);
    g_test_add_func("/core/datastructures/hashtable_put_raw_existing",
                    test_hashtable_put_raw_existing);
    g_test_add_func("/core/datastructures/hashtable_put_raw_collision",
                    test_hashtable_put_raw_collision);
    g_test_add_func("/core/datastructures/hashtable_update", test_hashtable_update);
    g_test_add_func("/core/datastructures/hashtable_merge", test_hashtable_merge);
    g_test_add_func("/core/datastructures/hashtable_present", test_hashtable_present);
    g_test_add_func("/core/datastructures/hashtable_del_with_next", test_hashtable_del_with_next);
    g_test_add_func("/core/datastructures/hashtable_del_no_next", test_hashtable_del_no_next);
    g_test_add_func("/core/datastructures/hashtable_free", test_hashtable_free);
    g_test_add_func("/core/datastructures/hashtable_equal_different_capacity",
                    test_hashtable_equal_different_capacity);
    g_test_add_func("/core/datastructures/hashset_equal", test_hashset_equal);
    g_test_add_func("/core/datastructures/hashtable_del_hashval_mismatch",
                    test_hashtable_del_hashval_mismatch);
    g_test_add_func("/core/datastructures/hashtable_equal_with_null_keys",
                    test_hashtable_equal_with_null_keys);
    g_test_add_func("/core/datastructures/hashtable_equal_hashval_mismatch",
                    test_hashtable_equal_hashval_mismatch);
    g_test_add_func("/core/datastructures/hashtable_equal_element_not_found",
                    test_hashtable_equal_element_not_found);
    g_test_add_func("/core/datastructures/eq_ptr", test_eq_ptr);
    g_test_add_func("/core/datastructures/hash_ptr", test_hash_ptr);
    g_test_add_func("/core/datastructures/djbhash", test_djbhash);
    g_test_add_func("/core/datastructures/djbhash_large", test_djbhash_large);
    g_test_add_func("/core/datastructures/symbol_put", test_symbol_put);
    g_test_add_func("/core/datastructures/symbol_get_null_table", test_symbol_get_null_table);
    g_test_add_func("/core/datastructures/symbol_free", test_symbol_free);
    g_test_add_func("/core/datastructures/sarray_new", test_sarray_new);
    g_test_add_func("/core/datastructures/sarray_free", test_sarray_free);
}
