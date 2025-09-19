#ifndef HAMMER_DNS_COMMON__H
#define HAMMER_DNS_COMMON__H

#include "../src/glue.h"
#include "../src/hammer.h"

HParser *init_domain();
HParser *init_character_string();

HParsedToken *act_index0(const HParseResult *p, void *user_data);

#endif
