#include "params.h"

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

/*TODO better error handling*/
int h_extract_param_k(HParserBackendWithParams *be_with_params,
                      backend_with_params_t *be_with_params_t) {

    be_with_params->params = NULL;

    int param_0 = -1;
    int success = 0;
    uintptr_t param;

    size_t expected_params_len = 1;
    backend_params_t params_t = be_with_params_t->params;
    size_t actual_params_len = params_t.len;

    if (actual_params_len >= expected_params_len) {
        backend_param_with_name_t param_t = params_t.params[0];
        success = sscanf((char *)param_t.param.param, "%d", &param_0);
    }

    if (success) {
        param = (uintptr_t)param_0;
        be_with_params->params = (void *)param;
    }

    return success;
}
