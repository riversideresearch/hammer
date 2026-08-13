/* Copyright (c) 2026 Riverside Research */
#include "../trace.h"
#include "parser_internal.h"

#include <stdarg.h>

#if defined(__STDC_VERSION__) &&                                                                   \
    ((__STDC_VERSION__ >= 201112L && !defined(__STDC_NO_VLA__)) || (__STDC_VERSION__ >= 199901L))
#define STACK_VLA(type, name, size) type name[size]
#else
#if defined(_MSC_VER)
#include <malloc.h> // for _alloca
#define STACK_VLA(type, name, size) type *name = _alloca(size)
#else
#error "Missing VLA implementation for this compiler"
#endif
#endif

static HParseResult *parse_choice(void *env, HParseState *state) {
    HSequence *s = (HSequence *)env;
    HInputStream backup = state->input_stream;
    size_t trace_scope = TRACE_CHOICE_BEGIN();
    for (size_t i = 0; i < s->len; ++i) {
        if (i != 0)
            state->input_stream = backup;
        TRACE_CHOICE_ARM_BEGIN(trace_scope, i);
        HParseResult *tmp = h_do_parse(s->p_array[i], state);
        TRACE_CHOICE_ARM_END(trace_scope, tmp != NULL);
        if (NULL != tmp) {
            TRACE_CHOICE_END(trace_scope, true);
            return tmp;
        }
        if (want_suspend(state)) {
            TRACE_CHOICE_END(trace_scope, true);
            return NULL; // bail out early, leaving overrun flag
        }
    }
    // nothing succeeded, so fail
    TRACE_CHOICE_END(trace_scope, false);
    return NULL;
}

static bool choice_isValidRegular(void *env) {
    HSequence *s = (HSequence *)env;
    for (size_t i = 0; i < s->len; ++i) {
        if (!s->p_array[i]->vtable->isValidRegular(s->p_array[i]->env))
            return false;
    }
    return true;
}

static bool choice_isValidCF(void *env) {
    HSequence *s = (HSequence *)env;
    for (size_t i = 0; i < s->len; ++i) {
        if (!s->p_array[i]->vtable->isValidCF(s->p_array[i]->env))
            return false;
    }
    return true;
}

static void desugar_choice(HAllocator *mm__, HCFStack *stk__, void *env) {
    HSequence *s = (HSequence *)env;
    HCFS_BEGIN_CHOICE() {
        for (size_t i = 0; i < s->len; i++) {
            HCFS_BEGIN_SEQ() { HCFS_DESUGAR(s->p_array[i]); }
            HCFS_END_SEQ();
        }
        HCFS_THIS_CHOICE->reshape = h_act_first;
    }
    HCFS_END_CHOICE();
}

static bool choice_ctrvm(HRVMProg *prog, void *env) {
    HSequence *s = (HSequence *)env;
    STACK_VLA(uint16_t, gotos, s->len);
    const HParser *choice = prog->current_parser;
    size_t choice_id = ++prog->next_choice_id;
    for (size_t i = 0; i < s->len; ++i) {
        uint16_t insn = h_rvm_insert_insn(prog, RVM_FORK, 0);
        const HDiagnosticContext *parent_context = prog->current_context;
        HDiagnosticContext *path = h_rvm_alloc(prog, sizeof(*path));
        path->parser = NULL;
        path->choice = choice;
        path->choice_alternative = i;
        path->choice_id = choice_id;
        path->next = parent_context;
        prog->current_context = path;
        bool compiled = h_compile_regex(prog, s->p_array[i]);
        prog->current_context = parent_context;
        if (!compiled)
            return false;
        gotos[i] = h_rvm_insert_insn(prog, RVM_GOTO, 65535);
        h_rvm_patch_arg(prog, insn, h_rvm_get_ip(prog));
    }
    h_rvm_insert_insn(prog, RVM_MATCH, 0x00FF);
    uint16_t jump = h_rvm_get_ip(prog);
    for (size_t i = 0; i < s->len; ++i)
        h_rvm_patch_arg(prog, gotos[i], jump);
    return true;
}

static const HParserVtable choice_vt = {
    .parse = parse_choice,
    .isValidRegular = choice_isValidRegular,
    .isValidCF = choice_isValidCF,
    .compile_to_rvm = choice_ctrvm,
    .desugar = desugar_choice,
    .higher = true,
};

bool h_is_choice_parser(const HParser *parser) { return parser && parser->vtable == &choice_vt; }

HParser *h_choice(HParser *p, ...) {
    va_list ap;
    va_start(ap, p);
    HParser *ret = h_choice__mv(&system_allocator, p, ap);
    va_end(ap);
    return ret;
}

HParser *h_choice__m(HAllocator *mm__, HParser *p, ...) {
    va_list ap;
    va_start(ap, p);
    HParser *ret = h_choice__mv(mm__, p, ap);
    va_end(ap);
    return ret;
}

HParser *h_choice__v(HParser *p, va_list ap) { return h_choice__mv(&system_allocator, p, ap); }

HParser *h_choice__mv(HAllocator *mm__, HParser *p, va_list ap_) {
    va_list ap;
    size_t len = 0;
    HSequence *s = h_new(HSequence, 1);

    HParser *arg;
    va_copy(ap, ap_);
    do {
        len++;
        arg = va_arg(ap, HParser *);
    } while (arg);
    va_end(ap);
    s->p_array = h_new(HParser *, len);

    va_copy(ap, ap_);
    s->p_array[0] = p;
    for (size_t i = 1; i < len; i++) {
        s->p_array[i] = va_arg(ap, HParser *);
    }
    while (arg)
        ;
    va_end(ap);

    s->len = len;
    return h_new_parser_with_free(mm__, &choice_vt, (void *)s, h_free_seq_env);
}

HParser *h_choice__a(void *args[]) { return h_choice__ma(&system_allocator, args); }

HParser *h_choice__ma(HAllocator *mm__, void *args[]) {
    size_t len = -1; // because do...while
    const HParser *arg;

    do {
        arg = ((HParser **)args)[++len];
    } while (arg);

    HSequence *s = h_new(HSequence, 1);
    s->p_array = h_new(HParser *, len);

    for (size_t i = 0; i < len; i++) {
        s->p_array[i] = ((HParser **)args)[i];
    }

    s->len = len;
    HParser *ret = h_new_parser_with_free(mm__, &choice_vt, (void *)s, h_free_seq_env);
    ret->desugared = NULL;
    return ret;
}
