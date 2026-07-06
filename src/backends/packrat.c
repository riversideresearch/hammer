/* Copyright (c) 2026 Riverside Research */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* dladdr(), strdup() used by the AST tracer below */
#endif
#include "../internal.h"
#include "../parsers/parser_internal.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------- *
 * AST-construction trace.
 *
 * Set HAMMER_TRACE_AST to 0 to disable. When enabled, every step of the
 * packrat parse is printed to stderr, indented by recursion depth, so you can
 * watch the AST being built bottom-up. h_do_parse() is the single point every
 * (parser, position) pair flows through, so bracketing its entry/exit yields a
 * tree-shaped log: each "-> parse" line is a parser being tried at a position,
 * and the matching "<= OK/FAIL" line shows the token it produced (or failure),
 * including cache ("memoized") hits.
 * ------------------------------------------------------------------------- */
#define HAMMER_TRACE_AST 1

#if HAMMER_TRACE_AST
#include <dlfcn.h> // dladdr()
#include <elf.h>   // Elf64_* for reading .symtab
#include <ctype.h>   // for isprint
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int h_trace_depth = 0;

static const char *trace_tt_name(HTokenType t) {
    switch (t) {
    case TT_NONE:     return "NONE";
    case TT_BYTES:    return "BYTES";
    case TT_SINT:     return "SINT";
    case TT_UINT:     return "UINT";
    case TT_DOUBLE:   return "DOUBLE";
    case TT_FLOAT:    return "FLOAT";
    case TT_SEQUENCE: return "SEQUENCE";
    case TT_ERR:      return "ERR";
    default:          return (t >= TT_USER) ? "USER" : "INVALID";
    }
}

static void trace_indent(void) {
    for (int i = 0; i < h_trace_depth; i++)
        fputs("  ", stderr);
}

static void trace_pos(HParseState *state) {
    HInputStream *in = &state->input_stream;
    fprintf(stderr, "@%zu", (size_t)(in->pos + in->index));
    if (in->bit_offset)
        fprintf(stderr, ".%db", in->bit_offset);
}

// print a one-line summary of the token an HParseResult carries
static void trace_token(const HParsedToken *tok) {
    if (!tok) {
        fputs("(null ast)", stderr);
        return;
    }
    switch (tok->token_type) {
    case TT_UINT:
        fprintf(stderr, "UINT %" PRIu64 " (0x%02" PRIx64 ")", tok->uint, tok->uint);
        break;
    case TT_SINT:
        fprintf(stderr, "SINT %" PRId64, tok->sint);
        break;
    case TT_BYTES:
        fprintf(stderr, "BYTES[%zu]", tok->bytes.len);
        break;
    case TT_SEQUENCE:
        fprintf(stderr, "SEQUENCE[%zu children]", tok->seq ? tok->seq->used : (size_t)0);
        break;
    default:
        fputs(trace_tt_name(tok->token_type), stderr);
        break;
    }
}

/* --- function-pointer -> name via dladdr() + ELF .symtab -----------------
 * We want the name of each combinator's parse function (parse_choice, ...).
 * Those functions are `static`, so they are absent from the dynamic symbol
 * table and dladdr() cannot name them on its own. So we use dladdr() only to
 * find which shared object the address belongs to (dli_fname) and its load
 * base (dli_fbase), then read that object's on-disk .symtab -- which still
 * contains local/static symbols, unless the binary was stripped -- and locate
 * the STT_FUNC symbol whose [value, value+size) range covers the address.
 *
 * Results are cached per vtable, so we open/scan the ELF at most once per
 * distinct combinator. Linux/ELF64 only; the whole block is debug-gated.
 */
