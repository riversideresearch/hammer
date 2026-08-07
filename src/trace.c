/* Copyright (c) 2026 Riverside Research */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* dladdr(), strdup() used by the AST tracer below */
#endif

#include "trace.h"
#include "backends/regex.h"
#include "internal.h"

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

#include <ctype.h> // isprint()
#include <dlfcn.h> // dladdr()
#include <elf.h>   // Elf64_* for reading .symtab
#include <fcntl.h>
#include <inttypes.h> // PRIu64 etc.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // memcmp(), memset(), strdup()
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(_WIN32)
#include <io.h>
#define ISATTY(fd) _isatty(_fileno(fd))
#else
#include <unistd.h>
#define ISATTY(fd) isatty(fileno(fd))
#endif

#if defined(_MSC_VER)
#define H_TRACE_THREAD_LOCAL __declspec(thread)
#elif defined(__clang__) || defined(__GNUC__)
#define H_TRACE_THREAD_LOCAL __thread
#else
#define H_TRACE_THREAD_LOCAL
#endif

/* Runtime on/off switch (compile gate is HAMMER_TRACE_AST above). Defaults OFF
 * so an ordinary h_parse() stays quiet even when tracing is compiled in; only
 * h_parse_debug() turns it on -- via h_trace_set_enabled() -- for the duration
 * of a single parse. */
H_TRACE_THREAD_LOCAL bool display_trace = false;
static H_TRACE_THREAD_LOCAL unsigned trace_enable_depth = 0;

/* Toggle the runtime trace. Exposed (see trace.h) so h_parse_debug() can enable
 * tracing for just its own call and switch it back off afterward. */
void h_trace_set_enabled(bool enabled) {
    if (enabled)
        trace_enable_depth++;
    else if (trace_enable_depth > 0)
        trace_enable_depth--;
    display_trace = trace_enable_depth > 0;
}

typedef struct HTraceFrame_ {
    const HParser *parser;
    const char *name;
    size_t start;
    uint8_t start_bit;
    unsigned long failure_serial;
} HTraceFrame;

#define H_TRACE_MAX_FRAMES 256
typedef struct HTraceContext_ {
    const uint8_t *input;
    size_t input_len;
    size_t depth;
    size_t frame_count;
    size_t overflow_frames;
    unsigned long failure_serial;
    HTraceFrame frames[H_TRACE_MAX_FRAMES];
    HParseError error;
    HParserBackend backend;
    uint8_t *expected_bytes;
    struct HTraceContext_ *parent;
} HTraceContext;

static H_TRACE_THREAD_LOCAL HTraceContext *trace_context;
static H_TRACE_THREAD_LOCAL HParseError trace_completed;

/* Copy the current furthest-failure record out to the caller (see trace.h).
 * out receives its own copy of the struct -- not a pointer into the global --
 * so it stays valid across later parses that overwrite trace_max. The copied
 * deepest_parsers[] entries still point into the tracer's own long-lived name
 * cache, so this shallow copy is safe and needs no ownership transfer. */
void h_trace_get_error(HParseError *out) {
    if (!out)
        return;
    memcpy(out, &trace_completed, sizeof(*out));
    if (out->parser)
        out->parser = strdup(out->parser);
    for (size_t i = 0; i < out->n_deepest; i++)
        out->deepest_parsers[i] = strdup(out->deepest_parsers[i]);
    for (size_t i = 0; i < out->n_context; i++)
        out->context[i] = strdup(out->context[i]);
}

static const char *trace_tt_name(HTokenType t) {
    switch (t) {
    case TT_NONE:
        return "NONE";
    case TT_BYTES:
        return "BYTES";
    case TT_SINT:
        return "SINT";
    case TT_UINT:
        return "UINT";
    case TT_DOUBLE:
        return "DOUBLE";
    case TT_FLOAT:
        return "FLOAT";
    case TT_SEQUENCE:
        return "SEQUENCE";
    case TT_ERR:
        return "ERR";
    default:
        return (t >= TT_USER) ? "USER" : "INVALID";
    }
}

