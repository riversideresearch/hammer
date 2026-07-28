/* Copyright (c) 2026 Riverside Research */
/* Parser combinators for binary formats.
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
#include "tsearch.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#if defined(_MSC_VER)
#define h_strdup _strdup
#else
#define h_strdup strdup
#endif

static void *tt_registry = NULL;
static HTTEntry **tt_by_id = NULL;
static size_t tt_by_id_sz = 0;
#define TT_START TT_USER
static HTokenType tt_next = TT_START;

/*
  // TODO: These are for the extension registry, which does not yet have a good name.
static void *ext_registry = NULL;
static HTTEntry** ext_by_id = NULL;
static int ext_by_id_sz = 0;
static int ext_next = 0;
*/

static int compare_entries(const void *v1, const void *v2) {
    const HTTEntry *e1 = (HTTEntry *)v1, *e2 = (HTTEntry *)v2;
    return strcmp(e1->name, e2->name);
}

static void default_unamb_sub(const HParsedToken *tok, struct result_buf *buf) {
    h_append_buf_formatted(buf, "XXX AMBIGUOUS USER TYPE %d", tok->token_type);
}

HTokenType h_allocate_token_new(
    const char *name,
    void (*unamb_sub)(const HParsedToken *tok, struct result_buf *buf),
    void (*pprint)(FILE *stream, const HParsedToken *tok,
                   int indent, int delta)) {
    if (!name)
        return TT_INVALID;

    HTTEntry *new_entry =
        system_allocator.alloc(&system_allocator, sizeof(*new_entry));
    if (!new_entry)
        return TT_INVALID;

    new_entry->name = name;
    new_entry->value = TT_INVALID;
    new_entry->unamb_sub =
        unamb_sub ? unamb_sub : default_unamb_sub;
    new_entry->pprint = pprint;

    void *search_result =
        tsearch(new_entry, &tt_registry, compare_entries);
    if (!search_result) {
        system_allocator.free(&system_allocator, new_entry);
        return TT_INVALID;
    }

    HTTEntry *probe = *(HTTEntry **)search_result;

    /* Existing registration: new_entry was not inserted. */
    if (probe != new_entry) {
        system_allocator.free(&system_allocator, new_entry);
        return probe->value;
    }

    /*
     * Keep new_entry->name pointing at the caller-provided name until all
     * allocations succeed, so tdelete() can still locate it during rollback.
     */
    char *owned_name = h_strdup(name);
    if (!owned_name)
        goto rollback;

    if (tt_next == (HTokenType)INT_MAX)
        goto rollback;

    HTokenType value = tt_next;
    size_t index = (size_t)(value - TT_START);

    if (index >= tt_by_id_sz) {
        size_t new_size = tt_by_id_sz ? tt_by_id_sz : 16;

        while (index >= new_size) {
            if (new_size > SIZE_MAX / 2)
                goto rollback;
            new_size *= 2;
        }

        if (new_size > SIZE_MAX / sizeof(*tt_by_id))
            goto rollback;

        HTTEntry **new_table;
        if (tt_by_id) {
            new_table =
                realloc(tt_by_id,
                        new_size * sizeof(*tt_by_id));
        } else {
            new_table =
                malloc(new_size * sizeof(*tt_by_id));
        }

        if (!new_table)
            goto rollback;

        tt_by_id = new_table;
        tt_by_id_sz = new_size;
    }

    /* No failing operations remain: commit the new registration. */
    probe->name = owned_name;
    probe->value = value;
    tt_by_id[index] = probe;
    tt_next = (HTokenType)(tt_next + 1);

    return value;

rollback:
    /*
     * Delete the tree node before freeing new_entry because the tree
     * comparator still needs new_entry->name.
     */
    tdelete(new_entry, &tt_registry, compare_entries);
    free(owned_name);
    system_allocator.free(&system_allocator, new_entry);
    return TT_INVALID;
}
HTokenType h_allocate_token_type(const char *name) {
    return h_allocate_token_new(name, NULL, NULL);
}
HTokenType h_get_token_type_number(const char *name) {
    HTTEntry e;
    e.name = name;
    HTTEntry **ret = (HTTEntry **)tfind(&e, &tt_registry, compare_entries);
    if (ret == NULL)
        return 0;
    else
        return (*ret)->value;
}
const char *h_get_token_type_name(HTokenType token_type) {
    if (token_type >= tt_next || token_type < TT_START)
        return NULL;
    else
        return tt_by_id[token_type - TT_START]->name;
}
const HTTEntry *h_get_token_type_entry(HTokenType token_type) {
    if (token_type >= tt_next || token_type < TT_START)
        return NULL;
    else
        return tt_by_id[token_type - TT_START];
}
