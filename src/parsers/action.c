#include "parser_internal.h"

#include <assert.h>

typedef struct {
    const HParser *p;
    HAction action;
    void *user_data;
} HParseAction;

static HParseResult *parse_action(void *env, HParseState *state) {
    HParseAction *a = (HParseAction *)env;
    if (a->p && a->action) {
        HParseResult *tmp = h_do_parse(a->p, state);
        // HParsedToken *tok = a->action(h_do_parse(a->p, state));
        if (tmp) {
            HParsedToken *tok = (HParsedToken *)a->action(tmp, a->user_data);
            return make_result(state->arena, tok);
        } else
            return NULL;
    } else // either the parser's missing or the action's missing
        return NULL;
}

static void desugar_action(HAllocator *mm__, HCFStack *stk__, void *env) {
    HParseAction *a = (HParseAction *)env;

    HCFS_BEGIN_CHOICE() {
        HCFS_BEGIN_SEQ() { HCFS_DESUGAR(a->p); }
        HCFS_END_SEQ();
        HCFS_THIS_CHOICE->user_data = a->user_data;
        HCFS_THIS_CHOICE->action = a->action;
        HCFS_THIS_CHOICE->reshape = h_act_first;
    }
    HCFS_END_CHOICE();
}

static bool action_isValidRegular(void *env) {
    HParseAction *a = (HParseAction *)env;
    return a->p->vtable->isValidRegular(a->p->env);
}

static bool action_isValidCF(void *env) {
    HParseAction *a = (HParseAction *)env;
    return a->p->vtable->isValidCF(a->p->env);
}

static const HParserVtable action_vt = {
    .parse = parse_action,
    .isValidRegular = action_isValidRegular,
    .isValidCF = action_isValidCF,
    .desugar = desugar_action,
    .higher = true,
};

HParser *h_action(const HParser *p, const HAction a, void *user_data) {
    return h_action__m(&system_allocator, p, a, user_data);
}

HParser *h_action__m(HAllocator *mm__, const HParser *p, const HAction a, void *user_data) {
    HParseAction *env = h_new(HParseAction, 1);
    env->p = p;
    env->action = a;
    env->user_data = user_data;
    return h_new_parser(mm__, &action_vt, env);
}