static void trace_indent(void) {
    size_t depth = trace_context ? trace_context->depth : 0;
    for (size_t i = 0; i < depth; i++)
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

/* changes the name from parse_<name> to h_<name> */
static char *fn_name_to_h(const char *name) {
    if (!name) {
        return NULL;
    }

    size_t len = strlen(name);
    const char *remainder = (len > 5) ? name + 5 : "";

    size_t rlen = strlen(remainder);

    /* allocate: 2 chars + remainder + '\0' */
    char *hname = malloc(rlen + 2);
    if (!hname)
        return NULL;

    hname[0] = 'h';

    memcpy(hname + 1, remainder, rlen + 1); /* copy including NUL */

    return hname;
}

static H_TRACE_THREAD_LOCAL struct {
    const HParserVtable *vt;
    const char *name;
} h_namecache[64];
static H_TRACE_THREAD_LOCAL size_t h_namecache_len = 0;

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
 * silently capping at H_PARSE_ERROR_MAX_PARSERS entries. */
static void trace_error_add_parser(HParseError *error, const char *name) {
    for (size_t i = 0; i < error->n_deepest; i++)
        if (error->deepest_parsers[i] == name)
            return;
    if (error->n_deepest < H_PARSE_ERROR_MAX_PARSERS)
        error->deepest_parsers[error->n_deepest++] = name;
}

static void trace_pos(HParseState *state) {
    HInputStream *in = &state->input_stream;
    size_t abs = in->pos + in->index;
    fprintf(stderr, "@%zu", abs);
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
        fprintf(stderr, "UINT %" PRIu64 " (0x%02" PRIx64 ")", tok->token_data.uint,
                tok->token_data.uint);
        break;
    case TT_SINT:
        fprintf(stderr, "SINT %" PRId64, tok->token_data.sint);
        break;
    case TT_BYTES:
        fprintf(stderr, "BYTES[%zu]", tok->token_data.bytes.len);
        break;
    case TT_SEQUENCE:
        fprintf(stderr, "SEQUENCE[%zu children]",
                tok->token_data.seq ? tok->token_data.seq->used : (size_t)0);
        break;
    default:
        fputs(trace_tt_name(tok->token_type), stderr);
        break;
    }
}

#define BYTES_PER_LINE 16
void h_trace_file_context(const uint8_t *input, size_t length, size_t highlight_index) {
    const char *color_red = "\x1b[31m";
    const char *color_reset = "\x1b[0m";
    int use_color = ISATTY(stderr);

    if (!display_trace)
        return;
    fprintf(stderr, "=== h_packrat_parse: input context (%zu bytes) ===\n", length);
    /*for (size_t i = 0; i < length; i++) {
        uint8_t c = input[i];
        char disp[2] = { isprint(c) ? (char)c : '\0', '\0' };
        fprintf(stderr, "%02x %s\n", c, disp);
    }*/
    for (size_t off = 0; off < length; off += BYTES_PER_LINE) {
        size_t line_len = (length - off < BYTES_PER_LINE) ? (length - off) : BYTES_PER_LINE;
        fprintf(stderr, "%04zx:  ", off);

        /* Hex bytes, grouped by 4 for readability */
        for (size_t i = 0; i < BYTES_PER_LINE; ++i) {
            size_t idx = off + i;
            if (i < line_len) {
                int is_highlight = (idx == highlight_index);
                if (use_color && is_highlight)
                    fputs(color_red, stderr);
                fprintf(stderr, "%02x", input[idx]);
                if (use_color && is_highlight)
                    fputs(color_reset, stderr);
            } else {
                fputs("  ", stderr);
            }
            if ((i & 3) == 3)
                fputs("  ", stderr);
            else
                fputc(' ', stderr);
        }

        /* ASCII column */
        fputc(' ', stderr);
        for (size_t i = 0; i < line_len; ++i) {
            size_t idx = off + i;
            uint8_t c = input[idx];
            int is_highlight = (idx == highlight_index);
            if (use_color && is_highlight)
                fputs(color_red, stderr);
            fputc(isprint(c) ? (char)c : '.', stderr);
            if (use_color && is_highlight)
                fputs(color_reset, stderr);
        }
        fprintf(stderr, "\n");
    }
}

void h_trace_begin(const uint8_t *input, size_t input_len) {
    if (!display_trace)
        return;
    HTraceContext *context = calloc(1, sizeof(*context));
    if (!context)
        return;
    context->input = input;
    context->input_len = input_len;
    context->parent = trace_context;
    trace_context = context;
    fprintf(stderr, "\n=== h_packrat_parse: begin (%zu bytes of input) ===\n", input_len);
}