static char *resolve_fn_name(void *addr) {
    Dl_info info;
    if (!dladdr(addr, &info) || !info.dli_fname)
        return NULL;

    int fd = open(info.dli_fname, O_RDONLY);
    if (fd < 0)
        return NULL;

    struct stat stbuf;
    if (fstat(fd, &stbuf) != 0 || (size_t)stbuf.st_size < sizeof(Elf64_Ehdr)) {
        close(fd);
        return NULL;
    }

    uint8_t *map = mmap(NULL, stbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED)
        return NULL;

    char *result = NULL;
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)map;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) == 0 && eh->e_ident[EI_CLASS] == ELFCLASS64) {
        // ET_DYN (shared lib / PIE): symbol values are offsets from the load
        // base. ET_EXEC: they are absolute addresses.
        uintptr_t target = (uintptr_t)addr;
        if (eh->e_type == ET_DYN)
            target -= (uintptr_t)info.dli_fbase;

        const Elf64_Shdr *sh = (const Elf64_Shdr *)(map + eh->e_shoff);
        for (unsigned i = 0; i < eh->e_shnum && result == NULL; i++) {
            if (sh[i].sh_type != SHT_SYMTAB)
                continue; // .dynsym is SHT_DYNSYM; we want .symtab (has statics)
            const Elf64_Sym *syms = (const Elf64_Sym *)(map + sh[i].sh_offset);
            size_t nsyms = sh[i].sh_size / sizeof(Elf64_Sym);
            const char *strtab = (const char *)(map + sh[sh[i].sh_link].sh_offset);
            for (size_t j = 0; j < nsyms; j++) {
                if (ELF64_ST_TYPE(syms[j].st_info) != STT_FUNC || syms[j].st_value == 0)
                    continue;
                uintptr_t lo = syms[j].st_value;
                uintptr_t hi = lo + (syms[j].st_size ? syms[j].st_size : 1);
                if (target >= lo && target < hi) {
                    result = strdup(strtab + syms[j].st_name);
                    break;
                }
            }
        }
    }

    munmap(map, stbuf.st_size);
    return result;
}

// cache: vtable pointer -> resolved parse-function name (looked up once each)
static struct {
    const HParserVtable *vt;
    const char *name;
} h_namecache[64];
static size_t h_namecache_len = 0;

static const char *trace_vt_name(const HParserVtable *vt) {
    for (size_t i = 0; i < h_namecache_len; i++)
        if (h_namecache[i].vt == vt)
            return h_namecache[i].name;

    // read the function pointer's bits as a data pointer without a direct
    // function->void* cast (which -pedantic rejects)
    char *name = resolve_fn_name(*(void *const *)&vt->parse);

    const char *stored = name ? name : "?(no symbol)";
    if (h_namecache_len < sizeof(h_namecache) / sizeof(h_namecache[0])) {
        h_namecache[h_namecache_len].vt = vt;
        h_namecache[h_namecache_len].name = stored;
        h_namecache_len++;
    }
    return stored;
}

static void trace_enter(const HParser *parser, HParseState *state) {
    trace_indent();
    fprintf(stderr, "-> %-20s %-9s ", trace_vt_name(parser->vtable),
            parser->vtable->higher ? "higher" : "primitive");
    trace_pos(state);
    fputc('\n', stderr);
    h_trace_depth++;
}

static void trace_exit(HParseResult *res, const char *note) {
    if (h_trace_depth > 0)
        h_trace_depth--;
    trace_indent();
    if (res) {
        fputs("<= OK   ast=", stderr);
        trace_token(res->ast);
        fprintf(stderr, " (%" PRId64 " bits)", res->bit_length);
    } else {
        fputs("<= FAIL", stderr);
    }
    if (note)
        fprintf(stderr, "  [%s]", note);
    fputc('\n', stderr);
}

static void trace_end(HParseResult *res, HParseState *state) {
    fprintf(stderr, "=== h_packrat_parse: end (%s) ===\n", res ? "SUCCESS" : "FAILURE");
    if (res)
        return;

    HInputStream *in = &state->input_stream;

    if (in->index < in->length) {
        uint8_t c = in->input[in->index];
        char disp[2] = { isprint(c) ? (char)c : '\0', '\0' };
        fprintf(stdout, "error: unexpected character: '%s' (0x%02x = %d)", disp, c, c);
    } else {
        fprintf(stdout, "error: unexpected end of input");
    }

    fprintf(stdout, " at index %zu", (size_t)(in->pos + in->index));
    if (in->bit_offset)
        fprintf(stdout, ".%db", in->bit_offset);
    fprintf(stdout, "\n");
}

#define TRACE_ENTER(p, s)     trace_enter((p), (s))
#define TRACE_EXIT(res, note) trace_exit((res), (note))
#define TRACE_BEGIN(len)                                                                           \
    do {                                                                                           \
        h_trace_depth = 0;                                                                         \
        fprintf(stderr, "\n=== h_packrat_parse: begin (%zu bytes of input) ===\n", (size_t)(len)); \
    } while (0)
