#include "parser_internal.h"

#include <assert.h>
#include <ctype.h>

static HParseResult *parse_whitespace(void *env, HParseState *state) {
    char c;
    HInputStream bak;
    do {
        bak = state->input_stream;
        c = h_read_bits(&state->input_stream, 8, false);
        if (want_suspend(state))
            return NULL; // bail out early, leaving overrun flag
        if (state->input_stream.overrun)
            break;
    } while (isspace((int)c));
    state->input_stream = bak;
    return h_do_parse((HParser *)env, state);
}

static const char SPACE_CHRS[6] = {' ', '\f', '\n', '\r', '\t', '\v'};

static void desugar_whitespace(HAllocator *mm__, HCFStack *stk__, void *env) {

    HCharset ws_cs = new_charset(mm__);
    for (size_t i = 0; i < sizeof(SPACE_CHRS); i++)
        charset_set(ws_cs, SPACE_CHRS[i], 1);

    HCFS_BEGIN_CHOICE() {
        HCFS_BEGIN_SEQ() {
            HCFS_BEGIN_CHOICE() {
                HCFS_BEGIN_SEQ() {
                    HCFS_ADD_CHARSET(ws_cs);
                    HCFS_APPEND(HCFS_THIS_CHOICE); // yay circular pointer!
                }
                HCFS_END_SEQ();
                HCFS_BEGIN_SEQ() {}
                HCFS_END_SEQ();
            }
            HCFS_END_CHOICE();
            HCFS_DESUGAR((HParser *)env);
        }
        HCFS_END_SEQ();
        HCFS_THIS_CHOICE->reshape = h_act_last;
    }
    HCFS_END_CHOICE();
}

static bool ws_isValidRegular(void *env) {
    HParser *p = (HParser *)env;
    return p->vtable->isValidRegular(p->env);
}

static bool ws_isValidCF(void *env) {
    HParser *p = (HParser *)env;
    return p->vtable->isValidCF(p->env);
}

static const HParserVtable whitespace_vt = {
    .parse = parse_whitespace,
    .isValidRegular = ws_isValidRegular,
    .isValidCF = ws_isValidCF,
    .desugar = desugar_whitespace,
    .higher = false,
};

HParser *h_whitespace(const HParser *p) { return h_whitespace__m(&system_allocator, p); }
HParser *h_whitespace__m(HAllocator *mm__, const HParser *p) {
    void *env = (void *)p;
    return h_new_parser(mm__, &whitespace_vt, env);
}