void h_trace_enter(const HParser *parser, HParseState *state) {
    if (!display_trace)
        return;
    trace_indent();
    fprintf(stderr, "-> %-20s %-9s ", fn_name_to_h(trace_vt_name(parser->vtable)),
            parser->vtable->higher ? "higher" : "primitive");
    trace_pos(state);
    fputc('\n', stderr);
    if (trace_context && trace_context->frame_count < H_TRACE_MAX_FRAMES) {
        HTraceFrame *frame = &trace_context->frames[trace_context->frame_count];
        frame->parser = parser;
        frame->name = trace_vt_name(parser->vtable);
        frame->start = state->input_stream.pos + state->input_stream.index;
        frame->start_bit = state->input_stream.bit_offset;
        frame->failure_serial = trace_context->failure_serial;
        trace_context->frame_count++;
    } else if (trace_context) {
        trace_context->overflow_frames++;
    }
    if (trace_context)
        trace_context->depth++;
}

static HParseErrorKind trace_failure_kind(const HParser *parser, const char *name, size_t index,
                                          size_t length) {
    if (strcmp(name, "parse_attr_bool") == 0)
        return H_PARSE_ERROR_SEMANTIC_PREDICATE;
    if (strcmp(name, "parse_int_range") == 0)
        return H_PARSE_ERROR_RANGE;
    if (index >= length)
        return H_PARSE_ERROR_UNEXPECTED_EOF;
    return parser->vtable->higher ? H_PARSE_ERROR_HIGHER_ORDER : H_PARSE_ERROR_PRIMITIVE_MISMATCH;
}

static void trace_record_failure(const HParser *parser, HParseState *state,
                                 const HTraceFrame *frame) {
    HTraceContext *context = trace_context;
    HParseError *error = &context->error;
    size_t end = state->input_stream.pos + state->input_stream.index;
    size_t index = parser->vtable->higher ? end : frame->start;
    HParseErrorKind kind = trace_failure_kind(parser, frame->name, index, context->input_len);
    bool replace = error->kind == H_PARSE_ERROR_NONE || index > error->index ||
                   (index == error->index && parser->vtable->higher &&
                    error->kind == H_PARSE_ERROR_PRIMITIVE_MISMATCH);

    if (replace) {
        memset(error, 0, sizeof(*error));
        error->index = index;
        error->end_index = end;
        error->bit_offset =
            parser->vtable->higher ? state->input_stream.bit_offset : frame->start_bit;
        error->kind = kind;
        error->parser = frame->name;
        if (index < context->input_len) {
            error->actual = context->input[index];
            error->has_actual = true;
        }
        trace_error_add_parser(error, frame->name);
        for (size_t i = context->frame_count; i > 0 && error->n_context < H_PARSE_ERROR_MAX_PARSERS;
             i--)
            error->context[error->n_context++] = context->frames[i - 1].name;
    } else if (index == error->index && kind == error->kind) {
        trace_error_add_parser(error, frame->name);
    }
}

