/* Copyright (c) 2026 Riverside Research */
#include "../trace.h"
#include "parser_internal.h"

typedef struct {
    ssize_t offset;
    int whence;
} HSeek;

static HParseResult *parse_skip(void *env, HParseState *state) {
    size_t n = (uintptr_t)env;

    h_skip_bits(&state->input_stream, n);
    return make_result(state->arena, NULL);
}

static HParseResult *parse_seek(void *env, HParseState *state) {
    HSeek *s = (HSeek *)env;
    HInputStream *stream = &state->input_stream;
    size_t pos;
    size_t start = stream->pos + stream->index;

    /* determine base position */
    switch (s->whence) {
    case SEEK_SET:
        pos = 0;
        break;
    case SEEK_END:
        if (!stream->last_chunk) {  /* the end is not yet known! */
            stream->overrun = true; /* we need more input */
            return NULL;
        }
        pos = h_input_stream_length(stream);
        break;
    case SEEK_CUR:
        pos = h_input_stream_pos(stream);
        break;
    default:
        h_trace_note_failure(stream->trace, H_PARSE_ERROR_HIGHER_ORDER,
                             "seek uses an invalid origin", start, start);
        return NULL; /* invalid argument */
    }

    /* calculate target position and do basic overflow checks */
    if (s->offset < 0 && (size_t)(-(s->offset + 1)) + 1 > pos) {
        h_trace_note_failure(stream->trace, H_PARSE_ERROR_HIGHER_ORDER,
                             "seek target is before the start of input", start, start);
        return NULL; /* underflow */
    }
    if (s->offset > 0 && (size_t)s->offset > SIZE_MAX - pos) {
        h_trace_note_failure(stream->trace, H_PARSE_ERROR_HIGHER_ORDER,
                             "seek target overflows the input position", start, start);
        return NULL; /* overflow */
    }
    pos += s->offset;

    /* perform the seek and check for overrun */
    h_seek_bits(stream, pos);
    if (stream->overrun) {
        h_trace_note_failure(stream->trace, H_PARSE_ERROR_UNEXPECTED_EOF,
                             "seek target exceeds available input", start, start);
        return NULL;
    }

    HParsedToken *tok = a_new(HParsedToken, 1);
    tok->token_type = TT_UINT;
    tok->token_data.uint = pos;
    tok->index = 0;
    tok->bit_length = 0;
    tok->bit_offset = 0;
    return make_result(state->arena, tok);
}

static HParseResult *parse_tell(void *env, HParseState *state) {
    HParsedToken *tok = a_new(HParsedToken, 1);
    tok->token_type = TT_UINT;
    tok->token_data.uint = h_input_stream_pos(&state->input_stream);
    tok->index = 0;
    tok->bit_length = 0;
    tok->bit_offset = 0;
    return make_result(state->arena, tok);
}

static const HParserVtable skip_vt = {
    .name = "h_skip",
    .parse = parse_skip,
    .isValidRegular = h_false,
    .isValidCF = h_false,
    .higher = false,
};

static const HParserVtable seek_vt = {
    .name = "h_seek",
    .parse = parse_seek,
    .isValidRegular = h_false,
    .isValidCF = h_false,
    .higher = false,
};

static const HParserVtable tell_vt = {
    .name = "h_tell",
    .parse = parse_tell,
    .isValidRegular = h_false,
    .isValidCF = h_false,
    .higher = false,
};

HParser *h_skip(size_t n) { return h_skip__m(&system_allocator, n); }

HParser *h_skip__m(HAllocator *mm__, size_t n) {
    return h_new_parser_with_free(mm__, &skip_vt, (void *)n, h_no_free_env);
}

HParser *h_seek(ssize_t offset, int whence) { return h_seek__m(&system_allocator, offset, whence); }

HParser *h_seek__m(HAllocator *mm__, ssize_t offset, int whence) {
    HSeek *env = h_new(HSeek, 1);
    env->offset = offset;
    env->whence = whence;
    return h_new_parser(mm__, &seek_vt, env);
}

HParser *h_tell() { return h_tell__m(&system_allocator); }

HParser *h_tell__m(HAllocator *mm__) { return h_new_parser(mm__, &tell_vt, NULL); }
