#include "hammer.h"
#include "internal.h"
#include "test_suite.h"

#include <glib.h>
#include <stdio.h>

static void test_registry_allocate_token_new_with_unamb(void) {
    void unamb_sub(const HParsedToken *tok, struct result_buf *buf) {
        h_append_buf_formatted(buf, "CUSTOM: %d", tok->token_type);
    }

    HTokenType id = h_allocate_token_new("test.registry.unamb", unamb_sub, NULL);
    g_check_cmp_int(id, >=, TT_USER);

    const HTTEntry *entry = h_get_token_type_entry(id);
    g_check_cmp_ptr(entry, !=, NULL);
    if (entry) {
        g_check_cmp_ptr(entry->unamb_sub, ==, unamb_sub);
    }
}

static void test_registry_allocate_token_new_default_unamb(void) {
    HTokenType id = h_allocate_token_new("test.registry.default_unamb", NULL, NULL);
    g_check_cmp_int(id, >=, TT_USER);

    const HTTEntry *entry = h_get_token_type_entry(id);
    g_check_cmp_ptr(entry, !=, NULL);
    if (entry) {
        g_check_cmp_ptr(entry->unamb_sub, !=, NULL);
    }
}

static void test_registry_allocate_token_duplicate(void) {
    const char *name = "test.registry.duplicate";
    HTokenType id1 = h_allocate_token_type(name);
    HTokenType id2 = h_allocate_token_type(name);
    g_check_cmp_int(id1, ==, id2);
}

static void test_registry_get_token_type_entry(void) {
    HTokenType id = h_allocate_token_type("test.registry.entry");

    const HTTEntry *entry = h_get_token_type_entry(id);
    g_check_cmp_ptr(entry, !=, NULL);
    if (entry) {
        g_check_string(entry->name, ==, "test.registry.entry");
    }

    const HTTEntry *invalid = h_get_token_type_entry(0);
    g_check_cmp_ptr(invalid, ==, NULL);

    const HTTEntry *out_of_range = h_get_token_type_entry((HTokenType)99999);
    g_check_cmp_ptr(out_of_range, ==, NULL);
}

static void test_registry_get_token_type_entry_invalid(void) {
    const HTTEntry *entry = h_get_token_type_entry(TT_INVALID);
    g_check_cmp_ptr(entry, ==, NULL);
}

static void test_registry_get_token_type_name_valid(void) {
    HTokenType id = h_allocate_token_type("test.registry.name");
    const char *name = h_get_token_type_name(id);
    g_check_string(name, ==, "test.registry.name");
}

static void test_registry_get_token_type_name_invalid(void) {
    const char *name = h_get_token_type_name(TT_INVALID);
    g_check_cmp_ptr(name, ==, NULL);

    const char *name2 = h_get_token_type_name((HTokenType)99999);
    g_check_cmp_ptr(name2, ==, NULL);
}

static void test_registry_get_token_type_number_existing(void) {
    const char *name = "test.registry.number";
    HTokenType id1 = h_allocate_token_type(name);
    HTokenType id2 = h_get_token_type_number(name);
    g_check_cmp_int(id1, ==, id2);
}

static void test_registry_get_token_type_number_nonexisting(void) {
    HTokenType id = h_get_token_type_number("test.registry.nonexisting");
    g_check_cmp_int(id, ==, 0);
}

static void test_registry_allocate_token_new_initial_allocation(void) {
    HTokenType id = h_allocate_token_type("test.registry.initial");
    g_check_cmp_int(id, >=, TT_USER);
}

static void test_registry_allocate_token_new_realloc_path(void) {
    char name[100];
    for (int i = 0; i < 20; i++) {
        snprintf(name, sizeof(name), "test.registry.realloc.%d", i);
        HTokenType id = h_allocate_token_type(name);
        g_check_cmp_int(id, >=, TT_USER);
    }
}

void register_registry_tests(void) {
    g_test_add_func("/core/registry/allocate_token_new_with_unamb",
                    test_registry_allocate_token_new_with_unamb);
    g_test_add_func("/core/registry/allocate_token_new_default_unamb",
                    test_registry_allocate_token_new_default_unamb);
    g_test_add_func("/core/registry/allocate_token_duplicate",
                    test_registry_allocate_token_duplicate);
    g_test_add_func("/core/registry/get_token_type_entry", test_registry_get_token_type_entry);
    g_test_add_func("/core/registry/get_token_type_entry_invalid",
                    test_registry_get_token_type_entry_invalid);
    g_test_add_func("/core/registry/get_token_type_name_valid",
                    test_registry_get_token_type_name_valid);
    g_test_add_func("/core/registry/get_token_type_name_invalid",
                    test_registry_get_token_type_name_invalid);
    g_test_add_func("/core/registry/get_token_type_number_existing",
                    test_registry_get_token_type_number_existing);
    g_test_add_func("/core/registry/get_token_type_number_nonexisting",
                    test_registry_get_token_type_number_nonexisting);
    g_test_add_func("/core/registry/allocate_token_new_initial_allocation",
                    test_registry_allocate_token_new_initial_allocation);
    g_test_add_func("/core/registry/allocate_token_new_realloc_path",
                    test_registry_allocate_token_new_realloc_path);
}