void h_trace_exit(const HParser *parser, HParseState *state, HParseResult *res, const char *note) {
    if (!display_trace)
        return;
    HTraceFrame frame = {0};
    bool have_frame =
        trace_context && trace_context->frame_count > 0 && trace_context->overflow_frames == 0;
    if (trace_context && trace_context->depth > 0)
        trace_context->depth--;
    if (trace_context && trace_context->overflow_frames > 0) {
        trace_context->overflow_frames--;
    }
    if (have_frame) {
        frame = trace_context->frames[trace_context->frame_count - 1];
        trace_context->frame_count--;
    }
    if (!res && have_frame) {
        bool originated_here = frame.failure_serial == trace_context->failure_serial;
        trace_context->failure_serial++;
        if (originated_here)
            trace_record_failure(parser, state, &frame);
    }
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

    HTraceContext *context = trace_context;
    if (!context)
        return;

    /*
    typedef struct HParseError_ {
        size_t index;       /< Furthest byte offset reached in the input.
        size_t end_index;   /< End position for a failure after consuming input.
        uint8_t actual;     /< Input byte at that offset (0 at end of input).
        bool has_actual;    /< Whether actual contains an input byte.
        uint8_t bit_offset; /< Sub-byte bit position, for bitwise grammars.
        HParseErrorKind kind;
        const char *parser;
        /Names of originating parsers tied at the selected failure position.
        const char *deepest_parsers[H_PARSE_ERROR_MAX_PARSERS];
        size_t n_deepest;   /< Number of valid entries in deepest_parsers.
        const char *context[H_PARSE_ERROR_MAX_PARSERS];
        size_t n_context;
    } HParseError;
    */
    HParseError *error = &context->error;
    if (!res && error->kind != H_PARSE_ERROR_NONE) {
        if (error->kind == H_PARSE_ERROR_SEMANTIC_PREDICATE) {
            fprintf(stdout, "error: semantic predicate failed");
        } else if (error->kind == H_PARSE_ERROR_RANGE) {
            fprintf(stdout, "error: integer outside permitted range");
        } else if (error->has_actual) {
            uint8_t c = error->actual;
            char disp[2] = {isprint(c) ? (char)c : '\0', '\0'};
            // Data Type Parsers require error message, byte value doesn't matter only length.
            if(strcmp(error->parser, "parse_bits") == 0 )
                fprintf(stdout, "error: ran out of bits to parse");
            else if(strcmp(error->parser, "parse_bytes") == 0)
                fprintf(stdout, "error: ran out of bytes to parse");
            else if(strcmp(error->parser, "parse_xor") == 0) // Filtering parsers require specific error messages
                fprintf(stdout, "error: both or neither parser passed");
            else if(strcmp(error->parser, "parse_difference") == 0) // if p1 fails it won't reach this error message
                fprintf(stdout, "error: p2's result is not shorter than p1's result");
            else if(strcmp(error->parser, "parse_butnot") == 0) // if p1 fails it won't reach this error message
                fprintf(stdout, "error: p1's result is shorter than p2's result");
            else if(strcmp(error->parser, "parse_not") == 0) // parse_not fails on a successful parse
                fprintf(stdout, "error: inner parse succeeded at byte(s):'%s' (0x%02x = %d)", disp, c, c);
            else if(strcmp(error->parser, "parse_nothing") == 0) // h_nothing_p always fails
                fprintf(stdout, "Always fail reached");
            else if (error->kind == H_PARSE_ERROR_PRIMITIVE_MISMATCH)
                fprintf(stdout, "error: unexpected byte(s): '%s' (0x%02x = %d)\n", disp, c, c);
            else
                fprintf(stdout, "error: %s failed at byte(s): '%s' (0x%02x = %d)",
                        fn_name_to_h(error->parser), disp, c, c);
        } else {
            // Sequential Parsers will reach this error message
            fprintf(stdout, "error: unexpected end of input");
        }

        fprintf(stdout, " starting at index %zu", error->index);

        if (error->bit_offset)
            fprintf(stdout, ".%db", error->bit_offset);

        if (error->n_deepest > 0) {
            fputs(" while running [", stdout);
            for (size_t i = 0; i < error->n_deepest; i++)
                fprintf(stdout, "%s%s", i ? ", " : "", fn_name_to_h(error->deepest_parsers[i]));
            fputc(']', stdout);
        }
        fprintf(stdout, "\n");
        h_trace_file_context(context->input, context->input_len, error->index);
    }

    HTraceContext *parent = context->parent;
    trace_completed = context->error;
    trace_context = parent;
    free(context);
}

// regex tracing
char *getsym(HSVMActionFunc addr) {
    if (!display_trace)
        return NULL;
    char *retstr;
    if (h_platform_asprintf(&retstr, "%p", addr) > 0)
        return retstr;
    else
        return NULL;
}

const char *rvm_op_names[RVM_OPCOUNT] = {"ACCEPT",  "GOTO", "FORK",  "PUSH", "ACTION",
                                         "CAPTURE", "EOF",  "MATCH", "STEP"};

const char *svm_op_names[SVM_OPCOUNT] = {"PUSH", "NOP", "ACTION", "CAPTURE", "ACCEPT"};

void dump_rvm_prog(HRVMProg *prog) {
    if (!display_trace)
        return;
    char *symref;
    for (unsigned int i = 0; i < prog->length; i++) {
        HRVMInsn *insn = &prog->insns[i];
        printf("%4d %-10s", i, rvm_op_names[insn->op]);
        switch (insn->op) {
        case RVM_GOTO:
        case RVM_FORK:
            printf("%hd\n", insn->arg);
            break;
        case RVM_ACTION:
            symref = getsym(prog->actions[insn->arg].action);
            printf("%s env=%p\n", symref, prog->actions[insn->arg].env);
            (&system_allocator)->free(&system_allocator, symref);
            break;
        case RVM_MATCH: {
            uint8_t low, high;
            low = insn->arg & 0xff;
            high = (insn->arg >> 8) & 0xff;
            if (high < low)
                printf("NONE\n");
            else {
                if (low >= 0x20 && low <= 0x7e)
                    printf("%02hhx ('%c')", low, low);
                else
                    printf("%02hhx", low);

                if (high >= 0x20 && high <= 0x7e)
                    printf(" - %02hhx ('%c')\n", high, high);
                else
                    printf(" - %02hhx\n", high);
            }
            break;
        }
        default:
            printf("\n");
        }
    }
}

