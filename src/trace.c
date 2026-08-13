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
static H_TRACE_THREAD_LOCAL unsigned trace_enable_depth = 0;
static H_TRACE_THREAD_LOCAL HParseDiagnostic trace_completed;
#define H_TRACE_MAX_NESTING 256
static H_TRACE_THREAD_LOCAL bool trace_dump_stack[H_TRACE_MAX_NESTING];

/* Toggle the runtime trace. Exposed (see trace.h) so h_parse_debug() can enable
 * tracing for just its own call and switch it back off afterward. */
void h_trace_set_enabled(bool enabled, bool dumpExecutionTrace) {
    if (enabled) {
        if (trace_enable_depth == 0)
            memset(&trace_completed, 0, sizeof(trace_completed));
        if (trace_enable_depth == 0)
            trace_completed.choice_root = H_TRACE_CHOICE_NONE;
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
}

bool h_trace_is_enabled(void) { return display_trace; }
bool h_trace_is_dump_enabled(void) { return display_trace && dump_trace; }

#define H_TRACE_MAX_FRAMES 256
#define H_TRACE_MAX_CHOICE_DEPTH 8

typedef struct HTraceDiagnosticState_ {
    HParseError error;
    const HParser *failure_parser;
    bool expected_bytes[256];
    bool expected_eof;
    HTraceNumericRange numeric_range;
    HTraceDispatchFailure dispatch_failure;
    HTraceFrame input_frames[H_PARSE_DIAGNOSTIC_MAX_INPUT_FRAMES];
    size_t input_frame_count;
    size_t choice_root;
} HTraceDiagnosticState;

typedef struct HTraceChoiceScope_ {
    size_t id;
    const HParser *parser;
    const HParser *origin;
    size_t node_checkpoint;
    size_t alternative_checkpoint;
    size_t first_alternative;
    size_t last_alternative;
    size_t alternative_count;
    size_t active_alternative;
    unsigned long arm_failure_serial;
    HTraceDiagnosticState outer;
    HTraceDiagnosticState best;
    bool has_best;
    bool truncated;
} HTraceChoiceScope;

typedef struct HTraceContext_ {
    const uint8_t *input;
    size_t input_len;
    size_t depth;
    size_t frame_count;
    size_t overflow_frames;
    unsigned long failure_serial;
    HTraceFrame frames[H_TRACE_MAX_FRAMES];
    HTraceFrame input_frames[H_PARSE_DIAGNOSTIC_MAX_INPUT_FRAMES];
    size_t input_frame_count;
    HTraceFrame observed_frames[H_PARSE_DIAGNOSTIC_MAX_INPUT_FRAMES];
    const HDiagnosticContext *observed_occurrences[H_PARSE_DIAGNOSTIC_MAX_INPUT_FRAMES];
    bool observed_entered[H_PARSE_DIAGNOSTIC_MAX_INPUT_FRAMES];
    size_t observed_frame_count;
    const HDiagnosticContext *observed_provenance;
    size_t observed_index;
    size_t observed_depth;
    HParseError error;
    HParserBackend backend;
    const HParser *root_parser;
    const HParser *failure_parser;
    bool expected_bytes[256];
    bool expected_eof;
    HTraceNumericRange numeric_range;
    HTraceNumericRange pending_numeric_range;
    HTraceDispatchFailure dispatch_failure;
    HTraceDispatchFailure pending_dispatch_failure;
    HTraceChoiceNode choice_nodes[H_TRACE_MAX_CHOICE_NODES];
    size_t choice_node_count;
    HTraceChoiceAlternative choice_alternatives[H_TRACE_MAX_CHOICE_ALTERNATIVES];
    size_t choice_alternative_count;
    size_t choice_root;
    HTraceChoiceScope choice_scopes[H_TRACE_MAX_CHOICE_DEPTH];
    size_t choice_depth;
    const HTraceCandidateFrame *pending_input_frames;
    size_t pending_input_frame_count;
    size_t next_choice_scope;
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
    trace_completed.dispatch_failure = context->dispatch_failure;
    memcpy(trace_completed.choice_nodes, context->choice_nodes,
           context->choice_node_count * sizeof(context->choice_nodes[0]));
    trace_completed.choice_node_count = context->choice_node_count;
    memcpy(trace_completed.choice_alternatives, context->choice_alternatives,
           context->choice_alternative_count * sizeof(context->choice_alternatives[0]));
    trace_completed.choice_alternative_count = context->choice_alternative_count;
    trace_completed.choice_root = context->choice_root;
    trace_completed.input_frame_count = context->input_frame_count;
    memcpy(trace_completed.input_frames, context->input_frames,
           context->input_frame_count * sizeof(context->input_frames[0]));
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
    (*out)->dispatch_failure = trace_completed.dispatch_failure;
    (*out)->choice_node_count = trace_completed.choice_node_count;
    for (size_t i = 0; i < trace_completed.choice_node_count; i++)
        (*out)->choice_nodes[i] = trace_completed.choice_nodes[i];
    (*out)->choice_alternative_count = trace_completed.choice_alternative_count;
    for (size_t i = 0; i < trace_completed.choice_alternative_count; i++) {
        (*out)->choice_alternatives[i] = trace_completed.choice_alternatives[i];
        trace_copy_error(&(*out)->choice_alternatives[i].error,
                         &trace_completed.choice_alternatives[i].error);
    }
    (*out)->choice_root = trace_completed.choice_root;
    (*out)->input_frame_count = trace_completed.input_frame_count;
    memcpy((*out)->input_frames, trace_completed.input_frames,
           trace_completed.input_frame_count * sizeof(trace_completed.input_frames[0]));
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

void h_trace_note_dispatch(const HParsedToken *token, bool has_opcode, size_t opcode,
                           const OpcodeMap *map, size_t count) {
    if (!display_trace || !trace_context || !token)
        return;

    HTraceDispatchFailure *failure = &trace_context->pending_dispatch_failure;
    memset(failure, 0, sizeof(*failure));
    failure->present = true;
    failure->has_opcode = has_opcode;
    failure->opcode = opcode;

    for (size_t i = 0; map && i < count; i++) {
        bool duplicate = false;
        for (size_t j = 0; j < failure->expected_count; j++) {
            if (failure->expected[j] == map[i].opcode) {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;
        if (failure->expected_count < H_TRACE_MAX_DISPATCH_OPCODES) {
            failure->expected[failure->expected_count++] = map[i].opcode;
        } else {
            failure->expected_truncated = true;
        }
    }
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
    if (h_is_context_parser(parser))
        return trace_parser_diagnostic_name(h_context_parser_child(parser));
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
#define MAX_CONTEXT_BYTES 256
void h_trace_file_context(const uint8_t *input, size_t length, size_t start_highlight,
                          size_t end_highlight) {
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

    if (!input || length == 0)
        return;

    /* Diagnostic positions can point at EOF or arrive with an empty/reversed
     * range. Use a clamped copy for window selection so rendering an error can
     * never wrap a size_t. Keep the original range for highlighting: an EOF
     * position must not incorrectly highlight the final input byte. */
    size_t focus_start = start_highlight < length ? start_highlight : length - 1;
    size_t focus_end = end_highlight < length ? end_highlight : length - 1;
    if (focus_end < focus_start)
        focus_end = focus_start;

    size_t window_start = 0;
    size_t window_end = length;
    if (length > MAX_CONTEXT_BYTES) {
        size_t range_width = focus_end - focus_start;

        if (range_width >= MAX_CONTEXT_BYTES - 1) {
            /* The complete range cannot fit; show it from its first byte. */
            window_start = focus_start;
        } else {
            size_t range_length = range_width + 1;
            size_t context_before = (MAX_CONTEXT_BYTES - range_length) / 2;
            window_start = focus_start > context_before ? focus_start - context_before : 0;
        }

        /* Keep a full window where possible, especially near EOF. */
        if (window_start > length - MAX_CONTEXT_BYTES)
            window_start = length - MAX_CONTEXT_BYTES;
        window_end = window_start + MAX_CONTEXT_BYTES;
    }

    if (window_start > 0)
        fprintf(stderr, "... %zu byte(s) omitted ...\n", window_start);

    for (size_t off = window_start; off < window_end; off += BYTES_PER_LINE) {
        size_t line_len = (window_end - off < BYTES_PER_LINE) ? (window_end - off) : BYTES_PER_LINE;
        fprintf(stderr, "%04zx:  ", off);

        /* Hex bytes, grouped by 4 for readability */
        for (size_t i = 0; i < BYTES_PER_LINE; ++i) {
            size_t idx = off + i;
            if (i < line_len) {
                int is_highlight = (idx >= start_highlight && idx <= end_highlight);
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
            int is_highlight = (idx >= start_highlight && idx <= end_highlight);
            if (use_color && is_highlight)
                fputs(color_red, stderr);
            fputc(isprint(c) ? (char)c : '.', stderr);
            if (use_color && is_highlight)
                fputs(color_reset, stderr);
        }
        fprintf(stderr, "\n");
    }

    if (window_end < length)
        fprintf(stderr, "... %zu byte(s) omitted ...\n", length - window_end);
}

/* Helper for trace_render to print the source of each parser. */
static void trace_print_source(FILE *stream, const HParser *parser) {
    if (!stream || !parser || !parser->diagnostic_source)
        return;

    const HSourceLocation *source = parser->diagnostic_source;

    fputs(" [", stream);
    if (source->file_name)
        fprintf(stream, "%s", source->file_name);
    else
        fputs("<unknown source>", stream);
    if (source->line)
        fprintf(stream, ":%zu", source->line);
    if (source->column)
        fprintf(stream, ":%zu", source->column);
    /*
    if (source->function_name)
        fprintf(stream, " in %s", source->function_name);
    */
    fputs("]", stream);
}

static void trace_print_input_position(FILE *stream, size_t index, uint8_t bit_offset) {
    fprintf(stream, "%zu", index);
    if (bit_offset)
        fprintf(stream, ".%ub", bit_offset);
}

static void trace_print_input_trail(FILE *stream, const HTraceContext *context) {
    if (!stream || !context || context->input_frame_count == 0)
        return;

    fputs("input trail:\n", stream);
    for (size_t i = 0; i < context->input_frame_count; i++) {
        const HTraceFrame *frame = &context->input_frames[i];
        char *name = h_trace_parser_name(frame->parser);

        fprintf(stream, "  -> %-20s entered at index ", name ? name : frame->name);
        trace_print_input_position(stream, frame->start, frame->start_bit);
        fputs(", reached index ", stream);
        trace_print_input_position(stream, frame->reached, frame->reached_bit);
        trace_print_source(stream, frame->parser);
        fputc('\n', stream);
        free(name);
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
    context->choice_root = H_TRACE_CHOICE_NONE;
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
        trace_print_source(stderr, parser);
        fputc('\n', stderr);
        free(parser_name);
    }
    if (trace_context && !trace_context->root_parser)
        trace_context->root_parser = parser;
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
    if (h_is_put_value_parser(parser))
        return H_PARSE_ERROR_REUSED_NAME;
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
    case H_PARSE_ERROR_REUSED_NAME:
    case H_PARSE_ERROR_DISPATCH:
        return end;
    default:
        return start;
    }
}

static unsigned int trace_error_priority(HParseErrorKind kind);

static void trace_state_save(const HTraceContext *context, HTraceDiagnosticState *state) {
    memset(state, 0, sizeof(*state));
    state->error = context->error;
    state->failure_parser = context->failure_parser;
    memcpy(state->expected_bytes, context->expected_bytes, sizeof(state->expected_bytes));
    state->expected_eof = context->expected_eof;
    state->numeric_range = context->numeric_range;
    state->dispatch_failure = context->dispatch_failure;
    state->input_frame_count = context->input_frame_count;
    memcpy(state->input_frames, context->input_frames,
           context->input_frame_count * sizeof(state->input_frames[0]));
    state->choice_root = context->choice_root;
}

static void trace_state_restore(HTraceContext *context, const HTraceDiagnosticState *state) {
    context->error = state->error;
    context->failure_parser = state->failure_parser;
    memcpy(context->expected_bytes, state->expected_bytes, sizeof(context->expected_bytes));
    context->expected_eof = state->expected_eof;
    context->numeric_range = state->numeric_range;
    context->dispatch_failure = state->dispatch_failure;
    context->input_frame_count = state->input_frame_count;
    memcpy(context->input_frames, state->input_frames,
           state->input_frame_count * sizeof(context->input_frames[0]));
    context->choice_root = state->choice_root;
}

static void trace_state_clear(HTraceContext *context) {
    memset(&context->error, 0, sizeof(context->error));
    context->failure_parser = NULL;
    memset(context->expected_bytes, 0, sizeof(context->expected_bytes));
    context->expected_eof = false;
    memset(&context->numeric_range, 0, sizeof(context->numeric_range));
    memset(&context->dispatch_failure, 0, sizeof(context->dispatch_failure));
    memset(&context->pending_numeric_range, 0, sizeof(context->pending_numeric_range));
    memset(&context->pending_dispatch_failure, 0, sizeof(context->pending_dispatch_failure));
    context->input_frame_count = 0;
    context->choice_root = H_TRACE_CHOICE_NONE;
}

static size_t trace_state_progress(const HTraceDiagnosticState *state) {
    return trace_failure_progress(state->error.kind, state->error.index, state->error.end_index);
}

static bool trace_state_prefer(const HTraceDiagnosticState *candidate,
                               const HTraceDiagnosticState *current) {
    if (current->error.kind == H_PARSE_ERROR_NONE)
        return candidate->error.kind != H_PARSE_ERROR_NONE;
    if (candidate->error.kind == H_PARSE_ERROR_NONE)
        return false;
    size_t candidate_progress = trace_state_progress(candidate);
    size_t current_progress = trace_state_progress(current);
    if (candidate_progress != current_progress)
        return candidate_progress > current_progress;
    if (!!candidate->error.message != !!current->error.message)
        return candidate->error.message != NULL;
    return trace_error_priority(candidate->error.kind) > trace_error_priority(current->error.kind);
}

static const HParser *trace_choice_origin(const HTraceContext *context,
                                          const HParser *choice_parser) {
    if (!context)
        return choice_parser;
    for (size_t i = context->frame_count; i > 0; i--) {
        const HParser *parser = context->frames[i - 1].parser;
        if (parser == choice_parser)
            continue;
        if (h_is_context_parser(parser))
            return parser;
        break;
    }
    return choice_parser;
}

static size_t trace_choice_begin_at(const HParser *parser, const HParser *origin) {
    if (!display_trace || !trace_context ||
        trace_context->choice_depth >= H_TRACE_MAX_CHOICE_DEPTH || !parser)
        return H_TRACE_CHOICE_NONE;

    HTraceContext *context = trace_context;
    HTraceChoiceScope *scope = &context->choice_scopes[context->choice_depth++];
    memset(scope, 0, sizeof(*scope));
    scope->id = ++context->next_choice_scope;
    scope->parser = parser;
    scope->origin = origin ? origin : parser;
    scope->node_checkpoint = context->choice_node_count;
    scope->alternative_checkpoint = context->choice_alternative_count;
    scope->first_alternative = H_TRACE_CHOICE_NONE;
    scope->last_alternative = H_TRACE_CHOICE_NONE;
    scope->active_alternative = H_TRACE_CHOICE_NONE;
    trace_state_save(context, &scope->outer);
    return scope->id;
}

size_t h_trace_choice_begin(void) {
    if (!trace_context || trace_context->frame_count == 0)
        return H_TRACE_CHOICE_NONE;
    const HTraceFrame *frame = &trace_context->frames[trace_context->frame_count - 1];
    return trace_choice_begin_at(frame->parser,
                                 trace_choice_origin(trace_context, frame->parser));
}

static HTraceChoiceScope *trace_choice_scope(HTraceContext *context, size_t id) {
    if (!context || id == H_TRACE_CHOICE_NONE || context->choice_depth == 0)
        return NULL;
    HTraceChoiceScope *scope = &context->choice_scopes[context->choice_depth - 1];
    return scope->id == id ? scope : NULL;
}

void h_trace_choice_arm_begin(size_t id, size_t alternative) {
    HTraceChoiceScope *scope = trace_choice_scope(trace_context, id);
    if (!scope)
        return;
    scope->active_alternative = alternative;
    scope->arm_failure_serial = trace_context->failure_serial;
    trace_state_clear(trace_context);
}

void h_trace_choice_arm_end(size_t id, bool success) {
    HTraceContext *context = trace_context;
    HTraceChoiceScope *scope = trace_choice_scope(context, id);
    if (!scope || scope->active_alternative == H_TRACE_CHOICE_NONE)
        return;

    if (!success && context->failure_serial != scope->arm_failure_serial &&
        context->error.kind != H_PARSE_ERROR_NONE) {
        HTraceDiagnosticState candidate;
        trace_state_save(context, &candidate);

        if (!scope->has_best || trace_state_prefer(&candidate, &scope->best)) {
            scope->best = candidate;
            scope->has_best = true;
        } else if (scope->has_best &&
                   trace_state_progress(&candidate) == trace_state_progress(&scope->best)) {
            for (size_t i = 0; i < 256; i++)
                scope->best.expected_bytes[i] |= candidate.expected_bytes[i];
            scope->best.expected_eof |= candidate.expected_eof;
        }

        if (context->choice_alternative_count < H_TRACE_MAX_CHOICE_ALTERNATIVES) {
            size_t index = context->choice_alternative_count++;
            HTraceChoiceAlternative *record = &context->choice_alternatives[index];
            memset(record, 0, sizeof(*record));
            record->alternative = scope->active_alternative;
            record->error = candidate.error;
            memcpy(record->expected_bytes, candidate.expected_bytes,
                   sizeof(record->expected_bytes));
            record->expected_eof = candidate.expected_eof;
            record->child_node = candidate.choice_root;
            record->next = H_TRACE_CHOICE_NONE;
            if (scope->last_alternative != H_TRACE_CHOICE_NONE)
                context->choice_alternatives[scope->last_alternative].next = index;
            else
                scope->first_alternative = index;
            scope->last_alternative = index;
            scope->alternative_count++;
        } else {
            scope->truncated = true;
        }
    }

    scope->active_alternative = H_TRACE_CHOICE_NONE;
}

static void trace_choice_normalize(HTraceContext *context, HTraceChoiceScope *scope,
                                   size_t node_index) {
    HTraceDiagnosticState aggregate = scope->best;
    HTraceChoiceNode *node = &context->choice_nodes[node_index];
    memset(node, 0, sizeof(*node));
    node->first_alternative = scope->first_alternative;
    node->alternative_count = scope->alternative_count;
    node->furthest_progress = trace_state_progress(&scope->best);
    node->truncated = scope->truncated;

    const HParser *origin = scope->origin ? scope->origin : scope->parser;
    const char *choice_name = trace_parser_diagnostic_name(origin);
    const char *choice_message = origin ? origin->diagnostic_message : NULL;
    const HSourceLocation *choice_source = origin ? origin->diagnostic_source : NULL;
    aggregate.error.parser = choice_name;
    aggregate.error.message = choice_message;
    aggregate.error.source = choice_source;
    aggregate.error.n_deepest = 0;
    trace_error_add_parser(&aggregate.error, choice_name);
    aggregate.failure_parser = origin;
    aggregate.choice_root = node_index;

    memset(aggregate.expected_bytes, 0, sizeof(aggregate.expected_bytes));
    aggregate.expected_eof = false;
    for (size_t index = scope->first_alternative; index != H_TRACE_CHOICE_NONE;
         index = context->choice_alternatives[index].next) {
        const HTraceChoiceAlternative *alternative = &context->choice_alternatives[index];
        size_t progress = trace_failure_progress(alternative->error.kind, alternative->error.index,
                                                 alternative->error.end_index);
        if (progress != node->furthest_progress)
            continue;
        for (size_t i = 0; i < 256; i++)
            aggregate.expected_bytes[i] |= alternative->expected_bytes[i];
        aggregate.expected_eof |= alternative->expected_eof;
    }

    trace_state_restore(context, &aggregate);
}

void h_trace_choice_end(size_t id, bool success) {
    HTraceContext *context = trace_context;
    HTraceChoiceScope *scope = trace_choice_scope(context, id);
    if (!scope)
        return;

    if (success || !scope->has_best) {
        trace_state_restore(context, &scope->outer);
        context->choice_node_count = scope->node_checkpoint;
        context->choice_alternative_count = scope->alternative_checkpoint;
    } else if (context->choice_node_count < H_TRACE_MAX_CHOICE_NODES) {
        size_t node_index = context->choice_node_count++;
        trace_choice_normalize(context, scope, node_index);
        HTraceDiagnosticState aggregate;
        trace_state_save(context, &aggregate);
        if (trace_state_prefer(&scope->outer, &aggregate)) {
            trace_state_restore(context, &scope->outer);
            context->choice_node_count = scope->node_checkpoint;
            context->choice_alternative_count = scope->alternative_checkpoint;
        }
    } else {
        trace_state_restore(context, &scope->best);
    }

    context->choice_depth--;
}

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

static size_t trace_provenance_depth(const HDiagnosticContext *provenance) {
    size_t depth = 0;
    for (; provenance; provenance = provenance->next)
        depth++;
    return depth;
}

static bool trace_provenance_has_parent(const HDiagnosticContext *path,
                                        const HDiagnosticContext *parent) {
    size_t path_depth = trace_provenance_depth(path);
    size_t parent_depth = trace_provenance_depth(parent);
    while (path && path_depth > parent_depth) {
        path = path->next;
        path_depth--;
    }
    return path == parent;
}

static bool trace_observed_occurrence_equal(const HTraceContext *context, size_t index,
                                            const HDiagnosticContext *item) {
    if (context->observed_occurrences[index] == item)
        return true;
    return h_is_context_parser(item->parser) &&
           context->observed_frames[index].parser == item->parser;
}

/* Remember entry positions for compiled parser occurrences. Ancestors keep
 * their first observation while the parser producing the current PUSH/shift
 * is refreshed, so repeated occurrences use their latest runtime entry. */
static void trace_observe_provenance(HTraceContext *context, const HDiagnosticContext *provenance,
                                     const HParser *event_parser, size_t index) {
    if (!context || !provenance)
        return;

    size_t depth = trace_provenance_depth(provenance);
    const HDiagnosticContext *event = NULL;
    for (const HDiagnosticContext *item = provenance; item; item = item->next)
        if (!event && item->parser == event_parser)
            event = item;

    for (const HDiagnosticContext *item = provenance; item; item = item->next) {
        const HParser *parser = item->parser;
        if (!parser)
            continue;
        HTraceFrame *frame = NULL;
        size_t slot = 0;
        for (size_t i = 0; i < context->observed_frame_count; i++) {
            if (trace_observed_occurrence_equal(context, i, item)) {
                frame = &context->observed_frames[i];
                slot = i;
                break;
            }
        }
        if (!frame && context->observed_frame_count < H_PARSE_DIAGNOSTIC_MAX_INPUT_FRAMES) {
            slot = context->observed_frame_count++;
            frame = &context->observed_frames[slot];
            memset(frame, 0, sizeof(*frame));
            context->observed_occurrences[slot] = item;
            frame->parser = parser;
            frame->name = parser->vtable ? trace_vt_name(parser->vtable) : "?(no parser)";
            frame->start = index;
        }
        if (frame && item == event && !context->observed_entered[slot]) {
            frame->start = index;
            context->observed_entered[slot] = true;
        } else if (frame && !context->observed_entered[slot] && index < frame->start) {
            frame->start = index;
        }
    }

    if (!context->observed_provenance || index > context->observed_index ||
        (index == context->observed_index && depth >= context->observed_depth)) {
        context->observed_provenance = provenance;
        context->observed_index = index;
        context->observed_depth = depth;
    }
}

static void trace_snapshot_provenance(HTraceContext *context, const HDiagnosticContext *provenance,
                                      size_t fallback_start, size_t reached, uint8_t reached_bit) {
    if (!context)
        return;

    const HDiagnosticContext *path = provenance;
    if (context->observed_provenance &&
        (!path || trace_provenance_has_parent(context->observed_provenance, path)))
        path = context->observed_provenance;
    if (!path)
        return;

    context->input_frame_count = 0;
    size_t depth = trace_provenance_depth(path);
    size_t skip = depth > H_PARSE_DIAGNOSTIC_MAX_INPUT_FRAMES
                      ? depth - H_PARSE_DIAGNOSTIC_MAX_INPUT_FRAMES
                      : 0;
    while (path && skip-- > 0)
        path = path->next;
    const HDiagnosticContext *ordered[H_PARSE_DIAGNOSTIC_MAX_INPUT_FRAMES];
    size_t ordered_count = 0;
    for (const HDiagnosticContext *item = path;
         item && ordered_count < H_PARSE_DIAGNOSTIC_MAX_INPUT_FRAMES; item = item->next)
        ordered[ordered_count++] = item;

    const HParser *previous = NULL;
    while (ordered_count > 0 &&
           context->input_frame_count < H_PARSE_DIAGNOSTIC_MAX_INPUT_FRAMES) {
        const HDiagnosticContext *item = ordered[--ordered_count];
        const HParser *parser = item->parser;
        if (!parser || parser == previous)
            continue;
        if (h_is_context_parser(previous) && h_context_parser_child(previous) == parser)
            continue;
        previous = parser;

        HTraceFrame *dst = &context->input_frames[context->input_frame_count++];
        memset(dst, 0, sizeof(*dst));
        dst->parser = parser;
        dst->name = parser->vtable ? trace_vt_name(parser->vtable) : "?(no parser)";
        dst->start = fallback_start;
        for (size_t i = 0; i < context->observed_frame_count; i++) {
            if (trace_observed_occurrence_equal(context, i, item)) {
                dst->start = context->observed_frames[i].start;
                dst->start_bit = context->observed_frames[i].start_bit;
                break;
            }
        }
        dst->reached = reached;
        dst->reached_bit = reached_bit;
    }
}

static void trace_observe_svm_trace(HTraceContext *context, const HRVMTrace *trace) {
    for (const HRVMTrace *item = trace; item; item = item->next) {
        if (item->opcode == SVM_PUSH)
            trace_observe_provenance(context, item->diagnostic_context, item->parser,
                                     item->input_pos);
    }
}

static void trace_snapshot_input_frames(HTraceContext *context, size_t reached,
                                        uint8_t reached_bit) {
    context->input_frame_count = 0;

    for (size_t i = 0; i < context->frame_count &&
                       context->input_frame_count < H_PARSE_DIAGNOSTIC_MAX_INPUT_FRAMES;
         i++) {
        const HTraceFrame *src = &context->frames[i];
        const HParser *parser = src->parser;

        if (!parser)
            continue;

        if (context->input_frame_count > 0) {
            const HTraceFrame *previous = &context->input_frames[context->input_frame_count - 1];
            if (h_is_context_parser(previous->parser) &&
                h_context_parser_child(previous->parser) == parser)
                continue;
            if (previous->parser == parser && previous->start == src->start &&
                previous->start_bit == src->start_bit)
                continue;
        }

        HTraceFrame *dst = &context->input_frames[context->input_frame_count++];
        *dst = *src;
        dst->reached = reached;
        dst->reached_bit = reached_bit;
    }
}

static void trace_snapshot_failure_occurrence(HTraceContext *context, const HParser *parser,
                                              size_t start, size_t reached) {
    if (!context || !parser ||
        context->input_frame_count >= H_PARSE_DIAGNOSTIC_MAX_INPUT_FRAMES)
        return;

    for (size_t i = 0; i < context->input_frame_count; i++) {
        const HTraceFrame *existing = &context->input_frames[i];
        if (existing->parser == parser &&
            (existing->start == start || h_is_context_parser(parser)))
            return;
        if (existing->start == start && h_is_context_parser(existing->parser) &&
            h_context_parser_child(existing->parser) == parser)
            return;
    }

    HTraceFrame *frame = &context->input_frames[context->input_frame_count++];
    memset(frame, 0, sizeof(*frame));
    frame->parser = parser;
    frame->name = parser->vtable ? trace_vt_name(parser->vtable) : "?(no parser)";
    frame->start = start;
    frame->reached = reached;
}

static void trace_snapshot_candidate_frames(HTraceContext *context,
                                            const HTraceCandidateFrame frames[], size_t count,
                                            size_t reached) {
    context->input_frame_count = 0;
    for (size_t i = 0; i < count &&
                       context->input_frame_count < H_PARSE_DIAGNOSTIC_MAX_INPUT_FRAMES;
         i++) {
        const HParser *parser = frames[i].parser;
        if (!parser)
            continue;
        if (context->input_frame_count > 0) {
            const HTraceFrame *previous = &context->input_frames[context->input_frame_count - 1];
            if (h_is_context_parser(previous->parser) &&
                h_context_parser_child(previous->parser) == parser)
                continue;
        }
        HTraceFrame *dst = &context->input_frames[context->input_frame_count++];
        memset(dst, 0, sizeof(*dst));
        dst->parser = parser;
        dst->name = parser->vtable ? trace_vt_name(parser->vtable) : "?(no parser)";
        dst->start = frames[i].start;
        dst->reached = reached;
    }
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
    HTraceDispatchFailure dispatch_failure = context->pending_dispatch_failure;
    memset(&context->pending_dispatch_failure, 0, sizeof(context->pending_dispatch_failure));
    if (dispatch_failure.present)
        kind = H_PARSE_ERROR_DISPATCH;
    bool value_failure = kind == H_PARSE_ERROR_SEMANTIC_PREDICATE || kind == H_PARSE_ERROR_RANGE ||
                         kind == H_PARSE_ERROR_XOR || kind == H_PARSE_ERROR_DIFFERENCE ||
                         kind == H_PARSE_ERROR_BUTNOT || kind == H_PARSE_ERROR_NO_VALUE ||
                         kind == H_PARSE_ERROR_DISPATCH || kind == H_PARSE_ERROR_REUSED_NAME;

    const HParser *label_parser = parser && parser->diagnostic_label ? parser : NULL;
    const HParser *message_parser = parser && parser->diagnostic_message ? parser : NULL;
    const HParser *source_parser = parser && parser->diagnostic_source ? parser : NULL;
    bool context_label = false;
    bool context_message = false;
    bool context_source = false;
    for (size_t i = context->frame_count;
         i > 0 && (!context_label || !context_message || !context_source); i--) {
        const HParser *candidate = context->frames[i - 1].parser;
        if (!candidate)
            continue;
        if (h_is_context_parser(candidate)) {
            if (!context_label && candidate->diagnostic_label) {
                label_parser = candidate;
                context_label = true;
            }
            if (!context_message && candidate->diagnostic_message) {
                message_parser = candidate;
                context_message = true;
            }
            if (!context_source && candidate->diagnostic_source) {
                source_parser = candidate;
                context_source = true;
            }
        } else {
            if (!label_parser && candidate->diagnostic_label)
                label_parser = candidate;
            if (!message_parser && candidate->diagnostic_message)
                message_parser = candidate;
            if (!source_parser && candidate->diagnostic_source)
                source_parser = candidate;
        }
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
        context->choice_root = H_TRACE_CHOICE_NONE;
        memset(error, 0, sizeof(*error));
        memset(context->expected_bytes, 0, sizeof(context->expected_bytes));
        context->expected_eof = false;
        memset(&context->numeric_range, 0, sizeof(context->numeric_range));
        memset(&context->dispatch_failure, 0, sizeof(context->dispatch_failure));
        if (kind == H_PARSE_ERROR_RANGE)
            context->numeric_range = numeric_range;
        if (kind == H_PARSE_ERROR_DISPATCH)
            context->dispatch_failure = dispatch_failure;
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

        trace_snapshot_input_frames(context, end, state->input_stream.bit_offset);
        trace_snapshot_failure_occurrence(context, parser, frame->start, end);
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
    HTraceContext *context = trace_context;
    if (!context)
        return;

    if (!display_trace)
        return;
    if (!dump_trace)
        trace_print_input_trail(stderr, context);
    fprintf(stderr, "=== h_packrat_parse: end (%s) ===\n", res ? "SUCCESS" : "FAILURE");

    trace_complete(context);
    if (!res)
        trace_render_diagnostic(context);

    HTraceContext *parent = context->parent;
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
        fprintf(stderr, "%4d %-10s", i, rvm_op_names[insn->op]);
        const HParser *display_parser =
            h_diagnostic_context_parser(prog->insn_contexts[i], prog->insn_parsers[i]);
        char *parser_name = h_trace_parser_name(display_parser);
        switch (insn->op) {
        case RVM_PUSH:
            if (parser_name) {
                fprintf(stderr, " parser=%s", parser_name);
                free(parser_name);
            }
            trace_print_source(stderr, display_parser);
            break;
        case RVM_GOTO:
        case RVM_FORK:
            fprintf(stderr, "%hd", insn->arg);
            break;
        case RVM_ACTION:
            symref = getsym(prog->actions[insn->arg].action);
            fprintf(stderr, " action=%s in", symref ? symref : "<unknown>");
            free(symref);
            if (parser_name) {
                fprintf(stderr, " parser=%s", parser_name);
                free(parser_name);
            }
            trace_print_source(stderr, display_parser);
            break;
        case RVM_MATCH: {
            uint8_t low, high;
            low = insn->arg & 0xff;
            high = (insn->arg >> 8) & 0xff;
            if (high < low)
                fprintf(stderr, "NONE");
            else {
                if (low >= 0x20 && low <= 0x7e)
                    fprintf(stderr, "%02hhx ('%c')", low, low);
                else
                    fprintf(stderr, "%02hhx", low);

                if (high >= 0x20 && high <= 0x7e)
                    fprintf(stderr, " - %02hhx ('%c')", high, high);
                else
                    fprintf(stderr, " - %02hhx", high);
            }
            break;
        }
        default:
            break;
        }
        fprintf(stderr, "\n");
    }
}

void dump_svm_prog(HRVMProg *prog, HRVMTrace *trace) {
    if (!display_trace)
        return;
    char *symref;
    for (; trace != NULL; trace = trace->next) {
        fprintf(stderr, "@%04zd %-10s", trace->input_pos, svm_op_names[trace->opcode]);
        switch (trace->opcode) {
        case SVM_ACTION:
            symref = getsym(prog->actions[trace->arg].action);
            fprintf(stderr, " action=%s in", symref ? symref : "<unknown>");
            free(symref);
            break;
        default:
            break;
        }

        const HParser *display_parser =
            h_diagnostic_context_parser(trace->diagnostic_context, trace->parser);
        char *parser_name = h_trace_parser_name(display_parser);
        if (parser_name) {
            fprintf(stderr, " parser=%s", parser_name);
            free(parser_name);
        }
        trace_print_source(stderr, display_parser);
        fprintf(stderr, "\n");
    }
}

static void trace_fprint_byte(FILE *stream, uint8_t c) {
    if (c == '\'' || c == '\\')
        fprintf(stream, "'\\%c'", c);
    else if (isprint(c))
        fprintf(stream, "'%c'", c);
    else
        fprintf(stream, "0x%02x", c);
}

static void trace_fprint_expectations(FILE *stream, const bool expected[256], bool expected_eof) {
    bool first = true;

    fputs("; expected ", stream);
    for (unsigned int lo = 0; lo < 256;) {
        if (!expected[lo]) {
            lo++;
            continue;
        }

        unsigned int hi = lo;
        while (hi + 1 < 256 && expected[hi + 1])
            hi++;

        if (!first)
            fputs(", ", stream);
        trace_fprint_byte(stream, (uint8_t)lo);
        if (hi != lo) {
            fputc('-', stream);
            trace_fprint_byte(stream, (uint8_t)hi);
        }
        first = false;
        lo = hi + 1;
    }

    if (expected_eof) {
        if (!first)
            fputs(", ", stream);
        fputs("end of input", stream);
        first = false;
    }
    if (first)
        fputs("a valid input byte", stream);
}

static void trace_fprint_choice_indent(FILE *stream, size_t depth) {
    for (size_t i = 0; i < depth; i++)
        fputs("  ", stream);
}

static void trace_fprint_choice_leaf(FILE *stream, const HTraceChoiceAlternative *alternative,
                                     size_t depth) {
    const HParseError *error = &alternative->error;
    trace_fprint_choice_indent(stream, depth);
    fprintf(stream, "%zu. ", alternative->alternative + 1);
    if (error->parser) {
        if (strncmp(error->parser, "parse_", 6) == 0)
            fprintf(stream, "[h%s] ", error->parser + 5);
        else
            fprintf(stream, "[%s] ", error->parser);
    }
    if (error->message) {
        fputs(error->message, stream);
    } else if (error->kind == H_PARSE_ERROR_SEMANTIC_PREDICATE) {
        fputs("semantic predicate failed", stream);
    } else if (error->kind == H_PARSE_ERROR_RANGE) {
        fputs("value outside accepted range", stream);
    } else if (error->kind == H_PARSE_ERROR_EXPLICIT_FAILURE) {
        fputs("parser always fails", stream);
    } else if (!error->has_actual) {
        fputs("unexpected end of input", stream);
    } else {
        fputs("unexpected byte ", stream);
        trace_fprint_byte(stream, error->actual);
    }
    fprintf(stream, " at index %zu", error->index);
    if (!error->message &&
        (alternative->expected_eof ||
         memchr(alternative->expected_bytes, true, sizeof(alternative->expected_bytes))))
        trace_fprint_expectations(stream, alternative->expected_bytes, alternative->expected_eof);
    if (error->source) {
        fputs(" [", stream);
        fputs(error->source->file_name ? error->source->file_name : "<unknown source>", stream);
        if (error->source->line)
            fprintf(stream, ":%zu", error->source->line);
        if (error->source->column)
            fprintf(stream, ":%zu", error->source->column);
        fputc(']', stream);
    }
    fputc('\n', stream);
}

static void trace_fprint_choice_node(FILE *stream, const HTraceChoiceNode *nodes, size_t node_count,
                                     const HTraceChoiceAlternative *alternatives,
                                     size_t alternative_count, size_t node_index, size_t depth) {
    if (!stream || !nodes || !alternatives || node_index == H_TRACE_CHOICE_NONE ||
        node_index >= node_count)
        return;
    const HTraceChoiceNode *node = &nodes[node_index];
    if (depth == 0)
        fputs("alternatives:\n", stream);
    for (size_t index = node->first_alternative; index != H_TRACE_CHOICE_NONE;
         index = alternatives[index].next) {
        if (index >= alternative_count)
            break;
        const HTraceChoiceAlternative *alternative = &alternatives[index];
        size_t progress = trace_failure_progress(alternative->error.kind, alternative->error.index,
                                                 alternative->error.end_index);
        if (progress != node->furthest_progress)
            continue;
        trace_fprint_choice_leaf(stream, alternative, depth + 1);
        if (alternative->child_node != H_TRACE_CHOICE_NONE)
            trace_fprint_choice_node(stream, nodes, node_count, alternatives, alternative_count,
                                     alternative->child_node, depth + 2);
    }
    if (node->truncated) {
        trace_fprint_choice_indent(stream, depth + 1);
        fputs("... additional alternatives omitted\n", stream);
    }
}

void h_trace_fprint_choice(FILE *stream, const HTraceChoiceNode *nodes, size_t node_count,
                           const HTraceChoiceAlternative *alternatives, size_t alternative_count,
                           size_t root) {
    trace_fprint_choice_node(stream, nodes, node_count, alternatives, alternative_count, root, 0);
}

/* The collector snapshot and public formatter are shared by every backend. */
static void trace_render_diagnostic(HTraceContext *context) {
    if (!context || trace_completed.error.kind == H_PARSE_ERROR_NONE)
        return;
    h_parse_diagnostic_fprint(stderr, &trace_completed);

    const HParseError *error = &trace_completed.error;
    size_t last_index = error->end_index > error->index ? error->end_index - 1 : error->end_index;
    if (context->input && context->input_len > 0)
        h_trace_file_context(context->input, context->input_len, error->index, last_index);
}

void rvm_match_error(HRVMProg *prog, const uint8_t *input, size_t input_len,
                     const HTraceFailureCandidate *candidates, size_t candidate_count,
                     HRVMTrace *trace) {
    if (!display_trace || !candidates || candidate_count == 0)
        return;
    h_backend_trace_begin(PB_REGULAR,
                          prog->root_parser ? prog->root_parser : candidates[0].parser, input,
                          input_len);
    if (dump_trace)
        dump_rvm_prog(prog);
    trace_observe_svm_trace(trace_context, trace);
    h_backend_trace_failures(candidates, candidate_count);
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
    trace_observe_svm_trace(trace_context, trace);
    h_backend_trace_failure(ctx->input_pos, ctx->input_pos, H_PARSE_ERROR_ACTION, ctx->parser,
                            ctx->diagnostic_context, NULL, false);
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
    trace_observe_svm_trace(trace_context, trace);
    if (ctx->failure.kind == SVM_FAILURE_INT_RANGE) {
        HParsedToken actual = {.token_type = ctx->failure.actual_type};
        if (actual.token_type == TT_SINT) {
            actual.token_data.sint = ctx->failure.actual.sint;
            h_trace_note_int_range(&actual, ctx->failure.lower, ctx->failure.upper);
        } else if (actual.token_type == TT_UINT) {
            actual.token_data.uint = ctx->failure.actual.uint;
            h_trace_note_int_range(&actual, ctx->failure.lower, ctx->failure.upper);
        }
    } else if (ctx->failure.kind == SVM_FAILURE_FLOAT_RANGE) {
        HParsedToken actual = {.token_type = ctx->failure.actual_type};
        if (actual.token_type == TT_FLOAT || actual.token_type == TT_DOUBLE) {
            actual.token_type = TT_DOUBLE;
            actual.token_data.dbl = ctx->failure.float_actual;
            h_trace_note_float_range(&actual, ctx->failure.float_lower, ctx->failure.float_upper);
        }
    }
    HParseErrorKind kind;
    switch (ctx->failure.kind) {
    case SVM_FAILURE_INT_RANGE:
    case SVM_FAILURE_FLOAT_RANGE:
        kind = H_PARSE_ERROR_RANGE;
        break;
    case SVM_FAILURE_SEMANTIC_PREDICATE:
        kind = H_PARSE_ERROR_SEMANTIC_PREDICATE;
        break;
    default:
        kind = H_PARSE_ERROR_ACTION;
        break;
    }
    h_backend_trace_failure(ctx->failure.start, ctx->failure.end, kind, ctx->parser,
                            ctx->diagnostic_context, NULL, false);
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
    case H_PARSE_ERROR_DISPATCH:
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
    context->choice_root = H_TRACE_CHOICE_NONE;
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
                             const HDiagnosticContext *provenance, const bool expected[256],
                             bool expected_eof) {
    if (!display_trace || !trace_context)
        return;

    HTraceContext *context = trace_context;
    context->failure_serial++;
    const HParser *semantic_parser = parser ? parser : context->root_parser;
    const HParser *origin = h_diagnostic_context_parser(provenance, semantic_parser);
    while (h_is_context_parser(semantic_parser))
        semantic_parser = h_context_parser_child(semantic_parser);
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
    if (h_is_nothing_parser(semantic_parser)) {
        kind = H_PARSE_ERROR_EXPLICIT_FAILURE;
        end = start;
        expected = NULL;
        expected_eof = false;
    }
    if (kind == H_PARSE_ERROR_SEMANTIC_PREDICATE &&
        (h_is_float_range_parser(semantic_parser) || h_is_int_range_parser(semantic_parser))) {
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
        context->choice_root = H_TRACE_CHOICE_NONE;
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

        if (context->frame_count > 0)
            trace_snapshot_input_frames(context, end, 0);
        else if (context->pending_input_frame_count > 0)
            trace_snapshot_candidate_frames(context, context->pending_input_frames,
                                            context->pending_input_frame_count, end);
        else if (provenance)
            trace_snapshot_provenance(context, provenance, start, end, 0);
        trace_snapshot_failure_occurrence(context, origin, start, end);
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

static const HTraceCandidateChoice *trace_candidate_choice_at(
    const HTraceFailureCandidate *candidate, size_t depth) {
    return candidate && depth < candidate->choice_depth ? &candidate->choices[depth] : NULL;
}

static void trace_report_failure_candidate(const HTraceFailureCandidate *candidate) {
    HTraceContext *context = trace_context;
    if (!candidate || !context)
        return;
    context->pending_input_frames = candidate->input_frames;
    context->pending_input_frame_count = candidate->input_frame_count;
    h_backend_trace_failure(candidate->start, candidate->end, candidate->kind, candidate->parser,
                            candidate->provenance, candidate->expected_bytes,
                            candidate->expected_eof);
    context->pending_input_frames = NULL;
    context->pending_input_frame_count = 0;
}

static void trace_report_choice_candidates(const HTraceFailureCandidate *candidates,
                                           const size_t *indices, size_t count, size_t depth) {
    if (count == 0)
        return;

    const HTraceCandidateChoice *root = NULL;
    for (size_t i = 0; i < count && !root; i++)
        root = trace_candidate_choice_at(&candidates[indices[i]], depth);
    if (!root) {
        for (size_t i = 0; i < count; i++) {
            const HTraceFailureCandidate *candidate = &candidates[indices[i]];
            trace_report_failure_candidate(candidate);
        }
        return;
    }

    size_t scoped[H_TRACE_MAX_FAILURE_CANDIDATES];
    size_t scoped_count = 0;
    for (size_t i = 0; i < count; i++) {
        const HTraceCandidateChoice *item =
            trace_candidate_choice_at(&candidates[indices[i]], depth);
        if (item && item->id == root->id && item->choice == root->choice)
            scoped[scoped_count++] = indices[i];
    }

    size_t scope = trace_choice_begin_at(root->choice, root->origin);
    if (scope == H_TRACE_CHOICE_NONE) {
        for (size_t i = 0; i < scoped_count; i++) {
            const HTraceFailureCandidate *candidate = &candidates[scoped[i]];
            trace_report_failure_candidate(candidate);
        }
        return;
    }

    bool emitted[H_TRACE_MAX_FAILURE_CANDIDATES] = {false};
    for (size_t emitted_count = 0; emitted_count < scoped_count;) {
        size_t i = H_TRACE_MAX_FAILURE_CANDIDATES;
        size_t lowest_alternative = SIZE_MAX;
        for (size_t j = 0; j < scoped_count; j++) {
            if (emitted[j])
                continue;
            const HTraceCandidateChoice *item =
                trace_candidate_choice_at(&candidates[scoped[j]], depth);
            if (item && item->alternative < lowest_alternative) {
                i = j;
                lowest_alternative = item->alternative;
            }
        }
        if (i == H_TRACE_MAX_FAILURE_CANDIDATES)
            break;
        const HTraceCandidateChoice *arm_context =
            trace_candidate_choice_at(&candidates[scoped[i]], depth);
        size_t alternative = arm_context->alternative;
        size_t arm[H_TRACE_MAX_FAILURE_CANDIDATES];
        size_t arm_count = 0;
        for (size_t j = i; j < scoped_count; j++) {
            const HTraceCandidateChoice *item =
                trace_candidate_choice_at(&candidates[scoped[j]], depth);
            if (item && item->alternative == alternative) {
                emitted[j] = true;
                emitted_count++;
                arm[arm_count++] = scoped[j];
            }
        }

        h_trace_choice_arm_begin(scope, alternative);
        trace_report_choice_candidates(candidates, arm, arm_count, depth + 1);
        h_trace_choice_arm_end(scope, false);
    }
    h_trace_choice_end(scope, false);
}

void h_backend_trace_failures(const HTraceFailureCandidate *candidates, size_t count) {
    if (!candidates || count == 0)
        return;
    if (count > H_TRACE_MAX_FAILURE_CANDIDATES)
        count = H_TRACE_MAX_FAILURE_CANDIDATES;
    size_t indices[H_TRACE_MAX_FAILURE_CANDIDATES];
    for (size_t i = 0; i < count; i++)
        indices[i] = i;
    trace_report_choice_candidates(candidates, indices, count, 0);
}

static bool trace_candidate_path_equal(const HTraceFailureCandidate *candidate,
                                       const HTraceCandidateChoice path[], size_t depth) {
    if (candidate->choice_depth != depth)
        return false;
    for (size_t i = 0; i < depth; i++)
        if (candidate->choices[i].id != path[i].id ||
            candidate->choices[i].alternative != path[i].alternative)
            return false;
    return true;
}

static bool trace_candidate_frames_equal(const HTraceFailureCandidate *candidate,
                                         const HTraceCandidateFrame frames[], size_t count) {
    if (candidate->input_frame_count != count)
        return false;
    for (size_t i = 0; i < count; i++)
        if (candidate->input_frames[i].parser != frames[i].parser ||
            candidate->input_frames[i].start != frames[i].start)
            return false;
    return true;
}

typedef struct HTraceCFContinuation_ {
    HCFChoice **items;
    const struct HTraceCFContinuation_ *next;
    size_t choice_depth;
    size_t parser_depth;
    const HParser *origin;
} HTraceCFContinuation;

/* Diagnostic-only prefix walk over the already desugared grammar. It neither
 * executes user callbacks nor mutates productions; continuations represent
 * the remainder of enclosing sequences while nested choices are explored. */
typedef struct HTraceCFWalk_ {
    const uint8_t *input;
    size_t input_pos;
    size_t input_len;
    size_t target;
    HParseErrorKind kind;
    const HParser *fallback;
    HTraceFailureCandidate *candidates;
    size_t count;
} HTraceCFWalk;

static void trace_walk_cf_items(HTraceCFWalk *walk, HCFChoice **items, size_t position,
                                const HTraceCFContinuation *continuation, size_t grammar_depth,
                                HTraceCandidateChoice path[], size_t choice_depth,
                                HTraceCandidateFrame parser_path[], size_t parser_depth,
                                const HParser *origin);

static void trace_add_cf_candidate(HTraceCFWalk *walk, const HCFChoice *symbol,
                                   HTraceCandidateChoice path[], size_t choice_depth,
                                   HTraceCandidateFrame parser_path[], size_t parser_depth) {
    const HDiagnosticContext *provenance = symbol->diagnostic_context;
    const HParser *parser = h_cfchoice_diagnostic_parser(symbol, walk->fallback);
    HTraceFailureCandidate *candidate = NULL;
    for (size_t i = 0; i < walk->count; i++) {
        if (walk->candidates[i].provenance == provenance &&
            walk->candidates[i].parser == parser &&
            trace_candidate_path_equal(&walk->candidates[i], path, choice_depth) &&
            trace_candidate_frames_equal(&walk->candidates[i], parser_path, parser_depth)) {
            candidate = &walk->candidates[i];
            break;
        }
    }
    if (!candidate) {
        if (walk->count >= H_TRACE_MAX_FAILURE_CANDIDATES)
            return;
        candidate = &walk->candidates[walk->count++];
        memset(candidate, 0, sizeof(*candidate));
        candidate->start = walk->target;
        candidate->end = walk->kind == H_PARSE_ERROR_UNEXPECTED_EOF ? walk->target
                                                                    : walk->target + 1;
        candidate->kind = walk->kind;
        candidate->parser = parser;
        candidate->provenance = provenance;
        memcpy(candidate->choices, path, choice_depth * sizeof(path[0]));
        candidate->choice_depth = choice_depth;
        memcpy(candidate->input_frames, parser_path, parser_depth * sizeof(parser_path[0]));
        candidate->input_frame_count = parser_depth;
    }
    if (symbol->type == HCF_END)
        candidate->expected_eof = true;
    else if (symbol->type == HCF_CHAR)
        candidate->expected_bytes[symbol->data.chr] = true;
    else
        for (size_t byte = 0; byte < 256; byte++)
            candidate->expected_bytes[byte] = charset_isset(symbol->data.charset, (uint8_t)byte);
}

static void trace_walk_cf_continuation(HTraceCFWalk *walk, size_t position,
                                       const HTraceCFContinuation *continuation,
                                       size_t grammar_depth, HTraceCandidateChoice path[],
                                       HTraceCandidateFrame parser_path[]) {
    if (!continuation)
        return;
    trace_walk_cf_items(walk, continuation->items, position, continuation->next, grammar_depth + 1,
                        path, continuation->choice_depth, parser_path, continuation->parser_depth,
                        continuation->origin);
}

static void trace_walk_cf_symbol(HTraceCFWalk *walk, const HCFChoice *symbol, size_t position,
                                 const HTraceCFContinuation *continuation, size_t grammar_depth,
                                 HTraceCandidateChoice path[], size_t choice_depth,
                                 HTraceCandidateFrame parser_path[], size_t parser_depth,
                                 const HParser *origin) {
    if (!symbol || grammar_depth >= 64 || walk->count >= H_TRACE_MAX_FAILURE_CANDIDATES ||
        position > walk->target)
        return;
    if (symbol->parser && h_is_context_parser(symbol->parser))
        origin = symbol->parser;
    if (symbol->parser && parser_depth < H_TRACE_MAX_CANDIDATE_FRAMES &&
        (parser_depth == 0 || parser_path[parser_depth - 1].parser != symbol->parser)) {
        parser_path[parser_depth].parser = symbol->parser;
        parser_path[parser_depth].start = position;
        parser_depth++;
    }
    if (symbol->type == HCF_CHOICE) {
        bool is_choice = h_is_choice_parser(symbol->parser) &&
                         choice_depth < H_TRACE_MAX_CANDIDATE_CHOICE_DEPTH;
        size_t alternative = 0;
        for (HCFSequence **sequence = symbol->data.seq; sequence && *sequence;
             sequence++, alternative++) {
            size_t child_choice_depth = choice_depth;
            if (is_choice) {
                HTraceCandidateChoice *entry = &path[child_choice_depth++];
                entry->choice = symbol->parser;
                entry->origin = origin ? origin : symbol->parser;
                entry->alternative = alternative;
                entry->id = (size_t)(uintptr_t)symbol;
            }
            trace_walk_cf_items(walk, (*sequence)->items, position, continuation,
                                grammar_depth + 1, path, child_choice_depth, parser_path,
                                parser_depth, origin);
        }
        return;
    }
    if (symbol->type != HCF_CHAR && symbol->type != HCF_CHARSET && symbol->type != HCF_END)
        return;
    if (position == walk->target) {
        trace_add_cf_candidate(walk, symbol, path, choice_depth, parser_path, parser_depth);
        return;
    }
    if (symbol->type == HCF_END || position < walk->input_pos ||
        position - walk->input_pos >= walk->input_len)
        return;
    uint8_t actual = walk->input[position - walk->input_pos];
    bool matched = symbol->type == HCF_CHAR ? actual == symbol->data.chr
                                           : charset_isset(symbol->data.charset, actual);
    if (matched)
        trace_walk_cf_continuation(walk, position + 1, continuation, grammar_depth, path,
                                   parser_path);
}

static void trace_walk_cf_items(HTraceCFWalk *walk, HCFChoice **items, size_t position,
                                const HTraceCFContinuation *continuation, size_t grammar_depth,
                                HTraceCandidateChoice path[], size_t choice_depth,
                                HTraceCandidateFrame parser_path[], size_t parser_depth,
                                const HParser *origin) {
    if (grammar_depth >= 64 || walk->count >= H_TRACE_MAX_FAILURE_CANDIDATES)
        return;
    if (!items || !*items) {
        trace_walk_cf_continuation(walk, position, continuation, grammar_depth, path, parser_path);
        return;
    }
    HTraceCFContinuation next = {items + 1, continuation, choice_depth, parser_depth, origin};
    trace_walk_cf_symbol(walk, *items, position, &next, grammar_depth + 1, path, choice_depth,
                         parser_path, parser_depth, origin);
}

size_t h_cf_trace_candidates(const HCFChoice *root, const uint8_t *input, size_t input_pos,
                             size_t input_len, size_t start, size_t index, HParseErrorKind kind,
                             const HParser *fallback, HTraceFailureCandidate candidates[]) {
    if (!root || !input || !candidates || start < input_pos || index < start)
        return 0;
    HTraceCFWalk walk = {input, input_pos, input_len, index, kind, fallback, candidates, 0};
    HTraceCandidateChoice path[H_TRACE_MAX_CANDIDATE_CHOICE_DEPTH] = {{0}};
    HTraceCandidateFrame parser_path[H_TRACE_MAX_CANDIDATE_FRAMES] = {{0}};
    trace_walk_cf_symbol(&walk, root, start, NULL, 0, path, 0, parser_path, 0, NULL);
    return walk.count;
}

void h_cf_trace_failure(size_t start, size_t end, HParseErrorKind kind, const HParser *parser,
                        const HDiagnosticContext *provenance, const bool expected[256],
                        bool expected_eof) {
    h_backend_trace_failure(start, end, kind, parser, provenance, expected, expected_eof);
}

void h_cf_trace_parser_enter(const HParser *parser, size_t index, const char *role) {
    if (!display_trace || !trace_context)
        return;

    const HParser *origin = parser ? parser : trace_context->root_parser;
    if (dump_trace) {
        char *name = h_trace_parser_name(origin);
        trace_indent();
        fprintf(stderr, "-> %-20s %-11s @%zu", name ? name : "?(no parser)",
                role ? role : "nonterminal", index);
        trace_print_source(stderr, origin);
        fputc('\n', stderr);
        free(name);
    }

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
    if (!display_trace || !trace_context)
        return;

    if (trace_context->depth > 0)
        trace_context->depth--;
    if (trace_context->overflow_frames > 0)
        trace_context->overflow_frames--;
    else if (trace_context->frame_count > 0)
        trace_context->frame_count--;

    if (dump_trace) {
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

        trace_print_source(stderr, parser);
        fputc('\n', stderr);
    }
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
                         const HParser *parser, const HDiagnosticContext *provenance,
                         const HParsedToken *token) {
    if (!display_trace || !trace_context)
        return;

    trace_observe_provenance(trace_context, provenance, parser, index);
    if (!dump_trace)
        return;

    fprintf(stderr, "@%04zu ", index);
    trace_lr_branch(branch);
    fprintf(stderr, "SHIFT  s%zu -> s%zu", from_state, to_state);
    if (token && token->token_type == TT_UINT) {
        fputs(" input=", stderr);
        trace_fprint_byte(stderr, (uint8_t)token->token_data.uint);
    } else {
        fputs(" input=end-of-input", stderr);
    }
    trace_lr_parser(parser);
    fputc('\n', stderr);
}

void h_cf_trace_lr_reduce(size_t branch, size_t from_state, size_t to_state, size_t length,
                          size_t start, size_t end, const HParser *parser,
                          const HDiagnosticContext *provenance, const HParsedToken *token,
                          bool success) {
    (void)provenance;
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

void h_cf_trace_lr_error(size_t branch, size_t state, size_t index, const HParser *parser,
                         const HDiagnosticContext *provenance) {
    (void)provenance;
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
    } else
        trace_print_input_trail(stderr, context);

    fprintf(stderr, "=== %s: end (%s) ===\n", trace_backend_name(context->backend),
            success ? "SUCCESS" : "FAILURE");

    trace_complete(context);
    if (!success)
        trace_render_diagnostic(context);

    HTraceContext *parent = context->parent;
    trace_context = parent;
    free(context);
}

void h_cf_trace_end(bool success) { h_backend_trace_end(success); }
#endif /* HAMMER_TRACE_AST */
