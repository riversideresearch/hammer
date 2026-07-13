/* Copyright (c) 2026 Riverside Research */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* dladdr(), strdup() used by the AST tracer below */
#endif

#include "../internal.h"
#include "trace.h"

/* ------------------------------------------------------------------------- *
 * AST-construction trace.
 *
 * Enabled via HAMMER_TRACE_AST (see trace.h). When enabled, every step of the
 * packrat parse is printed to stderr, indented by recursion depth, so you can
 * watch the AST being built bottom-up. h_do_parse() is the single point every
 * (parser, position) pair flows through, so bracketing its entry/exit yields a
 * tree-shaped log: each "-> parse" line is a parser being tried at a position,
 * and the matching "<= OK/FAIL" line shows the token it produced (or failure),
 * including cache ("memoized") hits.
 * ------------------------------------------------------------------------- */
#if HAMMER_TRACE_AST

#include <ctype.h>    // isprint()
#include <dlfcn.h>    // dladdr()
#include <elf.h>      // Elf64_* for reading .symtab
#include <fcntl.h>
#include <inttypes.h> // PRIu64 etc.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>   // memcmp(), memset(), strdup()
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* runtime on/off switch (compile gate is HAMMER_TRACE_AST above) */
static bool display_trace = true;

static int h_trace_depth = 0;

/* Track every distinct parser that reached the deepest input position, not
 * just the last one: several combinators can bottom out at the same furthest
 * offset and all of them are worth reporting on failure. Names are deduped by
 * pointer (trace_vt_name() returns a stable per-vtable string). */
#define TRACE_MAX_DEEPEST 16

typedef struct {
    size_t abs_pos;
    uint8_t ch;
    uint8_t bit_offset;
    const char *deepest_parsers[TRACE_MAX_DEEPEST];
    size_t n_deepest;
} TraceMaxState;

static TraceMaxState trace_max;

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

/* Append a parser name to the deepest-position set, skipping duplicates and
 * silently capping at TRACE_MAX_DEEPEST entries. */
static void trace_max_add_parser(const char *name) {
    for (size_t i = 0; i < trace_max.n_deepest; i++)
        if (trace_max.deepest_parsers[i] == name)
            return;
    if (trace_max.n_deepest < TRACE_MAX_DEEPEST)
        trace_max.deepest_parsers[trace_max.n_deepest++] = name;
}

static void trace_pos(const HParser *parser, HParseState *state) {
    HInputStream *in = &state->input_stream;
    size_t abs = in->pos + in->index;

    /* Only primitive parsers -- the leaves that actually consume input -- are
     * recorded as the deepest-position parsers. Higher-order combinators merely
     * delegate to their children, so naming them in the failure message adds
     * noise without pointing at what actually failed to match. */
    if (!parser->vtable->higher) {
        const char *name = trace_vt_name(parser->vtable);

        if (abs > trace_max.abs_pos ||
            (abs == trace_max.abs_pos && in->bit_offset > trace_max.bit_offset)) {
            /* strictly deeper (further byte, or same byte + further bit): this is
             * a new furthest position, so discard the old set and start over */
            trace_max.abs_pos = abs;
            trace_max.bit_offset = in->bit_offset;
            trace_max.ch = in->input[in->index];
            trace_max.n_deepest = 0;
            trace_max_add_parser(name);
        } else if (abs == trace_max.abs_pos && in->bit_offset == trace_max.bit_offset) {
            /* another parser tied at the current furthest position: record it too */
            trace_max.ch = in->input[in->index];
            trace_max_add_parser(name);
        }
    }

    fprintf(stderr, "@%zu", trace_max.abs_pos);
    if (trace_max.bit_offset)
        fprintf(stderr, ".%db", trace_max.bit_offset);
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

void h_trace_begin(size_t input_len) {
    if (!display_trace)
        return;
    h_trace_depth = 0;
    memset(&trace_max, 0, sizeof(trace_max));
    fprintf(stderr, "\n=== h_packrat_parse: begin (%zu bytes of input) ===\n", input_len);
}

void h_trace_enter(const HParser *parser, HParseState *state) {
    if (!display_trace)
        return;
    trace_indent();
    fprintf(stderr, "-> %-20s %-9s ", trace_vt_name(parser->vtable),
            parser->vtable->higher ? "higher" : "primitive");
    trace_pos(parser, state);
    fputc('\n', stderr);
    h_trace_depth++;
}

void h_trace_exit(HParseResult *res, const char *note) {
    if (!display_trace)
        return;
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

void h_trace_end(HParseResult *res, HParseState *state) {
    if (!display_trace)
        return;

    fprintf(stderr, "=== h_packrat_parse: end (%s) ===\n", res ? "SUCCESS" : "FAILURE");

    if (res)
        return;

    HInputStream *in = &state->input_stream;

    if (trace_max.abs_pos < in->length) {
        uint8_t c = trace_max.ch;
        char disp[2] = { isprint(c) ? (char)c : '\0', '\0' };
        fprintf(stdout, "error: unexpected byte: '%s' (0x%02x = %d)", disp, c, c);
    } else {
        fprintf(stdout, "error: unexpected end of input");
    }

    fprintf(stdout, " at index %zu", trace_max.abs_pos);

    if (trace_max.bit_offset)
        fprintf(stdout, ".%db", trace_max.bit_offset);

    if (trace_max.n_deepest > 0) {
        fputs(" while running [", stdout);
        for (size_t i = 0; i < trace_max.n_deepest; i++)
            fprintf(stdout, "%s%s", i ? ", " : "", trace_max.deepest_parsers[i]);
        fputc(']', stdout);
    }

    fprintf(stdout, "\n");
}

#endif /* HAMMER_TRACE_AST */