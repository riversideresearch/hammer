/* Copyright (c) 2026 Riverside Research */
#include "params.h"

#include <errno.h>
#include <inttypes.h>

size_t h_get_param_k(void *param) {
    uintptr_t params_int;

    params_int = (uintptr_t)param;
    return (size_t)params_int;
}

char *h_format_description_with_param_k(HAllocator *mm__, const char *backend_name, size_t k) {
    const char *format_str = "%s(%zu) parser backend";

    const char *generic_descr_format_str = "%s(k) parser backend (default k is %zu)";
    size_t len;
    char *descr = NULL;

    if (k > 0) {
        /* A specific k was given */
        /* Measure how big a buffer we need */
        len = snprintf(NULL, 0, format_str, backend_name, k);
        /* Allocate it and do the real snprintf */
        descr = h_new(char, len + 1);
        if (descr) {
            snprintf(descr, len + 1, format_str, backend_name, k);
        }
    } else {
        /*
         * No specific k, would use DEFAULT_KMAX.  We say what DEFAULT_KMAX
         * was compiled in in the description.
         */
        len = snprintf(NULL, 0, generic_descr_format_str, backend_name, DEFAULT_KMAX);
        /* Allocate and do the real snprintf */
        descr = h_new(char, len + 1);
        if (descr) {
            snprintf(descr, len + 1, generic_descr_format_str, backend_name, DEFAULT_KMAX);
        }
    }

    return descr;
}

char *h_format_name_with_param_k(HAllocator *mm__, const char *backend_name, size_t k) {
    const char *format_str = "%s(%zu)", *generic_name = "%s(k)";
    size_t len;
    char *name = NULL;

    if (k > 0) {
        /* A specific k was given */
        /* Measure how big a buffer we need */
        len = snprintf(NULL, 0, format_str, backend_name, k);
        /* Allocate it and do the real snprintf */
        name = h_new(char, len + 1);
        if (name) {
            snprintf(name, len + 1, format_str, backend_name, k);
        }
    } else {
        /* No specific k */
        len = snprintf(NULL, 0, generic_name, backend_name, k);
        name = h_new(char, len + 1);
        if (name) {
            snprintf(name, len + 1, generic_name, backend_name);
        }
    }

    return name;
}

#define MAX_LENGTH 10

int h_extract_param_k(HParserBackendWithParams *be_with_params,
                      backend_with_params_t *be_with_params_t) {

    if (!be_with_params || !be_with_params_t)
        return -1; // NULL input

    be_with_params->params = NULL;

    backend_params_t params_t = be_with_params_t->params;

    if (params_t.params == NULL || params_t.len == 0) {
        return -2; // NULL params in be_with_params_t->params
    }

    backend_param_with_name_t param_t = params_t.params[0];
    if (param_t.param.param == NULL)
        return -3; // NULL param

    size_t len = param_t.param.len; // length of the param string
    if (len > MAX_LENGTH || len == 0)
        return -4; // param_t.param.len is too large or 0

    // char's can sometimes be non NUL-terminated and will cause an overflow on sscanf, so
    // nul-termination can be added inside a temp copy to avoid overwriting unowned memory
    char tmp[MAX_LENGTH + 1];
    memcpy(tmp, param_t.param.param, len);
    tmp[len] = '\0';

    errno = 0;
    char *endptr = NULL;

    intmax_t val = strtoumax(tmp, &endptr, 10);

    if (endptr == tmp) {
        return 0; // No conversion performed
    }
    if (errno == ERANGE) {
        return -4;
    }
    if ((uintmax_t)val > UINTPTR_MAX)
        return -4; // does not fit in uintptr_t

    uintptr_t param = (uintptr_t)val;
    be_with_params->params = (void *)param;

    return 1; // Success
}
