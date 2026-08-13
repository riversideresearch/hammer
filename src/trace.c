/* Copyright (c) 2026 Riverside Research */
#include "hammer.h"

#include "backends/regex.h"
#include "internal.h"
#include "platform.h"
#include "trace.h"

/* ------------------------------------------------------------------------- *
 * AST-construction trace.
 *
 * Enabled via HAMMER_TRACE_AST (see trace.h). When enabled, every step of the
 * packrat parse is recorded as an indented execution log so callers can
 * inspect the AST being built bottom-up. h_do_parse() is the single point every
 * (parser, position) pair flows through, so bracketing its entry/exit yields a
 * tree-shaped log: each "-> parse" line is a parser being tried at a position,
 * and the matching "<= OK/FAIL" line shows the token it produced (or failure),
 * including cache ("memoized") hits.
 * ------------------------------------------------------------------------- */
#if HAMMER_TRACE_AST

#include <ctype.h> // isprint()
#include <inttypes.h> // PRIu64 etc.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // memcpy(), memset()

static char *trace_strdup(const char *source) {
    if (!source)
        return NULL;
    size_t length = strlen(source) + 1;
    char *copy = malloc(length);
    if (copy)
        memcpy(copy, source, length);
    return copy;
}

/* Diagnostic offsets should never wrap back into the input buffer. Saturate
 * malformed or overflowing positions at SIZE_MAX so subsequent bounds checks
 * consistently treat them as EOF/out of range. */
static size_t trace_size_add(size_t left, size_t right) {
    return right > SIZE_MAX - left ? SIZE_MAX : left + right;
}

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

struct HTraceState_ {
    bool print_summary;
    FILE *execution_stream;
    HParseDiagnostic completed;
    HTraceContext *context;
};

HTraceState *h_trace_state_new(bool print_summary) {
    HTraceState *trace = calloc(1, sizeof(*trace));
    if (trace) {
        trace->print_summary = print_summary;
        trace->execution_stream = tmpfile();
        trace->completed.choice_root = H_TRACE_CHOICE_NONE;
    }
    return trace;
}

void h_trace_state_free(HTraceState *trace) {
    if (!trace)
        return;
    while (trace->context) {
        HTraceContext *parent = trace->context->parent;
        free(trace->context);
        trace->context = parent;
    }
    if (trace->execution_stream)
        fclose(trace->execution_stream);
    free(trace->completed.execution_trace);
    free(trace);
}

bool h_trace_is_enabled(const HTraceState *trace) { return trace != NULL; }
bool h_trace_is_dump_enabled(const HTraceState *trace) {
    return trace && trace->execution_stream;
}
bool h_trace_should_print_summary(const HTraceState *trace) {
    return trace && trace->print_summary;
}

static void trace_snapshot_execution(HTraceState *trace) {
    if (!trace || !trace->execution_stream)
        return;
    FILE *stream = trace->execution_stream;
    if (fflush(stream) != 0 || fseek(stream, 0, SEEK_END) != 0)
        return;
    long end = ftell(stream);
    if (end < 0 || (uintmax_t)end > SIZE_MAX - 1 || fseek(stream, 0, SEEK_SET) != 0)
        return;
    size_t length = (size_t)end;
    char *text = malloc(length + 1);
    if (!text) {
        fseek(stream, 0, SEEK_END);
        return;
    }
    size_t read = fread(text, 1, length, stream);
    text[read] = '\0';
    free(trace->completed.execution_trace);
    trace->completed.execution_trace = text;
    trace->completed.execution_trace_length = read;
    fseek(stream, 0, SEEK_END);
}

static void trace_complete(HTraceState *trace, const HTraceContext *context) {
    HParseDiagnostic *completed = &trace->completed;
    char *execution_trace = completed->execution_trace;
    size_t execution_trace_length = completed->execution_trace_length;
    memset(completed, 0, sizeof(*completed));
    completed->execution_trace = execution_trace;
    completed->execution_trace_length = execution_trace_length;
    if (context) {
        completed->error = context->error;
        memcpy(completed->expected_bytes, context->expected_bytes,
               sizeof(completed->expected_bytes));
        completed->expected_eof = context->expected_eof;
        completed->numeric_range = context->numeric_range;
        completed->dispatch_failure = context->dispatch_failure;
        memcpy(completed->choice_nodes, context->choice_nodes,
               context->choice_node_count * sizeof(context->choice_nodes[0]));
        completed->choice_node_count = context->choice_node_count;
        memcpy(completed->choice_alternatives, context->choice_alternatives,
               context->choice_alternative_count * sizeof(context->choice_alternatives[0]));
        completed->choice_alternative_count = context->choice_alternative_count;
        completed->choice_root = context->choice_root;
        completed->input_frame_count = context->input_frame_count;
        memcpy(completed->input_frames, context->input_frames,
               context->input_frame_count * sizeof(context->input_frames[0]));
    }
    trace_snapshot_execution(trace);
}