#define TRACE_END(res, state) trace_end (res, state)
#else
#define TRACE_ENTER(p, s)     ((void)0)
#define TRACE_EXIT(res, note) ((void)0)
#define TRACE_BEGIN(len)      ((void)0)
#define TRACE_END(res, state)        ((void)0)
#endif

/* #define DETAILED_PACKRAT_STATISTICS */

#ifdef DETAILED_PACKRAT_STATISTICS
static size_t packrat_hash_count = 0;
static size_t packrat_hash_bytes = 0;
static size_t packrat_cmp_count = 0;
static size_t packrat_cmp_bytes = 0;
#endif

static uint32_t cache_key_hash(const void *key);

// short-hand for creating lowlevel parse cache values (parse result case)
static HParserCacheValue *cached_result(HParseState *state, HParseResult *result) {
    HParserCacheValue *ret = a_new(HParserCacheValue, 1);
    ret->value_type = PC_RIGHT;
    ret->value.right = result;
    ret->input_stream = state->input_stream;
    return ret;
}

// short-hand for creating lowlevel parse cache values (left recursion case)
static HParserCacheValue *cached_lr(HParseState *state, HLeftRec *lr) {
    HParserCacheValue *ret = a_new(HParserCacheValue, 1);
    ret->value_type = PC_LEFT;
    ret->value.left = lr;
    ret->input_stream = state->input_stream;
    return ret;
}

// internal helper to perform an uncached parse and common error-handling
static inline HParseResult *perform_lowlevel_parse(HParseState *state, const HParser *parser) {
    HParseResult *res;
    HInputStream bak;
    size_t len;

    if (!parser)
        return NULL;

    bak = state->input_stream;
    res = parser->vtable->parse(parser->env, state);

    if (!res)
        return NULL; // NB: input position is considered invalid on failure

    // combinators' parse functions by design do not have to check for overrun.
    // turn such bogus successes into parse failure.
    if (state->input_stream.overrun) {
        res->bit_length = 0;
        return NULL;
    }

    // update result length
    res->arena = state->arena;
    len = h_input_stream_pos(&state->input_stream) - h_input_stream_pos(&bak);
    if (res->bit_length == 0) // Don't modify if forwarding.
        res->bit_length = len;
    if (res->ast && res->ast->bit_length != 0)
        ((HParsedToken *)(res->ast))->bit_length = len;

    return res;
}

HParserCacheValue *recall(HParserCacheKey *k, HParseState *state, HHashValue keyhash) {
    HParserCacheValue *cached = h_hashtable_get_precomp(state->cache, k, keyhash);
    HRecursionHead *head = h_hashtable_get(state->recursion_heads, &k->input_pos);

    if (!head) {
        /* No heads found */
        return cached;
    } else {
        /* Some heads found */
        if (!cached && head->head_parser != k->parser &&
            !h_slist_find(head->involved_set, k->parser)) {
            /* Nothing in the cache, and the key parser is not involved */
            cached = cached_result(state, NULL);
            cached->input_stream = k->input_pos;
        }
        if (h_slist_find(head->eval_set, k->parser)) {
            /*
             * Something is in the cache, and the key parser is in the eval set.
             * Remove the key parser from the eval set of the head.
             */
            head->eval_set = h_slist_remove_all(head->eval_set, k->parser);
            HParseResult *tmp_res = perform_lowlevel_parse(state, k->parser);
            /* update the cache */
            if (!cached) {
                cached = cached_result(state, tmp_res);
                h_hashtable_put_precomp(state->cache, k, cached, keyhash);
            } else {
                cached->value_type = PC_RIGHT;
                cached->value.right = tmp_res;
                cached->input_stream = state->input_stream;
            }
        }

        return cached;
    }
}

/* Setting up the left recursion. We have the LR for the rule head;
 * we modify the involved_sets of all LRs in the stack, until we
 * see the current parser again.
 */

void setupLR(const HParser *p, HParseState *state, HLeftRec *rec_detect) {
    if (!rec_detect->head) {
        HRecursionHead *some = a_new(HRecursionHead, 1);
        some->head_parser = p;
        some->involved_set = h_slist_new(state->arena);
        some->eval_set = NULL;
        rec_detect->head = some;
    }

    HSlistNode *it;
    for (it = state->lr_stack->head; it; it = it->next) {
        HLeftRec *lr = it->elem;

        if (lr->rule == p)
            break;

        lr->head = rec_detect->head;
        h_slist_push(lr->head->involved_set, (void *)lr->rule);
    }
}

