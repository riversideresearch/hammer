/* Copyright (c) 2026 Riverside Research */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* dladdr(), strdup() used by the AST tracer below */
#include "hammer.h"
#endif

#include "backends/regex.h"
#include "internal.h"
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
H_TRACE_THREAD_LOCAL bool dump_trace = false;
H_TRACE_THREAD_LOCAL bool dump_input = false;
static H_TRACE_THREAD_LOCAL unsigned trace_enable_depth = 0;
static H_TRACE_THREAD_LOCAL HParseDiagnostic trace_completed;
#define H_TRACE_MAX_NESTING 256
static H_TRACE_THREAD_LOCAL bool trace_dump_stack[H_TRACE_MAX_NESTING];

/* Toggle the runtime trace. Exposed (see trace.h) so h_parse_debug() can enable
 * tracing for just its own call and switch it back off afterward. */
void h_trace_set_enabled(bool enabled, bool dumpExecutionTrace, bool dumpInputContext) {
    if (enabled) {
        if (trace_enable_depth == 0)
            memset(&trace_completed, 0, sizeof(trace_completed));
        if (trace_enable_depth < H_TRACE_MAX_NESTING)
            trace_dump_stack[trace_enable_depth] = dumpExecutionTrace;
        trace_enable_depth++;
    } else if (trace_enable_depth > 0) {
        trace_enable_depth--;
    }
    display_trace = trace_enable_depth > 0;
    dump_trace = display_trace && trace_enable_depth <= H_TRACE_MAX_NESTING
                     ? trace_dump_stack[trace_enable_depth - 1]
                     : dumpExecutionTrace;
    
    dump_input = display_trace && trace_enable_depth <= H_TRACE_MAX_NESTING
                     ? trace_dump_stack[trace_enable_depth - 1]
                     : dumpInputContext;
}

bool h_trace_is_enabled(void) { return display_trace; }
bool h_trace_is_dump_enabled(void) { return display_trace && dump_trace; }
bool h_trace_is_input_enabled(void) { return display_trace && dump_input; }

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
    const HParser *root_parser;
    const HParser *failure_parser;
    bool expected_bytes[256];
    bool expected_eof;
    HTraceNumericRange numeric_range;
    HTraceNumericRange pending_numeric_range;
    size_t next_branch_id;
    struct HTraceContext_ *parent;
} HTraceContext;

static H_TRACE_THREAD_LOCAL HTraceContext *trace_context;

static void trace_complete(const HTraceContext *context) {
    memset(&trace_completed, 0, sizeof(trace_completed));
    if (!context)
        return;
    trace_completed.error = context->error;
    memcpy(trace_completed.expected_bytes, context->expected_bytes,
           sizeof(trace_completed.expected_bytes));
    trace_completed.expected_eof = context->expected_eof;
    trace_completed.numeric_range = context->numeric_range;
}

/* Copy the current furthest-failure record out to the caller (see trace.h).
 * out receives its own copy of the struct -- not a pointer into the global --
 * so it stays valid across later parses that overwrite trace_max. The copied
 * deepest_parsers[] entries still point into the tracer's own long-lived name
 * cache, so this shallow copy is safe and needs no ownership transfer. */
static void trace_copy_error(HParseError *out, const HParseError *source) {
    memcpy(out, source, sizeof(*out));
    if (out->parser)
        out->parser = strdup(out->parser);
    if (out->message)
        out->message = strdup(out->message);
    for (size_t i = 0; i < out->n_deepest; i++)
        out->deepest_parsers[i] = strdup(out->deepest_parsers[i]);
    for (size_t i = 0; i < out->n_context; i++)
        out->context[i] = strdup(out->context[i]);
    out->source = NULL;
    if (source->source) {
        HSourceLocation *source_copy = calloc(1, sizeof(*source_copy));
        if (source_copy) {
            source_copy->line = source->source->line;
            source_copy->column = source->source->column;
            if (source->source->file_name)
                source_copy->file_name = strdup(source->source->file_name);
            if (source->source->function_name)
                source_copy->function_name = strdup(source->source->function_name);
            if ((!source->source->file_name || source_copy->file_name) &&
                (!source->source->function_name || source_copy->function_name)) {
                out->source = source_copy;
            } else {
                free((void *)source_copy->file_name);
                free((void *)source_copy->function_name);
                free(source_copy);
            }
        }
    }
}

void h_trace_get_error(HParseError *out) {
    if (!out)
        return;
    trace_copy_error(out, &trace_completed.error);
}

void h_trace_get_diagnostic(HParseDiagnostic **out) {
    if (!out)
        return;
    *out = calloc(1, sizeof(**out));
    if (!*out)
        return;
    trace_copy_error(&(*out)->error, &trace_completed.error);
    memcpy((*out)->expected_bytes, trace_completed.expected_bytes, sizeof((*out)->expected_bytes));
    (*out)->expected_eof = trace_completed.expected_eof;
    (*out)->numeric_range = trace_completed.numeric_range;
}

