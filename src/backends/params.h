#ifndef HAMMER_BACKENDS_PARAMS__H
#define HAMMER_BACKENDS_PARAMS__H

#include "../hammer.h"
#include "../internal.h"

static const size_t DEFAULT_KMAX = 1;



size_t h_get_param_k(void *param);

char * h_format_description_with_param_k(HAllocator *mm__, const char *backend_name, size_t k);

char * h_format_name_with_param_k(HAllocator *mm__, const char *backend_name, size_t k);

int h_extract_param_k(HParserBackendWithParams * be_with_params, backend_with_params_t * be_with_params_t);

#endif /* !defined(HAMMER_BACKENDS_PARAMS__H) */