// helper: true iff pos1 is less than pos2
static inline bool pos_lt(HInputStream pos1, HInputStream pos2) {
    return ((pos1.index < pos2.index) ||
            (pos1.index == pos2.index && pos1.bit_offset < pos2.bit_offset));
}

/* If recall() returns NULL, we need to store a dummy failure in the cache and compute the
 * future parse.
 */

HParseResult *grow(HParserCacheKey *k, HParseState *state, HRecursionHead *head) {
    // Store the head into the recursion_heads
    h_hashtable_put(state->recursion_heads, &k->input_pos, head);
    HParserCacheValue *old_cached = h_hashtable_get(state->cache, k);
    if (!old_cached || PC_LEFT == old_cached->value_type)
        h_platform_errx(1, "impossible match");
    HParseResult *old_res = old_cached->value.right;

    // rewind the input
    state->input_stream = k->input_pos;

    // reset the eval_set of the head of the recursion at each beginning of growth
    head->eval_set = h_slist_copy(head->involved_set);
    HParseResult *tmp_res = perform_lowlevel_parse(state, k->parser);

    if (tmp_res) {
        if (pos_lt(old_cached->input_stream, state->input_stream)) {
            h_hashtable_put(state->cache, k, cached_result(state, tmp_res));
            return grow(k, state, head);
        } else {
            // we're done with growing, we can remove data from the recursion head
            h_hashtable_del(state->recursion_heads, &k->input_pos);
            HParserCacheValue *cached = h_hashtable_get(state->cache, k);
            if (cached && PC_RIGHT == cached->value_type) {
                state->input_stream = cached->input_stream;
                return cached->value.right;
            } else {
                h_platform_errx(1, "impossible match");
            }
        }
    } else {
        h_hashtable_del(state->recursion_heads, &k->input_pos);
        state->input_stream = old_cached->input_stream;
        return old_res;
    }
}

HParseResult *lr_answer(HParserCacheKey *k, HParseState *state, HLeftRec *growable) {
    if (growable->head) {
        if (growable->head->head_parser != k->parser) {
            // not the head rule, so not growing
            return growable->seed;
        } else {
            // update cache
            h_hashtable_put(state->cache, k, cached_result(state, growable->seed));
            if (!growable->seed)
                return NULL;
            else
                return grow(k, state, growable->head);
        }
    } else {
        h_platform_errx(1, "lrAnswer with no head");
    }
}

/* Warth's recursion. Hi Alessandro! */
HParseResult *h_do_parse(const HParser *parser, HParseState *state) {
    HParserCacheKey *key = a_new(HParserCacheKey, 1);
    HHashValue keyhash;
    HLeftRec *base = NULL;
    HParserCacheValue *m = NULL, *cached = NULL;

    TRACE_ENTER(parser, state);

    key->input_pos = state->input_stream;
    key->parser = parser;
    keyhash = cache_key_hash(key);

    if (parser->vtable->higher) {
        m = recall(key, state, keyhash);
    }

    /* check to see if there is already a result for this object... */
    if (!m) {
        /*
         * But only cache it now if there's some chance it could grow; primitive
         * parsers can't
         */
        if (parser->vtable->higher) {
            base = a_new(HLeftRec, 1);
            base->seed = NULL;
            base->rule = parser;
            base->head = NULL;
            h_slist_push(state->lr_stack, base);
            /* cache it */
            h_hashtable_put_precomp(state->cache, key, cached_lr(state, base), keyhash);
        }

        /* parse the input */
        HParseResult *tmp_res = perform_lowlevel_parse(state, parser);
        if (parser->vtable->higher) {
            /* the base variable has passed equality tests with the cache */
            h_slist_pop(state->lr_stack);
            /* update the cached value to our new position */
            cached = h_hashtable_get_precomp(state->cache, key, keyhash);
            if (cached == NULL)
                return tmp_res;
            assert(cached != NULL);
            cached->input_stream = state->input_stream;
        }

        /*
         * setupLR, used below, mutates the LR to have a head if appropriate,
         * so we check to see if we have one
         */
        if (!base || NULL == base->head) {
            if (parser->vtable->higher) {
                h_hashtable_put_precomp(state->cache, key, cached_result(state, tmp_res), keyhash);
            }
            TRACE_EXIT(tmp_res, parser->vtable->higher ? "computed" : "primitive");
            return tmp_res;
        } else {
            base->seed = tmp_res;
            HParseResult *res = lr_answer(key, state, base);
            TRACE_EXIT(res, "left-recursion answer");
            return res;
        }
    } else {
        /* it exists! */
        state->input_stream = m->input_stream;
        if (PC_LEFT == m->value_type) {
            setupLR(parser, state, m->value.left);
            TRACE_EXIT(m->value.left->seed, "memoized (LR seed)");
            return m->value.left->seed;
        } else {
            TRACE_EXIT(m->value.right, "memoized (cache hit)");
            return m->value.right;
        }
    }
}

