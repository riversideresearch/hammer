#include <hammer/glue.h>
#include <hammer/hammer.h>
#include <stdio.h>
#include <string.h>

/**
 * Converts a hex string (sequence of characters) into a sequence of decimal values.
 * @param p Parse result containing the hex string.
 * @param user_data Unused user data.
 * @return Parsed sequence of decimal values.
 */
HParsedToken *act_hex_to_dec(const HParseResult *p, void *user_data) {
    const HParsedToken *parseToken = p->ast;
    size_t numElements = parseToken->seq->used;

    HParsedToken *newSeq = H_MAKE_SEQ();

    for (size_t i = 0; i < numElements; i++) {
        const HParsedToken *elem = h_seq_index(parseToken, i);

        // First hex digit (high nibble)
        const HParsedToken *innerElem = h_seq_index(elem, 0);
        uint8_t hi =
            (innerElem->uint > '9') ? (innerElem->uint | 0x20) - 'a' + 10 : innerElem->uint - '0';

        // Second hex digit (low nibble)
        innerElem = h_seq_index(elem, 1);
        uint8_t lo =
            (innerElem->uint > '9') ? (innerElem->uint | 0x20) - 'a' + 10 : innerElem->uint - '0';

        // Combine into one byte
        uint8_t value = (hi << 4) | lo;

        // Add to sequence
        HParsedToken *t = H_MAKE_UINT(value);
        h_seq_snoc(newSeq, t);
    }

    return newSeq;
}

/**
 * Builds the top-down parser for hex strings.
 * @return HParser configured for hex string parsing.
 */
HParser *topDownParse() {
    // Rule: exactly 2 hex chars form one byte
    H_RULE(hex_octet, h_repeat_n(h_choice(h_ch_range('0', '9'), h_ch_range('a', 'f'),
                                          h_ch_range('A', 'F'), NULL),
                                 2));

    // Rule: many octets, then end of input
    H_RULE(hex_string, h_left(h_many(hex_octet), h_end_p()));

    // Attach action to convert hex to decimal
    H_ARULE(hex_to_dec, hex_string);

    return hex_to_dec;
}