void dump_svm_prog(HRVMProg *prog, HRVMTrace *trace) {
    if (!display_trace)
        return;
    char *symref;
    for (; trace != NULL; trace = trace->next) {
        printf("@%04zd %-10s", trace->input_pos, svm_op_names[trace->opcode]);
        switch (trace->opcode) {
        case SVM_ACTION:
            symref = getsym(prog->actions[trace->arg].action);
            printf("%s env=%p\n", symref, prog->actions[trace->arg].env);
            (&system_allocator)->free(&system_allocator, symref);
            break;
        default:
            printf("\n");
        }
    }
}

static void trace_print_expected_byte(uint8_t c) {
    if (c == '\'' || c == '\\')
        fprintf(stderr, "'\\%c'", c);
    else if (isprint(c))
        fprintf(stderr, "'%c'", c);
    else
        fprintf(stderr, "0x%02x", c);
}

static void trace_print_expectations(const bool expected[256], bool expected_eof) {
    bool first = true;

    fputs("; expected ", stderr);
    for (unsigned int lo = 0; lo < 256;) {
        if (!expected[lo]) {
            lo++;
            continue;
        }

        unsigned int hi = lo;
        while (hi + 1 < 256 && expected[hi + 1])
            hi++;

        if (!first)
            fputs(", ", stderr);
        trace_print_expected_byte((uint8_t)lo);
        if (hi != lo) {
            fputc('-', stderr);
            trace_print_expected_byte((uint8_t)hi);
        }
        first = false;
        lo = hi + 1;
    }

    if (expected_eof) {
        if (!first)
            fputs(", ", stderr);
        fputs("end of input", stderr);
        first = false;
    }
    if (first)
        fputs("a valid input byte", stderr);
}

void rvm_match_error(HRVMProg *prog, const uint8_t *input, size_t input_len, size_t index,
                     const bool expected[256], bool expected_eof) {
    if (!display_trace)
        return;
    if(true) // later change this to a trace dump debug flag
        dump_rvm_prog(prog);
    if (index >= input_len) {
        fprintf(stderr, "error: unexpected end of input at index %zu", index);
    } else {
        uint8_t ch = input[index];
        fprintf(stderr, "error: unexpected byte ");
        trace_print_expected_byte(ch);
        fprintf(stderr, " (0x%02x = %u) at index %zu", ch, ch, index);
    }
    trace_print_expectations(expected, expected_eof);
    fputc('\n', stderr);
    
    if (input && input_len > 0)
        h_trace_file_context(input, input_len, index);
}
void svm_action_error(HSVMContext *ctx, HRVMProg *orig_prog, HRVMTrace *trace,
                        const uint8_t *input, size_t input_len, const char *msg) {
    if (!display_trace)
        return;
    if(true) // later change this to a trace dump debug flag
        dump_svm_prog(orig_prog, trace);
    if(ctx->input_pos >= input_len)
        fprintf(stderr, "error: %s ran out of bytes to parse at index %zu\n", msg, ctx->input_pos);
    else
        fprintf(stderr, "error: %s failed at index %zu: ch=%02x\n", msg, ctx->input_pos, input[ctx->input_pos]);
    if (input && input_len > 0)
        h_trace_file_context(input, input_len, ctx->input_pos);
}

void svm_failure_error(HSVMContext *ctx, HRVMProg *orig_prog, HRVMTrace *trace,
                       const uint8_t *input, size_t input_len) {
    if (!display_trace)
        return;
    if (true) // later change this to a trace dump debug flag
        dump_svm_prog(orig_prog, trace);

    switch (ctx->failure.kind) {
    case SVM_FAILURE_RANGE:
        fputs("error: integer ", stderr);
        if (ctx->failure.actual_type == TT_SINT)
            fprintf(stderr, "%" PRId64, ctx->failure.actual.sint);
        else
            fprintf(stderr, "%" PRIu64, ctx->failure.actual.uint);
        fprintf(stderr, " outside permitted range [%" PRId64 ", %" PRId64 "]",
                ctx->failure.lower, ctx->failure.upper);
        break;
    case SVM_FAILURE_NONE:
    default:
        fputs("error: SVM action failed", stderr);
        break;
    }

    fprintf(stderr, " starting at index %zu", ctx->failure.start);
    if (ctx->failure.end > ctx->failure.start)
        fprintf(stderr, " and ending at index %zu", ctx->failure.end);
    if (ctx->failure.parser)
        fprintf(stderr, " while running [%s]", ctx->failure.parser);
    fputc('\n', stderr);

    if (input && input_len > 0)
        h_trace_file_context(input, input_len, ctx->failure.start);
}
#endif /* HAMMER_TRACE_AST */