int h_packrat_compile(HAllocator *mm__, HParser *parser, const void *params) {
    parser->backend_vtable = &h__packrat_backend_vtable;
    parser->backend = PB_PACKRAT;
    return 0; // No compilation necessary, and everything should work
              // out of the box.
}

void h_packrat_free(HParser *parser) {
    parser->backend_vtable = h_get_default_backend_vtable();
    parser->backend = h_get_default_backend();
}

static uint32_t cache_key_hash(const void *key) {
#ifdef DETAILED_PACKRAT_STATISTICS
    ++(packrat_hash_count);
    packrat_hash_bytes += sizeof(HParserCacheKey);
#endif
    return h_djbhash(key, sizeof(HParserCacheKey));
}

static bool cache_key_equal(const void *key1, const void *key2) {
#ifdef DETAILED_PACKRAT_STATISTICS
    ++(packrat_cmp_count);
    packrat_cmp_bytes += sizeof(HParserCacheKey);
#endif
    return memcmp(key1, key2, sizeof(HParserCacheKey)) == 0;
}

static uint32_t pos_hash(const void *key) {
#ifdef DETAILED_PACKRAT_STATISTICS
    ++(packrat_hash_count);
    packrat_hash_bytes += sizeof(HInputStream);
#endif
    return h_djbhash(key, sizeof(HInputStream));
}

static bool pos_equal(const void *key1, const void *key2) {
#ifdef DETAILED_PACKRAT_STATISTICS
    ++(packrat_cmp_count);
    packrat_cmp_bytes += sizeof(HInputStream);
#endif
    return memcmp(key1, key2, sizeof(HInputStream)) == 0;
}

HParseResult *h_packrat_parse(HAllocator *mm__, const HParser *parser, HInputStream *input_stream) {
    TRACE_BEGIN(input_stream->length);
    HArena *arena = h_new_arena(mm__, 0);

    // out-of-memory handling
    jmp_buf except;
    h_arena_set_except(arena, &except);
    if (setjmp(except)) {
        h_delete_arena(arena);
        return NULL;
    }

    HParseState *parse_state = a_new_(arena, HParseState, 1);
    parse_state->cache = h_hashtable_new(arena, cache_key_equal, // key_equal_func
                                         cache_key_hash);        // hash_func
    parse_state->input_stream = *input_stream;
    parse_state->lr_stack = h_slist_new(arena);
    parse_state->recursion_heads = h_hashtable_new(arena, pos_equal, pos_hash);
    parse_state->arena = arena;
    parse_state->symbol_table = NULL;
    HParseResult *res = h_do_parse(parser, parse_state);
    *input_stream = parse_state->input_stream;
    h_slist_free(parse_state->lr_stack);
    h_hashtable_free(parse_state->recursion_heads);
    // tear down the parse state
    h_hashtable_free(parse_state->cache);
    TRACE_END(res, parse_state);
    if (!res)
        h_delete_arena(parse_state->arena);

    return res;
}

// The following naive implementation of the iterative (chunked) parsing API
// concatenates chunks and blindly re-runs the full parse on every call to
// h_packrat_parse_chunk.
//
// NB: A full implementation will still have to concatenate the chunks to
// support arbitrary backtracking, but should be able save much, if not all, of
// the HParseState between calls.
// Cutting unneeded past input should also be possible but is complicated by
// the fact that only higher-order combinators are saved to the packrat cache,
// so former input to bare primitive combinators must remain available.
//
// Note: The iterative API expects us to always consume an entire input chunk
// when we suspend, even if packrat later backtracks into it. We will produce
// the correct parse result and accurately consume from a final chunk, but all
// earlier chunks will be reported as fully consumed and as being part of the
// HParseResult in terms of its bit_length field.