void h_trace_note_int_range(const HParsedToken *token, int64_t lower, int64_t upper) {
    if (!display_trace || !trace_context || !token)
        return;
    HTraceNumericRange *range = &trace_context->pending_numeric_range;
    memset(range, 0, sizeof(*range));
    if (token->token_type == TT_SINT) {
        range->kind = H_TRACE_NUMERIC_RANGE_SINT;
        range->actual.sint = token->token_data.sint;
    } else if (token->token_type == TT_UINT) {
        range->kind = H_TRACE_NUMERIC_RANGE_UINT;
        range->actual.uint = token->token_data.uint;
    } else {
        return;
    }
    range->expected.integer.lower = lower;
    range->expected.integer.upper = upper;
}

void h_trace_note_float_range(const HParsedToken *token, double lower, double upper) {
    if (!display_trace || !trace_context || !token)
        return;
    HTraceNumericRange *range = &trace_context->pending_numeric_range;
    memset(range, 0, sizeof(*range));
    if (token->token_type == TT_FLOAT)
        range->actual.floating = token->token_data.flt;
    else if (token->token_type == TT_DOUBLE)
        range->actual.floating = token->token_data.dbl;
    else
        return;
    range->kind = H_TRACE_NUMERIC_RANGE_FLOAT;
    range->expected.floating.lower = lower;
    range->expected.floating.upper = upper;
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

static const char *trace_parser_diagnostic_name(const HParser *parser) {
    if (!parser)
        return "?(no parser)";
    if (parser->diagnostic_label)
        return parser->diagnostic_label;
    return parser->vtable ? trace_vt_name(parser->vtable) : "?(no parser)";
}

char *h_trace_parser_name(const HParser *parser) {
    if (!parser || !parser->vtable)
        return NULL;

    const char *name = trace_parser_diagnostic_name(parser);
    if (strncmp(name, "parse_", 6) == 0)
        return fn_name_to_h(name);
    return strdup(name);
}

/* Append a parser name to the deepest-position set, skipping duplicates and
 * silently capping at H_PARSE_ERROR_MAX_PARSERS entries. */
static void trace_error_add_parser(HParseError *error, const char *name) {
    for (size_t i = 0; i < error->n_deepest; i++)
        if (error->deepest_parsers[i] == name ||
            (error->deepest_parsers[i] && name && strcmp(error->deepest_parsers[i], name) == 0))
            return;
    if (error->n_deepest < H_PARSE_ERROR_MAX_PARSERS)
        error->deepest_parsers[error->n_deepest++] = name;
}

static void trace_pos(HParseState *state) {
    HInputStream *in = &state->input_stream;
    size_t abs = in->pos + in->index;
    if (!dump_trace)
        return;
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
void h_trace_file_context(const uint8_t *input, size_t length, size_t start_index,
                          size_t end_index) {
    const char *color_red = "\x1b[31m";
    const char *color_reset = "\x1b[0m";
    int use_color = ISATTY(stderr);

    if (!display_trace)
        return;
    const char *backend_name = "h_parse_debug";
    if (trace_context) {
        switch (trace_context->backend) {
        case PB_PACKRAT:
            backend_name = "h_packrat_parse";
            break;
        case PB_LL:
            backend_name = "h_llk_parse";
            break;
        case PB_LALR:
            backend_name = "h_lalr_parse";
            break;
        case PB_GLR:
            backend_name = "h_glr_parse";
            break;
        default:
            break;
        }
    }
    fprintf(stderr, "=== %s: input context (%zu bytes) ===\n", backend_name, length);
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
                int is_highlight = (idx >= start_index && idx <= end_index);
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
            int is_highlight = (idx >= start_index && idx <= end_index);
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
    context->backend = PB_PACKRAT;
    context->parent = trace_context;
    trace_context = context;
    fprintf(stderr, "\n=== h_packrat_parse: begin (%zu bytes of input) ===\n", input_len);
}

void h_trace_enter(const HParser *parser, HParseState *state) {
    if (!display_trace)
        return;
    if (dump_trace) {
        char *parser_name = h_trace_parser_name(parser);
        trace_indent();
        fprintf(stderr, "-> %-20s %-9s ", parser_name ? parser_name : "?(no parser)",
                parser->vtable->higher ? "higher" : "primitive");
        trace_pos(state);
        fputc('\n', stderr);
        free(parser_name);
    }
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
    if (h_is_nothing_parser(
            parser)) // same functionality as strcmp name but works in stripped builds
        return H_PARSE_ERROR_EXPLICIT_FAILURE;
    if (h_is_get_value_parser(parser))
        return H_PARSE_ERROR_NO_VALUE;
    if (h_is_xor_parser(parser))
        return H_PARSE_ERROR_XOR;
    if (h_is_difference_parser(parser))
        return H_PARSE_ERROR_DIFFERENCE;
    if (h_is_butnot_parser(parser))
        return H_PARSE_ERROR_BUTNOT;
    if (h_is_attr_bool_parser(parser))
        return H_PARSE_ERROR_SEMANTIC_PREDICATE;
    if (h_is_int_range_parser(parser) || h_is_float_range_parser(parser))
        return H_PARSE_ERROR_RANGE;
    if (index >= length)
        return H_PARSE_ERROR_UNEXPECTED_EOF;
    return parser->vtable->higher ? H_PARSE_ERROR_HIGHER_ORDER : H_PARSE_ERROR_PRIMITIVE_MISMATCH;
}

/* Syntax failures identify the input position that could not be matched.
 * Failures raised after parsing a value identify a consumed half-open span,
 * but still compete as furthest failures using the end of that span. */
static size_t trace_failure_progress(HParseErrorKind kind, size_t start, size_t end) {
    switch (kind) {
    case H_PARSE_ERROR_SEMANTIC_PREDICATE:
    case H_PARSE_ERROR_RANGE:
    case H_PARSE_ERROR_ACTION:
    case H_PARSE_ERROR_HIGHER_ORDER:
    case H_PARSE_ERROR_XOR:
    case H_PARSE_ERROR_DIFFERENCE:
    case H_PARSE_ERROR_BUTNOT:
    case H_PARSE_ERROR_NO_VALUE:
        return end;
    default:
        return start;
    }
}

static unsigned int trace_error_priority(HParseErrorKind kind);

static size_t trace_collect_expectations(const HParser *parser, const HTraceFrame *frame,
                                         size_t end, bool overrun, bool expected[256],
                                         bool *expected_eof) {
    memset(expected, 0, 256 * sizeof(*expected));
    *expected_eof = false;
    if (!parser || !parser->vtable || !parser->vtable->trace_expectations)
        return 0;
    size_t consumed = end >= frame->start ? end - frame->start : 0;
    return parser->vtable->trace_expectations(parser->env, consumed, overrun, expected,
                                              expected_eof);
}

static void trace_record_failure(const HParser *parser, HParseState *state,
                                 const HTraceFrame *frame) {
    HTraceContext *context = trace_context;
    HParseError *error = &context->error;
    size_t end = state->input_stream.pos + state->input_stream.index;
    bool expected[256];
    bool expected_eof;
    size_t failure_offset = trace_collect_expectations(
        parser, frame, end, state->input_stream.overrun, expected, &expected_eof);
    size_t index = parser->vtable->higher ? end : frame->start + failure_offset;
    HParseErrorKind kind = trace_failure_kind(parser, frame->name, index, context->input_len);
    HTraceNumericRange numeric_range = context->pending_numeric_range;
    memset(&context->pending_numeric_range, 0, sizeof(context->pending_numeric_range));
    bool value_failure = kind == H_PARSE_ERROR_SEMANTIC_PREDICATE || kind == H_PARSE_ERROR_RANGE ||
                         kind == H_PARSE_ERROR_XOR || kind == H_PARSE_ERROR_DIFFERENCE ||
                         kind == H_PARSE_ERROR_BUTNOT || kind == H_PARSE_ERROR_NO_VALUE;

    const HParser *label_parser = parser && parser->diagnostic_label ? parser : NULL;
    const HParser *message_parser = parser && parser->diagnostic_message ? parser : NULL;
    const HParser *source_parser = parser && parser->diagnostic_source ? parser : NULL;
    for (size_t i = context->frame_count;
         i > 0 && (!label_parser || !message_parser || !source_parser); i--) {
        const HParser *candidate = context->frames[i - 1].parser;
        if (!candidate)
            continue;
        if (!label_parser && candidate->diagnostic_label)
            label_parser = candidate;
        if (!message_parser && candidate->diagnostic_message)
            message_parser = candidate;
        if (!source_parser && candidate->diagnostic_source)
            source_parser = candidate;
    }
    const char *failure_name =
        label_parser ? label_parser->diagnostic_label : trace_parser_diagnostic_name(parser);
    const char *failure_message = message_parser ? message_parser->diagnostic_message : NULL;

    if (value_failure)
        index = frame->start;

    size_t progress = trace_failure_progress(kind, index, end);
    size_t previous_progress = trace_failure_progress(error->kind, error->index, error->end_index);
    bool replace = error->kind == H_PARSE_ERROR_NONE || progress > previous_progress ||
                   (progress == previous_progress && failure_message && !error->message) ||
                   (progress == previous_progress && !!failure_message == !!error->message &&
                    trace_error_priority(kind) > trace_error_priority(error->kind));

    if (replace) {
        memset(error, 0, sizeof(*error));
        memset(context->expected_bytes, 0, sizeof(context->expected_bytes));
        context->expected_eof = false;
        memset(&context->numeric_range, 0, sizeof(context->numeric_range));
        if (kind == H_PARSE_ERROR_RANGE)
            context->numeric_range = numeric_range;
        context->failure_parser = label_parser ? label_parser : parser;
        error->index = index;
        error->end_index = end;
        error->bit_offset = value_failure || !parser->vtable->higher
                                ? frame->start_bit
                                : state->input_stream.bit_offset;
        error->kind = kind;
        error->parser = failure_name;
        error->message = failure_message;
        error->source = source_parser ? source_parser->diagnostic_source : NULL;
        if (kind != H_PARSE_ERROR_EXPLICIT_FAILURE && index < context->input_len) {
            error->actual = context->input[index];
            error->has_actual = true;
        }
        trace_error_add_parser(error, failure_name);
        for (size_t i = context->frame_count; i > 0 && error->n_context < H_PARSE_ERROR_MAX_PARSERS;
             i--) {
            const HTraceFrame *context_frame = &context->frames[i - 1];
            error->context[error->n_context++] =
                trace_parser_diagnostic_name(context_frame->parser);
        }
        memcpy(context->expected_bytes, expected, sizeof(context->expected_bytes));
        context->expected_eof = expected_eof;
    } else if (progress == previous_progress && kind == error->kind &&
               !!failure_message == !!error->message) {
        trace_error_add_parser(error, failure_name);
        for (size_t i = 0; i < 256; i++)
            context->expected_bytes[i] |= expected[i];
        context->expected_eof |= expected_eof;
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
    if (!dump_trace)
        return;
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

static void trace_render_diagnostic(HTraceContext *context);

void h_trace_end(HParseResult *res, HParseState *state) {
    if (!display_trace)
        return;

    fprintf(stderr, "=== h_packrat_parse: end (%s) ===\n", res ? "SUCCESS" : "FAILURE");

    HTraceContext *context = trace_context;
    if (!context)
        return;

    if (!res)
        trace_render_diagnostic(context);

    HTraceContext *parent = context->parent;
    trace_complete(context);
    trace_context = parent;
    free(context);
}

// regex tracing
char *getsym(HSVMActionFunc addr) {
    if (!display_trace)
        return NULL;
    return resolve_fn_name(*(void *const *)&addr);
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
        char *parser_name = h_trace_parser_name(prog->insn_parsers[i]);
        switch (insn->op) {
        case RVM_PUSH:
            if (parser_name) {
                printf(" parser=%s", parser_name);
                free(parser_name);
            }
            break;
        case RVM_GOTO:
        case RVM_FORK:
            printf("%hd", insn->arg);
            break;
        case RVM_ACTION:
            symref = getsym(prog->actions[insn->arg].action);
            printf(" action=%s in", symref ? symref : "<unknown>");
            free(symref);
            if (parser_name) {
                printf(" parser=%s", parser_name);
                free(parser_name);
            }
            break;
        case RVM_MATCH: {
            uint8_t low, high;
            low = insn->arg & 0xff;
            high = (insn->arg >> 8) & 0xff;
            if (high < low)
                printf("NONE");
            else {
                if (low >= 0x20 && low <= 0x7e)
                    printf("%02hhx ('%c')", low, low);
                else
                    printf("%02hhx", low);

                if (high >= 0x20 && high <= 0x7e)
                    printf(" - %02hhx ('%c')", high, high);
                else
                    printf(" - %02hhx", high);
            }
            break;
        }
        default:
            break;
        }
        printf("\n");
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
            printf(" action=%s in", symref ? symref : "<unknown>");
            free(symref);
            break;
        default:
            break;
        }

        char *parser_name = h_trace_parser_name(trace->parser);
        if (parser_name) {
            printf(" parser=%s", parser_name);
            free(parser_name);
        }
        printf("\n");
    }
}

static void trace_print_parser_context(const HParser *parser) {
    char *parser_name = h_trace_parser_name(parser);
    if (!parser_name)
        return;
    fprintf(stderr, " while running [%s]", parser_name);
    free(parser_name);
}

static void trace_print_error_parsers(const HParseError *error) {
    if (!error || error->n_deepest == 0)
        return;

    fputs(" while running [", stderr);
    for (size_t i = 0; i < error->n_deepest; i++) {
        const char *raw = error->deepest_parsers[i];
        char *name = strncmp(raw, "parse_", 6) == 0 ? fn_name_to_h(raw) : strdup(raw);
        fprintf(stderr, "%s%s", i ? ", " : "", name ? name : raw);
        free(name);
    }
    fputc(']', stderr);
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

/* One normalized summary formatter shared by every traced backend. */
static void trace_render_diagnostic(HTraceContext *context) {
    HParseError *error = context ? &context->error : NULL;
    if (!error || error->kind == H_PARSE_ERROR_NONE)
        return;
    size_t last_index = error->end_index > error->index ? error->end_index - 1 : error->end_index;

    fprintf(stderr, "error: ");
    if (error->source) {
        if (error->source->file_name)
            fprintf(stderr, "%s", error->source->file_name);
        else
            fputs("<unknown source>", stderr);
        if (error->source->line)
            fprintf(stderr, ":%zu", error->source->line);
        if (error->source->column)
            fprintf(stderr, ":%zu", error->source->column);
        if (error->source->function_name)
            fprintf(stderr, " in %s", error->source->function_name);
        fputs(": ", stderr);
    }
    if (error->message) {
        fprintf(stderr, "%s", error->message);
    } else if (error->kind == H_PARSE_ERROR_RANGE &&
               context->numeric_range.kind == H_TRACE_NUMERIC_RANGE_SINT) {
        fprintf(stderr, "unexpected int %" PRId64, context->numeric_range.actual.sint);
    } else if (error->kind == H_PARSE_ERROR_RANGE &&
               context->numeric_range.kind == H_TRACE_NUMERIC_RANGE_UINT) {
        fprintf(stderr, "unexpected int %" PRIu64, context->numeric_range.actual.uint);
    } else if (error->kind == H_PARSE_ERROR_RANGE &&
               context->numeric_range.kind == H_TRACE_NUMERIC_RANGE_FLOAT) {
        fprintf(stderr, "unexpected float %.17g", context->numeric_range.actual.floating);
    } else if (error->kind == H_PARSE_ERROR_RANGE) {
        fputs("mismatched token type", stderr);
    } else if (error->kind == H_PARSE_ERROR_SEMANTIC_PREDICATE) {
        fputs("semantic predicate failed", stderr);
    } else if (error->kind == H_PARSE_ERROR_ACTION) {
        fputs("semantic action failed", stderr);
    } else if (error->kind == H_PARSE_ERROR_EXPLICIT_FAILURE) {
        fprintf(stderr, "parser always fails at index %zu", error->index);
    } else if (error->kind == H_PARSE_ERROR_XOR) {
        fputs("both XOR alternatives matched; exactly one must match", stderr);
    } else if (error->kind == H_PARSE_ERROR_DIFFERENCE) {
        fputs("difference rejected a longer right-hand match", stderr);
    } else if (error->kind == H_PARSE_ERROR_BUTNOT) {
        fputs("but-not rejected a right-hand match that was not shorter", stderr);
    } else if (error->kind == H_PARSE_ERROR_NO_VALUE) {
        fputs("no value to retrieve from provided name", stderr);
    } else if (!error->has_actual) {
        fprintf(stderr, "unexpected end of input at index %zu", error->index);
    } else {
        fputs("unexpected byte ", stderr);
        trace_print_expected_byte(error->actual);
        fprintf(stderr, " (0x%02x = %u) at index %zu", error->actual, error->actual, error->index);
    }

    if (error->message)
        fprintf(stderr, " at index %zu", error->index);
    if (error->bit_offset)
        fprintf(stderr, ".%ub", error->bit_offset);
    if (!error->message &&
        (error->kind == H_PARSE_ERROR_RANGE || error->kind == H_PARSE_ERROR_SEMANTIC_PREDICATE ||
         error->kind == H_PARSE_ERROR_ACTION || error->kind == H_PARSE_ERROR_BUTNOT ||
         error->kind == H_PARSE_ERROR_DIFFERENCE || error->kind == H_PARSE_ERROR_XOR)) {
        if (error->index != last_index)
            fprintf(stderr, " from index %zu to index %zu", error->index, last_index);
        else
            fprintf(stderr, " at index %zu", error->index);
    } else if (!error->message && error->kind != H_PARSE_ERROR_EXPLICIT_FAILURE &&
               error->kind != H_PARSE_ERROR_NO_VALUE) {
        trace_print_expectations(context->expected_bytes, context->expected_eof);
    }
    if (!error->message && error->kind == H_PARSE_ERROR_RANGE) {
        if (context->numeric_range.kind == H_TRACE_NUMERIC_RANGE_SINT ||
            context->numeric_range.kind == H_TRACE_NUMERIC_RANGE_UINT)
            fprintf(stderr, "; expected value between %" PRId64 " and %" PRId64,
                    context->numeric_range.expected.integer.lower,
                    context->numeric_range.expected.integer.upper);
        else if (context->numeric_range.kind == H_TRACE_NUMERIC_RANGE_FLOAT)
            fprintf(stderr, "; expected value between %.17g and %.17g",
                    context->numeric_range.expected.floating.lower,
                    context->numeric_range.expected.floating.upper);
    }
    if (error->n_deepest > 0)
        trace_print_error_parsers(error);
    else
        trace_print_parser_context(context->failure_parser);
    fputc('\n', stderr);

    if (context->input && context->input_len > 0 && dump_input)
        h_trace_file_context(context->input, context->input_len, error->index, last_index);
}

void rvm_match_error(HRVMProg *prog, const uint8_t *input, size_t input_len, size_t index,
                     const bool expected[256], bool expected_eof, const HParser *parser) {
    if (!display_trace)
        return;
    h_backend_trace_begin(PB_REGULAR, prog->root_parser ? prog->root_parser : parser, input,
                          input_len);
    if (dump_trace)
        dump_rvm_prog(prog);
    h_backend_trace_failure(index, index < input_len ? index + 1 : index,
                            index < input_len ? H_PARSE_ERROR_PRIMITIVE_MISMATCH
                                              : H_PARSE_ERROR_UNEXPECTED_EOF,
                            parser, expected, expected_eof);
    h_backend_trace_end(false);
}
void svm_action_error(HSVMContext *ctx, HRVMProg *orig_prog, HRVMTrace *trace, const uint8_t *input,
                      size_t input_len, const char *msg) {
    if (!display_trace)
        return;
    (void)msg;
    h_backend_trace_begin(PB_REGULAR, orig_prog->root_parser ? orig_prog->root_parser : ctx->parser,
                          input, input_len);
    if (dump_trace)
        dump_svm_prog(orig_prog, trace);
    h_backend_trace_failure(ctx->input_pos, ctx->input_pos, H_PARSE_ERROR_ACTION, ctx->parser, NULL,
                            false);
    h_backend_trace_end(false);
}

void svm_failure_error(HSVMContext *ctx, HRVMProg *orig_prog, HRVMTrace *trace,
                       const uint8_t *input, size_t input_len) {
    if (!display_trace)
        return;
    h_backend_trace_begin(PB_REGULAR, orig_prog->root_parser ? orig_prog->root_parser : ctx->parser,
                          input, input_len);
    if (dump_trace)
        dump_svm_prog(orig_prog, trace);
    if (ctx->failure.kind == SVM_FAILURE_RANGE) {
        HParsedToken actual = {.token_type = ctx->failure.actual_type};
        if (actual.token_type == TT_SINT) {
            actual.token_data.sint = ctx->failure.actual.sint;
            h_trace_note_int_range(&actual, ctx->failure.lower, ctx->failure.upper);
        } else if (actual.token_type == TT_UINT) {
            actual.token_data.uint = ctx->failure.actual.uint;
            h_trace_note_int_range(&actual, ctx->failure.lower, ctx->failure.upper);
        } else if (actual.token_type == TT_FLOAT || actual.token_type == TT_DOUBLE) {
            actual.token_type = TT_DOUBLE;
            actual.token_data.dbl = ctx->failure.float_actual;
            h_trace_note_float_range(&actual, ctx->failure.float_lower, ctx->failure.float_upper);
        }
    }
    HParseErrorKind kind;
    switch (ctx->failure.kind) {
    case SVM_FAILURE_RANGE:
        kind = H_PARSE_ERROR_RANGE;
        break;
    case SVM_FAILURE_SEMANTIC_PREDICATE:
        kind = H_PARSE_ERROR_SEMANTIC_PREDICATE;
        break;
    default:
        kind = H_PARSE_ERROR_ACTION;
        break;
    }
    h_backend_trace_failure(ctx->failure.start, ctx->failure.end, kind, ctx->parser, NULL, false);
    h_backend_trace_end(false);
}

static const char *trace_backend_name(HParserBackend backend) {
    switch (backend) {
    case PB_REGULAR:
        return "h_regular_parse";
    case PB_LL:
        return "h_llk_parse";
    case PB_LALR:
        return "h_lalr_parse";
    case PB_GLR:
        return "h_glr_parse";
    default:
        return "h_cf_parse";
    }
}

static unsigned int trace_error_priority(HParseErrorKind kind) {
    switch (kind) {
    case H_PARSE_ERROR_RANGE:
        return 3;
    case H_PARSE_ERROR_SEMANTIC_PREDICATE:
        return 2;
    case H_PARSE_ERROR_PRIMITIVE_MISMATCH:
    case H_PARSE_ERROR_UNEXPECTED_EOF:
        return 1;
    default:
        return 0;
    }
}

void h_backend_trace_begin(HParserBackend backend, const HParser *parser, const uint8_t *input,
                           size_t input_len) {
    if (!display_trace)
        return;

    HTraceContext *context = calloc(1, sizeof(*context));
    if (!context)
        return;
    context->input = input;
    context->input_len = input_len;
    context->backend = backend;
    context->root_parser = parser;
    context->parent = trace_context;
    trace_context = context;

    fprintf(stderr, "\n=== %s: begin (%zu bytes of input) ===\n", trace_backend_name(backend),
            input_len);
}

void h_cf_trace_begin(HParserBackend backend, const HParser *parser, const uint8_t *input,
                      size_t input_len) {
    h_backend_trace_begin(backend, parser, input, input_len);
}

void h_backend_trace_failure(size_t start, size_t end, HParseErrorKind kind, const HParser *parser,
                             const bool expected[256], bool expected_eof) {
    if (!display_trace || !trace_context)
        return;

    HTraceContext *context = trace_context;
    const HParser *origin = parser ? parser : context->root_parser;
    const char *name = origin && origin->vtable ? trace_vt_name(origin->vtable) : "?(no parser)";
    const HParser *label_parser = origin && origin->diagnostic_label ? origin : NULL;
    const HParser *message_parser = origin && origin->diagnostic_message ? origin : NULL;
    const HParser *source_parser = origin && origin->diagnostic_source ? origin : NULL;
    if (!label_parser && context->root_parser && context->root_parser->diagnostic_label)
        label_parser = context->root_parser;
    if (!message_parser && context->root_parser && context->root_parser->diagnostic_message)
        message_parser = context->root_parser;
    if (!source_parser && context->root_parser && context->root_parser->diagnostic_source)
        source_parser = context->root_parser;
    const char *failure_name =
        label_parser ? label_parser->diagnostic_label : trace_parser_diagnostic_name(origin);
    const char *failure_message = message_parser ? message_parser->diagnostic_message : NULL;
    size_t index = start;
    if (h_is_nothing_parser(origin)) {
        kind = H_PARSE_ERROR_EXPLICIT_FAILURE;
        end = start;
        expected = NULL;
        expected_eof = false;
    }
    if (kind == H_PARSE_ERROR_SEMANTIC_PREDICATE &&
        (strcmp(name, "parse_int_range") == 0 || strcmp(name, "parse_float_range") == 0)) {
        kind = H_PARSE_ERROR_RANGE;
    }

    HParseError *error = &context->error;
    HTraceNumericRange numeric_range = context->pending_numeric_range;
    memset(&context->pending_numeric_range, 0, sizeof(context->pending_numeric_range));
    size_t progress = trace_failure_progress(kind, index, end);
    size_t previous_progress = trace_failure_progress(error->kind, error->index, error->end_index);
    bool replace = error->kind == H_PARSE_ERROR_NONE || progress > previous_progress ||
                   (progress == previous_progress && failure_message && !error->message) ||
                   (progress == previous_progress && !!failure_message == !!error->message &&
                    trace_error_priority(kind) > trace_error_priority(error->kind));
    if (replace) {
        memset(error, 0, sizeof(*error));
        memset(context->expected_bytes, 0, sizeof(context->expected_bytes));
        context->expected_eof = false;
        memset(&context->numeric_range, 0, sizeof(context->numeric_range));
        if (kind == H_PARSE_ERROR_RANGE)
            context->numeric_range = numeric_range;
        context->failure_parser = label_parser ? label_parser : origin;
        error->index = index;
        error->end_index = end;
        error->kind = kind;
        error->parser = failure_name;
        error->message = failure_message;
        error->source = source_parser ? source_parser->diagnostic_source : NULL;
        if (kind != H_PARSE_ERROR_EXPLICIT_FAILURE && index < context->input_len) {
            error->actual = context->input[index];
            error->has_actual = true;
        }
        trace_error_add_parser(error, failure_name);
        if (context->root_parser && context->root_parser->vtable) {
            const char *root_name = trace_parser_diagnostic_name(context->root_parser);
            if (strcmp(root_name, failure_name) != 0)
                error->context[error->n_context++] = root_name;
        }
    } else if (progress == previous_progress && kind == error->kind &&
               !!failure_message == !!error->message) {
        trace_error_add_parser(error, failure_name);
    } else {
        return;
    }

    if (expected) {
        for (size_t i = 0; i < 256; i++)
            context->expected_bytes[i] |= expected[i];
    }
    context->expected_eof |= expected_eof;
}

void h_cf_trace_failure(size_t start, size_t end, HParseErrorKind kind, const HParser *parser,
                        const bool expected[256], bool expected_eof) {
    h_backend_trace_failure(start, end, kind, parser, expected, expected_eof);
}

void h_cf_trace_parser_enter(const HParser *parser, size_t index, const char *role) {
    if (!display_trace || !dump_trace || !trace_context)
        return;

    const HParser *origin = parser ? parser : trace_context->root_parser;
    char *name = h_trace_parser_name(origin);
    trace_indent();
    fprintf(stderr, "-> %-20s %-11s @%zu\n", name ? name : "?(no parser)",
            role ? role : "nonterminal", index);
    free(name);

    if (trace_context->frame_count < H_TRACE_MAX_FRAMES) {
        HTraceFrame *frame = &trace_context->frames[trace_context->frame_count++];
        frame->parser = origin;
        frame->name = origin && origin->vtable ? trace_vt_name(origin->vtable) : "?(no parser)";
        frame->start = index;
        frame->start_bit = 0;
        frame->failure_serial = 0;
    } else {
        trace_context->overflow_frames++;
    }
    trace_context->depth++;
}

void h_cf_trace_parser_exit(const HParser *parser, size_t start, size_t end,
                            const HParsedToken *token, bool success, const char *role) {
    (void)parser;
    if (!display_trace || !dump_trace || !trace_context)
        return;

    if (trace_context->depth > 0)
        trace_context->depth--;
    if (trace_context->overflow_frames > 0)
        trace_context->overflow_frames--;
    else if (trace_context->frame_count > 0)
        trace_context->frame_count--;

    trace_indent();
    if (success) {
        fputs("<= OK   ast=", stderr);
        trace_token(token);
        fprintf(stderr, " span=[%zu,%zu)", start, end);
    } else {
        fputs("<= FAIL", stderr);
        fprintf(stderr, " @%zu", end);
    }
    if (role)
        fprintf(stderr, "  [%s]", role);
    fputc('\n', stderr);
}

static void trace_lr_branch(size_t branch) {
    if (trace_context && trace_context->backend == PB_GLR)
        fprintf(stderr, "[branch %zu] ", branch);
}

static void trace_lr_parser(const HParser *parser) {
    char *name = h_trace_parser_name(parser);
    if (name) {
        fprintf(stderr, " parser=%s", name);
        free(name);
    }
}

void h_cf_trace_lr_shift(size_t branch, size_t from_state, size_t to_state, size_t index,
                         const HParser *parser, const HParsedToken *token) {
    if (!display_trace || !dump_trace || !trace_context)
        return;

    fprintf(stderr, "@%04zu ", index);
    trace_lr_branch(branch);
    fprintf(stderr, "SHIFT  s%zu -> s%zu", from_state, to_state);
    if (token && token->token_type == TT_UINT) {
        fputs(" input=", stderr);
        trace_print_expected_byte((uint8_t)token->token_data.uint);
    } else {
        fputs(" input=end-of-input", stderr);
    }
    trace_lr_parser(parser);
    fputc('\n', stderr);
}

void h_cf_trace_lr_reduce(size_t branch, size_t from_state, size_t to_state, size_t length,
                          size_t start, size_t end, const HParser *parser,
                          const HParsedToken *token, bool success) {
    if (!display_trace || !dump_trace || !trace_context)
        return;

    fprintf(stderr, "@%04zu ", end);
    trace_lr_branch(branch);
    if (!success) {
        fprintf(stderr, "REJECT  s%zu len=%zu span=[%zu,%zu)", from_state, length, start, end);
    } else if (to_state == SIZE_MAX) {
        fprintf(stderr, "REDUCE  s%zu -> accept len=%zu span=[%zu,%zu)", from_state, length, start,
                end);
    } else {
        fprintf(stderr, "REDUCE  s%zu -> s%zu len=%zu span=[%zu,%zu)", from_state, to_state, length,
                start, end);
    }
    trace_lr_parser(parser);
    if (success) {
        fputs(" ast=", stderr);
        trace_token(token);
    }
    fputc('\n', stderr);
}

void h_cf_trace_lr_error(size_t branch, size_t state, size_t index, const HParser *parser) {
    if (!display_trace || !dump_trace || !trace_context)
        return;

    fprintf(stderr, "@%04zu ", index);
    trace_lr_branch(branch);
    fprintf(stderr, "ERROR  no action in s%zu", state);
    trace_lr_parser(parser);
    fputc('\n', stderr);
}

size_t h_cf_trace_glr_fork(size_t branch, size_t state, size_t index) {
    if (!display_trace || !dump_trace || !trace_context)
        return branch;

    size_t child = ++trace_context->next_branch_id;
    fprintf(stderr, "@%04zu [branch %zu] FORK -> branch %zu at s%zu\n", index, branch, child,
            state);
    return child;
}

void h_cf_trace_glr_merge(size_t survivor, size_t merged, size_t state, size_t index) {
    if (!display_trace || !dump_trace || !trace_context)
        return;
    fprintf(stderr, "@%04zu [branch %zu] MERGE branch %zu at s%zu\n", index, survivor, merged,
            state);
}

void h_backend_trace_end(bool success) {
    if (!display_trace)
        return;

    HTraceContext *context = trace_context;
    if (!context)
        return;

    if (dump_trace) {
        while (context->overflow_frames > 0) {
            if (context->depth > 0)
                context->depth--;
            context->overflow_frames--;
        }
        while (context->frame_count > 0) {
            HTraceFrame *frame = &context->frames[context->frame_count - 1];
            context->frame_count--;
            if (context->depth > 0)
                context->depth--;
            trace_indent();
            char *name = frame->parser ? h_trace_parser_name(frame->parser) : NULL;
            fprintf(stderr, "<= FAIL  [aborted %s from @%zu]\n", name ? name : frame->name,
                    frame->start);
            free(name);
        }
    }

    fprintf(stderr, "=== %s: end (%s) ===\n", trace_backend_name(context->backend),
            success ? "SUCCESS" : "FAILURE");

    if (!success)
        trace_render_diagnostic(context);

    HTraceContext *parent = context->parent;
    trace_complete(context);
    trace_context = parent;
    free(context);
}

void h_cf_trace_end(bool success) { h_backend_trace_end(success); }
#endif /* HAMMER_TRACE_AST */
