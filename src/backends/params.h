/* Copyright (c) 2026 Riverside Research */
#ifndef HAMMER_BACKENDS_PARAMS__H
#define HAMMER_BACKENDS_PARAMS__H

#include "../hammer.h"
#include "../internal.h"

static const size_t DEFAULT_KMAX = 1;

/**
 * @brief Retrieve encoded integer parameter k from a void pointer
 *
 * @param param Pointer value previoulsy produced by casting an int to (void*)
 * @return The decoded int value as size_t, if NULL returns 0.
 * @note  This function assumes the pointer was produced by casting an integer
 *        into a pointer using (void *)(uintptr_t)int_value. Passing arbitrary
 *        pointers yields implementation-defined results.
 */
size_t h_get_param_k(void *param);

/**
 * @brief Format a human-readable description string for a backend that accepts
 *        parameter k.
 *
 * @param  mm__ Allocator context used by h_new. The current implementation
 *              calls h_new to allocate the returned string. The allocator
 *              pointer may be required by the environment; it must be a
 *              valid allocator or NULL if the environment allows it.
 * @param backend_name string with the backend name. Don't allow unsafe input.
 * @param k If k>0 a specific value is printed, if k=0 size defaults to 1.
 * @return Pointer to a string on success, NULL on failure
 * @note   The caller is responsible for freeing the returned string w/ h_free();
 */
char *h_format_description_with_param_k(HAllocator *mm__, const char *backend_name, size_t k);

/**
 * @brief Format a short backend name string that includes parameter k.
 *
 * @param param Pointer value previoulsy produced by casting an int to (void*)
 * @param backend_name string with the backend name. Don't allow unsafe input.
 * @param k If k>0 a specific value is printed, if k=0 size defaults to 1.
 * @return Pointer to a string on success, NULL on failure
 * @note   The caller is responsible for freeing the returned string w/ h_free();
 */
char *h_format_name_with_param_k(HAllocator *mm__, const char *backend_name, size_t k);

/**
 * @brief Extracts the single integer parameter "k" from a backend_with_params_t and
 *        stores it into the backend's params field.
 *        Expects exactly one parameter entry in `be_with_params_t->params`.
 *        The parameter is interpreted as a decimal integer using sscanf("%d").
 *
 * @param be_with_params Output backend structure whose `params` field will be set.
 * @param be_with_params_t Input parameter container holding the raw parameter list.
 * @return n for each successful int extraction & assignment, 0 if the parsing fails.
 *         0 if parse fails, -1 on NULL input, -2 if params are NULL/0 length, -3 if
 *         the raw param is NULL, -4 if the length of the raw param is 0 or >10.
 * @note  On success, the parsed integer value is cast to uintptr_t and stored in
 *        `be_with_params->params` as a (void *) pointer. The caller is responsible for
 *        interpreting this value appropriately.
 */
int h_extract_param_k(HParserBackendWithParams *be_with_params,
                      backend_with_params_t *be_with_params_t);

#endif /* !defined(HAMMER_BACKENDS_PARAMS__H) */