void h_packrat_parse_start(HSuspendedParser *s) {
    // nothing to do here, we allocate lazily below
}

bool h_packrat_parse_chunk(HSuspendedParser *s, HInputStream *input) {
    HAllocator *mm__ = s->mm__;
    HParseResult *res;
    HInputStream *cat;
    size_t newlen;

    if (s->backend_state == NULL) { // this is the first chunk
        // attempt to finish the parse on just the given input.
        res = h_packrat_parse(mm__, s->parser, input);
        if (input->last_chunk || !input->overrun) {
            s->backend_state = res; // pass on the result
            return true;            // and signal we're done
        }

        // we ran out of input and are expecting more
        // allocate and initialize an input stream to concatenate the chunks
        cat = h_new(HInputStream, 1);
        if (!cat) {
            s->backend_state = NULL;
            return true;
        }
        *cat = *input;
        cat->input = h_alloc(mm__, input->length);
        if (!cat->input) {
            h_free(cat);
            s->backend_state = NULL;
            return true;
        }
        memcpy((void *)cat->input, input->input, input->length);
        s->backend_state = cat;

        goto suspend;
    }

    // we have received additional input - append it to the saved stream
    cat = s->backend_state;
    if (input->pos != cat->length) {
        h_free((void *)cat->input);
        h_free(cat);
        s->backend_state = NULL;
        return true;
    }
    assert(input->pos == cat->length);
    if (input->length > SIZE_MAX - cat->length)
        h_platform_errx(1, "input length would overflow");
    newlen = cat->length + input->length;
    void *new_input = h_realloc(mm__, (void *)cat->input, newlen);
    if (!new_input) {
        h_free((void *)cat->input);
        h_free(cat);
        s->backend_state = NULL;
        return true;
    }
    cat->input = new_input;
    memcpy((void *)((uint8_t *)cat->input + cat->length), input->input, input->length);
    cat->length = newlen;
    cat->last_chunk = input->last_chunk;

    // reset our input stream and call the parser on it (again)
    cat->index = 0;
    cat->bit_offset = 0;
    cat->margin = 0;
    cat->endianness = DEFAULT_ENDIANNESS;
    cat->overrun = false;
    res = h_packrat_parse(mm__, s->parser, cat);
    if (cat->index > cat->length) {
        h_free((void *)cat->input);
        h_free(cat);
        s->backend_state = NULL;
        return true;
    }
    assert(cat->index <= cat->length);
    input->overrun = cat->overrun;

    // suspend if the parser still needs more input
    if (input->overrun && !input->last_chunk)
        goto suspend;
    // otherwise the parse is finished...

    // report final input position
    if (cat->index < input->pos) { // parser just needed some lookahead
        input->index = 0;          // don't consume this last chunk
        input->bit_offset = 0;
        input->margin = 0;
    } else {
        input->index = cat->index - input->pos;
        input->bit_offset = cat->bit_offset;
        input->margin = cat->margin;
        input->endianness = cat->endianness;
    }

    // clean up and return the result
    h_free((void *)cat->input);
    h_free(cat);
    s->backend_state = res;

    return true; // don't call me again.

suspend:
    input->index = input->length; // consume the entire chunk on suspend
    input->margin = 0;
    input->bit_offset = 0;
    return false; // come back with more input.
}

HParseResult *h_packrat_parse_finish(HSuspendedParser *s) { return s->backend_state; }

HParserBackendVTable h__packrat_backend_vtable = {
    .compile = h_packrat_compile,
    .parse = h_packrat_parse,
    .free = h_packrat_free,
    .parse_start = h_packrat_parse_start,
    .parse_chunk = h_packrat_parse_chunk,
    .parse_finish = h_packrat_parse_finish,
    /* Name/param resolution functions */
    .backend_short_name = "packrat",
    .backend_description = "Packrat parser with Warth's recursion",
    .get_description_with_params = h_get_description_with_no_params,
    .get_short_name_with_params = h_get_short_name_with_no_params};