/* Copy the current furthest-failure record out to the caller (see trace.h).
 * out receives its own copy of the struct -- not a pointer into the global --
 * so it stays valid across later parses that overwrite trace_max. The copied
 * deepest_parsers[] entries still point into the tracer's own long-lived name
 * cache, so this shallow copy is safe and needs no ownership transfer. */
static void trace_copy_error(HParseError *out, const HParseError *source) {
    memcpy(out, source, sizeof(*out));
    if (out->parser)
        out->parser = trace_strdup(out->parser);
    if (out->message)
        out->message = trace_strdup(out->message);
    for (size_t i = 0; i < out->n_deepest; i++)
        out->deepest_parsers[i] = trace_strdup(out->deepest_parsers[i]);
    for (size_t i = 0; i < out->n_context; i++)
        out->context[i] = trace_strdup(out->context[i]);
    out->source = NULL;
    if (source->source) {
        HSourceLocation *source_copy = calloc(1, sizeof(*source_copy));
        if (source_copy) {
            source_copy->line = source->source->line;
            source_copy->column = source->source->column;
            if (source->source->file_name)
                source_copy->file_name = trace_strdup(source->source->file_name);
            if (source->source->function_name)
                source_copy->function_name = trace_strdup(source->source->function_name);
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

void h_trace_get_error(const HTraceState *trace, HParseError *out) {
    if (!trace || !out)
        return;
    trace_copy_error(out, &trace->completed.error);
}

void h_trace_get_diagnostic(const HTraceState *trace, HParseDiagnostic **out) {
    if (!trace || !out)
        return;
    *out = calloc(1, sizeof(**out));
    if (!*out)
        return;
    trace_copy_error(&(*out)->error, &trace->completed.error);
    memcpy((*out)->expected_bytes, trace->completed.expected_bytes,
           sizeof((*out)->expected_bytes));
    (*out)->expected_eof = trace->completed.expected_eof;
    (*out)->numeric_range = trace->completed.numeric_range;
    (*out)->dispatch_failure = trace->completed.dispatch_failure;
    (*out)->choice_node_count = trace->completed.choice_node_count;
    for (size_t i = 0; i < trace->completed.choice_node_count; i++)
        (*out)->choice_nodes[i] = trace->completed.choice_nodes[i];
    (*out)->choice_alternative_count = trace->completed.choice_alternative_count;
    for (size_t i = 0; i < trace->completed.choice_alternative_count; i++) {
        (*out)->choice_alternatives[i] = trace->completed.choice_alternatives[i];
        trace_copy_error(&(*out)->choice_alternatives[i].error,
                         &trace->completed.choice_alternatives[i].error);
    }
    (*out)->choice_root = trace->completed.choice_root;
    (*out)->input_frame_count = trace->completed.input_frame_count;
    memcpy((*out)->input_frames, trace->completed.input_frames,
           trace->completed.input_frame_count * sizeof(trace->completed.input_frames[0]));
    (*out)->execution_trace = trace_strdup(trace->completed.execution_trace);
    (*out)->execution_trace_length = (*out)->execution_trace
                                         ? trace->completed.execution_trace_length
                                         : 0;
}

void h_trace_note_int_range(HTraceState *trace, const HParsedToken *token, int64_t lower,
                            int64_t upper) {
    if (!trace || !trace->context || !token)
        return;
    HTraceNumericRange *range = &trace->context->pending_numeric_range;
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

void h_trace_note_float_range(HTraceState *trace, const HParsedToken *token, double lower,
                              double upper) {
    if (!trace || !trace->context || !token)
        return;
    HTraceNumericRange *range = &trace->context->pending_numeric_range;
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

void h_trace_note_dispatch(HTraceState *trace, const HParsedToken *token, bool has_opcode,
                           size_t opcode, const OpcodeMap *map, size_t count) {
    if (!trace || !trace->context || !token)
        return;

    HTraceDispatchFailure *failure = &trace->context->pending_dispatch_failure;
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

static void trace_indent(const HTraceState *trace) {
    size_t depth = trace && trace->context ? trace->context->depth : 0;
    FILE *out = trace ? trace->execution_stream : NULL;
    if (!out)
        return;
    for (size_t i = 0; i < depth; i++)
        fputs("  ", out);
}

static const char *trace_vt_name(const HParserVtable *vt) {
    return vt && vt->name ? vt->name : "?(unnamed parser)";
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
    return trace_strdup(name);
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
    size_t abs = trace_size_add(in->pos, in->index);
    FILE *out = in->trace ? in->trace->execution_stream : NULL;
    if (!out)
        return;
    fprintf(out, "@%zu", abs);
    if (in->bit_offset)
        fprintf(out, ".%db", in->bit_offset);
}

// print a one-line summary of the token an HParseResult carries
static void trace_token(FILE *out, const HParsedToken *tok) {
    if (!out)
        return;
    if (!tok) {
        fputs("(null ast)", out);
        return;
    }
    switch (tok->token_type) {
    case TT_UINT:
        fprintf(out, "UINT %" PRIu64 " (0x%02" PRIx64 ")", tok->token_data.uint,
                tok->token_data.uint);
        break;
    case TT_SINT:
        fprintf(out, "SINT %" PRId64, tok->token_data.sint);
        break;
    case TT_BYTES:
        fprintf(out, "BYTES[%zu]", tok->token_data.bytes.len);
        break;
    case TT_SEQUENCE:
        fprintf(out, "SEQUENCE[%zu children]",
                tok->token_data.seq ? tok->token_data.seq->used : (size_t)0);
        break;
    default:
        fputs(trace_tt_name(tok->token_type), out);
        break;
    }
}

#define BYTES_PER_LINE 16
#define MAX_CONTEXT_BYTES 256
void h_trace_fprint_input_context(FILE *stream, const uint8_t *input, size_t length,
                                  size_t start_highlight, size_t end_highlight) {
    if (!stream)
        return;
    const char *color_red = "\x1b[31m";
    const char *color_reset = "\x1b[0m";
    int use_color = h_platform_is_terminal(stream);

    fprintf(stream, "=== h_parse_debug: input context (%zu bytes) ===\n", length);

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
        fprintf(stream, "... %zu byte(s) omitted ...\n", window_start);

    for (size_t off = window_start; off < window_end;) {
        size_t line_len = (window_end - off < BYTES_PER_LINE) ? (window_end - off) : BYTES_PER_LINE;
        fprintf(stream, "%04zx:  ", off);

        /* Hex bytes, grouped by 4 for readability */
        for (size_t i = 0; i < BYTES_PER_LINE; ++i) {
            size_t idx = off + i;
            if (i < line_len) {
                int is_highlight = (idx >= start_highlight && idx <= end_highlight);
                if (use_color && is_highlight)
                    fputs(color_red, stream);
                fprintf(stream, "%02x", input[idx]);
                if (use_color && is_highlight)
                    fputs(color_reset, stream);
            } else {
                fputs("  ", stream);
            }
            if ((i & 3) == 3)
                fputs("  ", stream);
            else
                fputc(' ', stream);
        }

        /* ASCII column */
        fputc(' ', stream);
        for (size_t i = 0; i < line_len; ++i) {
            size_t idx = off + i;
            uint8_t c = input[idx];
            int is_highlight = (idx >= start_highlight && idx <= end_highlight);
            if (use_color && is_highlight)
                fputs(color_red, stream);
            fputc(isprint(c) ? (char)c : '.', stream);
            if (use_color && is_highlight)
                fputs(color_reset, stream);
        }
        fprintf(stream, "\n");
        off += line_len;
    }

    if (window_end < length)
        fprintf(stream, "... %zu byte(s) omitted ...\n", length - window_end);
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

void h_trace_fprint_input_trail(FILE *stream, const HTraceFrame *frames, size_t frame_count) {
    if (!stream || !frames || frame_count == 0)
        return;

    fputs("input trail:\n", stream);
    for (size_t i = 0; i < frame_count; i++) {
        const HTraceFrame *frame = &frames[i];
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

void h_trace_begin(HTraceState *trace, const uint8_t *input, size_t input_len) {
    if (!trace)
        return;
    HTraceContext *context = calloc(1, sizeof(*context));
    if (!context)
        return;
    context->input = input;
    context->input_len = input_len;
    context->backend = PB_PACKRAT;
    context->choice_root = H_TRACE_CHOICE_NONE;
    context->parent = trace->context;
    trace->context = context;
    if (trace->execution_stream)
        fprintf(trace->execution_stream, "\n=== h_packrat_parse: begin (%zu bytes of input) ===\n",
                input_len);
}

void h_trace_enter(const HParser *parser, HParseState *state) {
    HTraceState *trace = state->input_stream.trace;
    if (!trace)
        return;
    if (trace->execution_stream) {
        FILE *out = trace->execution_stream;
        char *parser_name = h_trace_parser_name(parser);
        trace_indent(trace);
        fprintf(out, "-> %-20s %-9s ", parser_name ? parser_name : "?(no parser)",
                parser->vtable->higher ? "higher" : "primitive");
        trace_pos(state);
        trace_print_source(out, parser);
        fputc('\n', out);
        free(parser_name);
    }
    if (trace->context && !trace->context->root_parser)
        trace->context->root_parser = parser;
    if (trace->context && trace->context->frame_count < H_TRACE_MAX_FRAMES) {
        HTraceFrame *frame = &trace->context->frames[trace->context->frame_count];
        frame->parser = parser;
        frame->name = trace_vt_name(parser->vtable);
        frame->start = trace_size_add(state->input_stream.pos, state->input_stream.index);
        frame->start_bit = state->input_stream.bit_offset;
        frame->failure_serial = trace->context->failure_serial;
        trace->context->frame_count++;
    } else if (trace->context) {
        trace->context->overflow_frames++;
    }
    if (trace->context)
        trace->context->depth++;
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

static size_t trace_choice_begin_at(HTraceState *trace, const HParser *parser,
                                    const HParser *origin) {
    if (!trace || !trace->context ||
        trace->context->choice_depth >= H_TRACE_MAX_CHOICE_DEPTH || !parser)
        return H_TRACE_CHOICE_NONE;

    HTraceContext *context = trace->context;
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

size_t h_trace_choice_begin(HTraceState *trace) {
    if (!trace || !trace->context || trace->context->frame_count == 0)
        return H_TRACE_CHOICE_NONE;
    const HTraceFrame *frame = &trace->context->frames[trace->context->frame_count - 1];
    return trace_choice_begin_at(trace, frame->parser,
                                 trace_choice_origin(trace->context, frame->parser));
}

static HTraceChoiceScope *trace_choice_scope(HTraceContext *context, size_t id) {
    if (!context || id == H_TRACE_CHOICE_NONE || context->choice_depth == 0)
        return NULL;
    HTraceChoiceScope *scope = &context->choice_scopes[context->choice_depth - 1];
    return scope->id == id ? scope : NULL;
}

void h_trace_choice_arm_begin(HTraceState *trace, size_t id, size_t alternative) {
    HTraceContext *context = trace ? trace->context : NULL;
    HTraceChoiceScope *scope = trace_choice_scope(context, id);
    if (!scope)
        return;
    scope->active_alternative = alternative;
    scope->arm_failure_serial = context->failure_serial;
    trace_state_clear(context);
}

void h_trace_choice_arm_end(HTraceState *trace, size_t id, bool success) {
    HTraceContext *context = trace ? trace->context : NULL;
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

void h_trace_choice_end(HTraceState *trace, size_t id, bool success) {
    HTraceContext *context = trace ? trace->context : NULL;
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
    HTraceContext *context = state->input_stream.trace->context;
    HParseError *error = &context->error;
    size_t end = trace_size_add(state->input_stream.pos, state->input_stream.index);
    bool expected[256];
    bool expected_eof;
    size_t failure_offset = trace_collect_expectations(
        parser, frame, end, state->input_stream.overrun, expected, &expected_eof);
    size_t index =
        parser->vtable->higher ? end : trace_size_add(frame->start, failure_offset);
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
        if (kind != H_PARSE_ERROR_EXPLICIT_FAILURE && context->input &&
            index < context->input_len) {
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
    HTraceState *trace = state->input_stream.trace;
    if (!trace)
        return;
    HTraceContext *context = trace->context;
    HTraceFrame frame = {0};
    bool have_frame = context && context->frame_count > 0 && context->overflow_frames == 0;
    if (context && context->depth > 0)
        context->depth--;
    if (context && context->overflow_frames > 0) {
        context->overflow_frames--;
    }
    if (have_frame) {
        frame = context->frames[context->frame_count - 1];
        context->frame_count--;
    }
    if (!res && have_frame) {
        bool originated_here = frame.failure_serial == context->failure_serial;
        context->failure_serial++;
        if (originated_here)
            trace_record_failure(parser, state, &frame);
    }
    if (!trace->execution_stream)
        return;
    FILE *out = trace->execution_stream;
    trace_indent(trace);
    if (res) {
        fputs("<= OK   ast=", out);
        trace_token(out, res->ast);
        fprintf(out, " (%" PRId64 " bits)", res->bit_length);
    } else {
        fputs("<= FAIL", out);
    }
    if (note)
        fprintf(out, "  [%s]", note);
    fputc('\n', out);
}

void h_trace_end(HParseResult *res, HParseState *state) {
    HTraceState *trace = state->input_stream.trace;
    HTraceContext *context = trace ? trace->context : NULL;
    if (!context)
        return;

    if (trace->execution_stream)
        fprintf(trace->execution_stream, "=== h_packrat_parse: end (%s) ===\n",
                res ? "SUCCESS" : "FAILURE");

    trace_complete(trace, context);

    HTraceContext *parent = context->parent;
    trace->context = parent;
    free(context);
}

const char *rvm_op_names[RVM_OPCOUNT] = {"ACCEPT",  "GOTO", "FORK",  "PUSH", "ACTION",
                                         "CAPTURE", "EOF",  "MATCH", "STEP"};

const char *svm_op_names[SVM_OPCOUNT] = {"PUSH", "NOP", "ACTION", "CAPTURE", "ACCEPT"};

void dump_rvm_prog(HTraceState *trace_state, HRVMProg *prog) {
    FILE *out = trace_state ? trace_state->execution_stream : NULL;
    if (!out)
        return;
    for (unsigned int i = 0; i < prog->length; i++) {
        HRVMInsn *insn = &prog->insns[i];
        fprintf(out, "%4d %-10s", i, rvm_op_names[insn->op]);
        const HParser *display_parser =
            h_diagnostic_context_parser(prog->insn_contexts[i], prog->insn_parsers[i]);
        char *parser_name = h_trace_parser_name(display_parser);
        switch (insn->op) {
        case RVM_PUSH:
            if (parser_name) {
                fprintf(out, " parser=%s", parser_name);
                free(parser_name);
            }
            trace_print_source(out, display_parser);
            break;
        case RVM_GOTO:
        case RVM_FORK:
            fprintf(out, "%hd", insn->arg);
            break;
        case RVM_ACTION:
            if (parser_name) {
                fprintf(out, " parser=%s", parser_name);
                free(parser_name);
            }
            trace_print_source(out, display_parser);
            break;
        case RVM_MATCH: {
            uint8_t low, high;
            low = insn->arg & 0xff;
            high = (insn->arg >> 8) & 0xff;
            if (high < low)
                fprintf(out, "NONE");
            else {
                if (low >= 0x20 && low <= 0x7e)
                    fprintf(out, "%02hhx ('%c')", low, low);
                else
                    fprintf(out, "%02hhx", low);

                if (high >= 0x20 && high <= 0x7e)
                    fprintf(out, " - %02hhx ('%c')", high, high);
                else
                    fprintf(out, " - %02hhx", high);
            }
            break;
        }
        default:
            break;
        }
        fprintf(out, "\n");
    }
}

void dump_svm_prog(HTraceState *trace_state, HRVMProg *prog, HRVMTrace *trace) {
    (void)prog;
    FILE *out = trace_state ? trace_state->execution_stream : NULL;
    if (!out)
        return;
    for (; trace != NULL; trace = trace->next) {
        fprintf(out, "@%04zd %-10s", trace->input_pos, svm_op_names[trace->opcode]);

        const HParser *display_parser =
            h_diagnostic_context_parser(trace->diagnostic_context, trace->parser);
        char *parser_name = h_trace_parser_name(display_parser);
        if (parser_name) {
            fprintf(out, " parser=%s", parser_name);
            free(parser_name);
        }
        trace_print_source(out, display_parser);
        fprintf(out, "\n");
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

void rvm_match_error(HTraceState *trace_state, HRVMProg *prog, const uint8_t *input,
                     size_t input_len,
                     const HTraceFailureCandidate *candidates, size_t candidate_count,
                     HRVMTrace *trace) {
    if (!trace_state || !candidates || candidate_count == 0)
        return;
    h_backend_trace_begin(trace_state, PB_REGULAR,
                          prog->root_parser ? prog->root_parser : candidates[0].parser, input,
                          input_len);
    if (trace_state->execution_stream)
        dump_rvm_prog(trace_state, prog);
    trace_observe_svm_trace(trace_state->context, trace);
    h_backend_trace_failures(trace_state, candidates, candidate_count);
    h_backend_trace_end(trace_state, false);
}
void svm_action_error(HSVMContext *ctx, HRVMProg *orig_prog, HRVMTrace *trace, const uint8_t *input,
                      size_t input_len, const char *msg) {
    HTraceState *trace_state = ctx->trace_state;
    if (!trace_state)
        return;
    (void)msg;
    h_backend_trace_begin(trace_state, PB_REGULAR,
                          orig_prog->root_parser ? orig_prog->root_parser : ctx->parser, input,
                          input_len);
    if (trace_state->execution_stream)
        dump_svm_prog(trace_state, orig_prog, trace);
    trace_observe_svm_trace(trace_state->context, trace);
    h_backend_trace_failure(trace_state, ctx->input_pos, ctx->input_pos, H_PARSE_ERROR_ACTION,
                            ctx->parser, ctx->diagnostic_context, NULL, false);
    h_backend_trace_end(trace_state, false);
}

void svm_failure_error(HSVMContext *ctx, HRVMProg *orig_prog, HRVMTrace *trace,
                       const uint8_t *input, size_t input_len) {
    HTraceState *trace_state = ctx->trace_state;
    if (!trace_state)
        return;
    h_backend_trace_begin(trace_state, PB_REGULAR,
                          orig_prog->root_parser ? orig_prog->root_parser : ctx->parser, input,
                          input_len);
    if (trace_state->execution_stream)
        dump_svm_prog(trace_state, orig_prog, trace);
    trace_observe_svm_trace(trace_state->context, trace);
    if (ctx->failure.kind == SVM_FAILURE_INT_RANGE) {
        HParsedToken actual = {.token_type = ctx->failure.actual_type};
        if (actual.token_type == TT_SINT) {
            actual.token_data.sint = ctx->failure.actual.sint;
            h_trace_note_int_range(trace_state, &actual, ctx->failure.lower, ctx->failure.upper);
        } else if (actual.token_type == TT_UINT) {
            actual.token_data.uint = ctx->failure.actual.uint;
            h_trace_note_int_range(trace_state, &actual, ctx->failure.lower, ctx->failure.upper);
        }
    } else if (ctx->failure.kind == SVM_FAILURE_FLOAT_RANGE) {
        HParsedToken actual = {.token_type = ctx->failure.actual_type};
        if (actual.token_type == TT_FLOAT || actual.token_type == TT_DOUBLE) {
            actual.token_type = TT_DOUBLE;
            actual.token_data.dbl = ctx->failure.float_actual;
            h_trace_note_float_range(trace_state, &actual, ctx->failure.float_lower,
                                     ctx->failure.float_upper);
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
    h_backend_trace_failure(trace_state, ctx->failure.start, ctx->failure.end, kind, ctx->parser,
                            ctx->diagnostic_context, NULL, false);
    h_backend_trace_end(trace_state, false);
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

void h_backend_trace_begin(HTraceState *trace, HParserBackend backend, const HParser *parser,
                           const uint8_t *input, size_t input_len) {
    if (!trace)
        return;

    HTraceContext *context = calloc(1, sizeof(*context));
    if (!context)
        return;
    context->input = input;
    context->input_len = input_len;
    context->backend = backend;
    context->root_parser = parser;
    context->choice_root = H_TRACE_CHOICE_NONE;
    context->parent = trace->context;
    trace->context = context;

    if (trace->execution_stream)
        fprintf(trace->execution_stream, "\n=== %s: begin (%zu bytes of input) ===\n",
                trace_backend_name(backend), input_len);
}

void h_cf_trace_begin(HTraceState *trace, HParserBackend backend, const HParser *parser,
                      const uint8_t *input, size_t input_len) {
    h_backend_trace_begin(trace, backend, parser, input, input_len);
}

void h_backend_trace_failure(HTraceState *trace, size_t start, size_t end, HParseErrorKind kind,
                             const HParser *parser, const HDiagnosticContext *provenance,
                             const bool expected[256], bool expected_eof) {
    if (!trace || !trace->context)
        return;

    HTraceContext *context = trace->context;
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
        if (kind != H_PARSE_ERROR_EXPLICIT_FAILURE && context->input &&
            index < context->input_len) {
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

static void trace_report_failure_candidate(HTraceState *trace,
                                           const HTraceFailureCandidate *candidate) {
    HTraceContext *context = trace ? trace->context : NULL;
    if (!candidate || !context)
        return;
    context->pending_input_frames = candidate->input_frames;
    context->pending_input_frame_count = candidate->input_frame_count;
    h_backend_trace_failure(trace, candidate->start, candidate->end, candidate->kind,
                            candidate->parser, candidate->provenance, candidate->expected_bytes,
                            candidate->expected_eof);
    context->pending_input_frames = NULL;
    context->pending_input_frame_count = 0;
}

static void trace_report_choice_candidates(HTraceState *trace,
                                           const HTraceFailureCandidate *candidates,
                                           const size_t *indices, size_t count, size_t depth) {
    if (count == 0)
        return;

    const HTraceCandidateChoice *root = NULL;
    for (size_t i = 0; i < count && !root; i++)
        root = trace_candidate_choice_at(&candidates[indices[i]], depth);
    if (!root) {
        for (size_t i = 0; i < count; i++) {
            const HTraceFailureCandidate *candidate = &candidates[indices[i]];
            trace_report_failure_candidate(trace, candidate);
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

    size_t scope = trace_choice_begin_at(trace, root->choice, root->origin);
    if (scope == H_TRACE_CHOICE_NONE) {
        for (size_t i = 0; i < scoped_count; i++) {
            const HTraceFailureCandidate *candidate = &candidates[scoped[i]];
            trace_report_failure_candidate(trace, candidate);
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

        h_trace_choice_arm_begin(trace, scope, alternative);
        trace_report_choice_candidates(trace, candidates, arm, arm_count, depth + 1);
        h_trace_choice_arm_end(trace, scope, false);
    }
    h_trace_choice_end(trace, scope, false);
}

void h_backend_trace_failures(HTraceState *trace, const HTraceFailureCandidate *candidates,
                              size_t count) {
    if (!candidates || count == 0)
        return;
    if (count > H_TRACE_MAX_FAILURE_CANDIDATES)
        count = H_TRACE_MAX_FAILURE_CANDIDATES;
    size_t indices[H_TRACE_MAX_FAILURE_CANDIDATES];
    for (size_t i = 0; i < count; i++)
        indices[i] = i;
    trace_report_choice_candidates(trace, candidates, indices, count, 0);
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
        candidate->end = walk->kind == H_PARSE_ERROR_UNEXPECTED_EOF
                             ? walk->target
                             : trace_size_add(walk->target, 1);
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
    if (matched && position != SIZE_MAX)
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
    if (!root || (!input && input_len > 0) || !candidates || start < input_pos || index < start)
        return 0;
    HTraceCFWalk walk = {input, input_pos, input_len, index, kind, fallback, candidates, 0};
    HTraceCandidateChoice path[H_TRACE_MAX_CANDIDATE_CHOICE_DEPTH] = {{0}};
    HTraceCandidateFrame parser_path[H_TRACE_MAX_CANDIDATE_FRAMES] = {{0}};
    trace_walk_cf_symbol(&walk, root, start, NULL, 0, path, 0, parser_path, 0, NULL);
    return walk.count;
}

void h_cf_trace_failure(HTraceState *trace, size_t start, size_t end, HParseErrorKind kind,
                        const HParser *parser, const HDiagnosticContext *provenance,
                        const bool expected[256], bool expected_eof) {
    h_backend_trace_failure(trace, start, end, kind, parser, provenance, expected, expected_eof);
}

void h_cf_trace_parser_enter(HTraceState *trace, const HParser *parser, size_t index,
                             const char *role) {
    if (!trace || !trace->context)
        return;

    HTraceContext *context = trace->context;
    const HParser *origin = parser ? parser : context->root_parser;
    if (trace->execution_stream) {
        FILE *out = trace->execution_stream;
        char *name = h_trace_parser_name(origin);
        trace_indent(trace);
        fprintf(out, "-> %-20s %-11s @%zu", name ? name : "?(no parser)",
                role ? role : "nonterminal", index);
        trace_print_source(out, origin);
        fputc('\n', out);
        free(name);
    }

    if (context->frame_count < H_TRACE_MAX_FRAMES) {
        HTraceFrame *frame = &context->frames[context->frame_count++];
        frame->parser = origin;
        frame->name = origin && origin->vtable ? trace_vt_name(origin->vtable) : "?(no parser)";
        frame->start = index;
        frame->start_bit = 0;
        frame->failure_serial = 0;
    } else {
        context->overflow_frames++;
    }
    context->depth++;
}

void h_cf_trace_parser_exit(HTraceState *trace, const HParser *parser, size_t start, size_t end,
                            const HParsedToken *token, bool success, const char *role) {
    (void)parser;
    if (!trace || !trace->context)
        return;

    HTraceContext *context = trace->context;
    if (context->depth > 0)
        context->depth--;
    if (context->overflow_frames > 0)
        context->overflow_frames--;
    else if (context->frame_count > 0)
        context->frame_count--;

    if (trace->execution_stream) {
        FILE *out = trace->execution_stream;
        trace_indent(trace);
        if (success) {
            fputs("<= OK   ast=", out);
            trace_token(out, token);
            fprintf(out, " span=[%zu,%zu)", start, end);
        } else {
            fputs("<= FAIL", out);
            fprintf(out, " @%zu", end);
        }
        if (role)
            fprintf(out, "  [%s]", role);

        trace_print_source(out, parser);
        fputc('\n', out);
    }
}

static void trace_lr_branch(const HTraceState *trace, size_t branch) {
    if (trace && trace->context && trace->context->backend == PB_GLR)
        fprintf(trace->execution_stream, "[branch %zu] ", branch);
}

static void trace_lr_parser(HTraceState *trace, const HParser *parser) {
    char *name = h_trace_parser_name(parser);
    if (name) {
        fprintf(trace->execution_stream, " parser=%s", name);
        free(name);
    }
}

void h_cf_trace_lr_shift(HTraceState *trace, size_t branch, size_t from_state, size_t to_state,
                         size_t index,
                         const HParser *parser, const HDiagnosticContext *provenance,
                         const HParsedToken *token) {
    if (!trace || !trace->context)
        return;

    trace_observe_provenance(trace->context, provenance, parser, index);
    if (!trace->execution_stream)
        return;

    FILE *out = trace->execution_stream;
    fprintf(out, "@%04zu ", index);
    trace_lr_branch(trace, branch);
    fprintf(out, "SHIFT  s%zu -> s%zu", from_state, to_state);
    if (token && token->token_type == TT_UINT) {
        fputs(" input=", out);
        trace_fprint_byte(out, (uint8_t)token->token_data.uint);
    } else {
        fputs(" input=end-of-input", out);
    }
    trace_lr_parser(trace, parser);
    fputc('\n', out);
}

void h_cf_trace_lr_reduce(HTraceState *trace, size_t branch, size_t from_state, size_t to_state,
                          size_t length,
                          size_t start, size_t end, const HParser *parser,
                          const HDiagnosticContext *provenance, const HParsedToken *token,
                          bool success) {
    (void)provenance;
    if (!trace || !trace->execution_stream || !trace->context)
        return;

    FILE *out = trace->execution_stream;
    fprintf(out, "@%04zu ", end);
    trace_lr_branch(trace, branch);
    if (!success) {
        fprintf(out, "REJECT  s%zu len=%zu span=[%zu,%zu)", from_state, length, start, end);
    } else if (to_state == SIZE_MAX) {
        fprintf(out, "REDUCE  s%zu -> accept len=%zu span=[%zu,%zu)", from_state, length, start,
                end);
    } else {
        fprintf(out, "REDUCE  s%zu -> s%zu len=%zu span=[%zu,%zu)", from_state, to_state, length,
                start, end);
    }
    trace_lr_parser(trace, parser);
    if (success) {
        fputs(" ast=", out);
        trace_token(out, token);
    }
    fputc('\n', out);
}

void h_cf_trace_lr_error(HTraceState *trace, size_t branch, size_t state, size_t index,
                         const HParser *parser,
                         const HDiagnosticContext *provenance) {
    (void)provenance;
    if (!trace || !trace->execution_stream || !trace->context)
        return;

    FILE *out = trace->execution_stream;
    fprintf(out, "@%04zu ", index);
    trace_lr_branch(trace, branch);
    fprintf(out, "ERROR  no action in s%zu", state);
    trace_lr_parser(trace, parser);
    fputc('\n', out);
}

size_t h_cf_trace_glr_fork(HTraceState *trace, size_t branch, size_t state, size_t index) {
    if (!trace || !trace->execution_stream || !trace->context)
        return branch;

    size_t child = ++trace->context->next_branch_id;
    fprintf(trace->execution_stream, "@%04zu [branch %zu] FORK -> branch %zu at s%zu\n", index,
            branch, child, state);
    return child;
}

void h_cf_trace_glr_merge(HTraceState *trace, size_t survivor, size_t merged, size_t state,
                          size_t index) {
    if (!trace || !trace->execution_stream || !trace->context)
        return;
    fprintf(trace->execution_stream, "@%04zu [branch %zu] MERGE branch %zu at s%zu\n", index,
            survivor, merged, state);
}

void h_backend_trace_end(HTraceState *trace, bool success) {
    HTraceContext *context = trace ? trace->context : NULL;
    if (!context)
        return;

    if (trace->execution_stream) {
        FILE *out = trace->execution_stream;
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
            trace_indent(trace);
            char *name = frame->parser ? h_trace_parser_name(frame->parser) : NULL;
            fprintf(out, "<= FAIL  [aborted %s from @%zu]\n", name ? name : frame->name,
                    frame->start);
            free(name);
        }
        fprintf(out, "=== %s: end (%s) ===\n", trace_backend_name(context->backend),
                success ? "SUCCESS" : "FAILURE");
    }

    trace_complete(trace, context);

    HTraceContext *parent = context->parent;
    trace->context = parent;
    free(context);
}

void h_cf_trace_end(HTraceState *trace, bool success) { h_backend_trace_end(trace, success); }
#endif /* HAMMER_TRACE_AST */
