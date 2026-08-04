#include "dns.h"

#include <hammer/glue.h>
#include <hammer/hammer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const uint16_t valid_types[] = {
    1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,    12,   13,  14,  15,  16,  17,
    18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,    29,   30,  31,  32,  33,  34,
    35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,    46,   47,  48,  49,  50,  51,
    52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,    63,   64,  65,  66,  67,  68,
    99,  100, 101, 102, 103, 104, 105, 106, 107, 108, 109,   249,  250, 251, 252, 253, 254,
    255, 256, 257, 258, 259, 260, 261, 262, 263, 264, 32768, 32769};

const size_t valid_types_count = sizeof(valid_types) / sizeof(valid_types[0]);

int binarySearch(const uint16_t arr[], int left, int right, int key) {
    if (right >= left) {
        int mid = left + (right - left) / 2;

        if (arr[mid] == key)
            return mid;
        if (arr[mid] > key)
            return binarySearch(arr, left, mid - 1, key);
        return binarySearch(arr, mid + 1, right, key);
    }
    return -1;
}

bool is_zero(HParseResult *result, void *user_data) {
    return result && result->ast && result->ast->uint == 0;
}
bool validate_pointer(HParseResult *result, void *user) {
    uint16_t curr = ((DNSContext *)user)->curr_storage;
    uint16_t len = ((DNSContext *)user)->packet_len;
    uint16_t pValue = (uint16_t)result->ast->uint;
    if (pValue < curr && pValue < len)
        return true;
    else
        return false;
}
bool validate_type(HParseResult *result, void *user_data) {
    uint16_t type = (uint16_t)result->ast->uint;
    int res = binarySearch(valid_types, 0, valid_types_count - 1, type);
    if (res >= 0)
        return true;
    else
        return false;
}
bool validate_rclass(HParseResult *result, void *user_data) {
    uint16_t class = (uint16_t)result->ast->uint;
    if (class >= 1 && class <= 4)
        return true;
    else
        return false;
}
bool validate_qclass(HParseResult *result, void *user_data) {
    uint16_t class = (uint16_t)result->ast->uint;
    if ((class >= 1 && class <= 4) || class == 254 || class == 255)
        return true;
    else
        return false;
}

bool validate_pflag(HParseResult *result, void *user_data) {
    return result && result->ast && result->ast->uint == 3;
}

HParsedToken *store_qdcount(const HParseResult *result, void *user) {
    if (user && result && result->ast) {
        ((DNSContext *)user)->qdcount = (uint16_t)result->ast->uint;
    }
    return (HParsedToken *)result->ast;
}
HParsedToken *store_ancount(const HParseResult *result, void *user) {
    if (user && result && result->ast) {
        ((DNSContext *)user)->ancount = (uint16_t)result->ast->uint;
    }
    return (HParsedToken *)result->ast;
}
HParsedToken *store_nscount(const HParseResult *result, void *user) {
    if (user && result && result->ast) {
        ((DNSContext *)user)->nscount = (uint16_t)result->ast->uint;
    }
    return (HParsedToken *)result->ast;
}
HParsedToken *store_arcount(const HParseResult *result, void *user) {
    if (user && result && result->ast) {
        ((DNSContext *)user)->arcount = (uint16_t)result->ast->uint;
    }
    return (HParsedToken *)result->ast;
}

HParsedToken *updateCurr(const HParseResult *result, void *user) {
    if (result && result->ast) {
        uint32_t bitpos = (uint32_t)result->ast->uint;
        ((DNSContext *)user)->curr_storage = (uint16_t)bitpos / 8; // position in bytes
    }
    return (HParsedToken *)result->ast;
}

// Parser starts here
HParser *headerParser(DNSContext *ctx) {
    HParser *ID = h_uint16();

    HParser *QR = h_bits(1, false);
    HParser *opcode = h_bits(4, false);
    HParser *AA = h_bits(1, false);
    HParser *TC = h_bits(1, false);
    HParser *RD = h_bits(1, false);
    HParser *RA = h_bits(1, false);
    HParser *zero = h_bits(3, false);
    HParser *verified_zero = h_attr_bool(zero, is_zero, ctx);
    HParser *rCode = h_bits(4, false);
    HParser *flags = h_sequence(QR, opcode, AA, TC, RD, RA, verified_zero, rCode, NULL);

    HParser *QDCount = h_action(h_uint16(), store_qdcount, ctx);
    HParser *ANCount = h_action(h_uint16(), store_ancount, ctx);
    HParser *NSCount = h_action(h_uint16(), store_nscount, ctx);
    HParser *ARCount = h_action(h_uint16(), store_arcount, ctx);

    HParser *header = h_sequence(ID, flags, QDCount, ANCount, NSCount, ARCount, NULL);
    return header;
}

HParser *bodyParser(DNSContext *ctx) {
    HParser *label = h_length_value(h_ch_range(0x01, 0x3f), h_ch_range(0x00, 0xff));
    HParser *pflag = h_bits(2, false);
    HParser *vpflag = h_attr_bool(pflag, validate_pflag, ctx);
    HParser *temppointer = h_bits(14, false);
    HParser *curr = h_tell();
    HParser *newCurr = h_action(curr, updateCurr, ctx);
    HParser *vpointer = h_attr_bool(temppointer, validate_pointer, ctx);
    HParser *pointer = h_sequence(vpflag, newCurr, vpointer, NULL);
    HParser *name_termination = h_ch(0x00);
    HParser *name_end = h_choice(name_termination, pointer, NULL);
    HParser *QName =
        h_choice(h_sequence(h_many1(label), name_end, NULL), pointer, name_termination, NULL);

    HParser *QType = h_uint16();
    HParser *vQType = h_attr_bool(QType, validate_type, NULL);
    HParser *QClass = h_uint16();
    HParser *vQClass = h_attr_bool(QClass, validate_qclass, NULL);
    HParser *question = h_sequence(QName, vQType, vQClass, NULL);

    HParser *questions = h_repeat_n(question, ctx->qdcount);

    HParser *RNAME =
        h_choice(h_sequence(h_many1(label), name_end, NULL), pointer, name_termination, NULL);
    HParser *RType = h_uint16();
    HParser *vRType = h_attr_bool(RType, validate_type, NULL);
    HParser *RClass = h_uint16();
    HParser *vRClass = h_attr_bool(RClass, validate_rclass, NULL);
    HParser *ttl = h_uint32();
    HParser *RDLength = h_uint16();
    HParser *RData = h_length_value(RDLength, h_bits(8, false));
    HParser *response = h_sequence(RNAME, vRType, vRClass, ttl, RData, NULL);

    HParser *answers = h_repeat_n(response, ctx->ancount);
    HParser *authorities = h_repeat_n(response, ctx->nscount);
    HParser *additionals = h_repeat_n(response, ctx->arcount);

    return h_sequence(questions, answers, authorities, additionals, NULL);
}