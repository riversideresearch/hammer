#include "tftp.h"

#include <hammer/glue.h>
#include <hammer/hammer.h>
#include <string.h>

// uint - 16bits (2 byte op code), (2 byte block #). Data can be 512bytes * 8 max or less
// function used for bound checking
bool validate_dataBytes(HParseResult *result, void *user_data) {
    return result && result->ast && ((result->ast->uint) - 32) <= (512 * 8);
}

/*
Parser implementation based off of documentation found via this link:
            https://www.rfc-editor.org/rfc/rfc1350

A read request (rrq) or write request (wrq) is valid if there is an opcode
of 1 for rrq, or 2 for wrq. There is also a file name field which is valid
if it contains a sequence of valid netascii characters. There is a mode
associated with a rrq/wrq which is valid if the mode string matches any
of the allowed mode string followed by a zero byte.
*/
HParser *rrqwrqParser() {

    // op code: values 1 for read request, 2 for write request
    H_RULE(opc, h_int_range(h_uint16(), 1, 2));

    // a zero byte x00
    H_RULE(zbyt, h_int_range(h_uint8(), 0, 0));

    // file name is a sequence of bytes in netascii terminated by a zero byte
    // valid file name chars are defined below:
    H_RULE(lcAlph, h_ch_range('a', 'z'));  // lower case letters
    H_RULE(ucAlph, h_ch_range('A', 'Z'));  // upper case letters
    H_RULE(numbers, h_ch_range('0', '9')); // numbers
    // special chars that might be in a file name
    H_RULE(spclChars, h_choice(h_ch('.'), h_ch('_'), h_ch('/'), h_ch('-'), h_ch(' '), NULL));

    // combining all the parsers for parts of the filename + a terminal zero byte
    H_RULE(filename,
           h_sequence(h_many1(h_choice(lcAlph, ucAlph, numbers, spclChars, NULL)), zbyt, NULL));

    // mode can be any of the below strings
    H_RULE(netascii, h_token((const uint8_t *)"netascii", 8));
    H_RULE(octet, h_token((const uint8_t *)"octet", 5));
    H_RULE(mail, h_token((const uint8_t *)"mail", 4));
    H_RULE(mode, h_sequence(h_choice(netascii, octet, mail, NULL), zbyt, NULL));

    // same shape for read&write requests
    H_RULE(rrqwrq, h_sequence(opc, filename, mode, NULL));

    return rrqwrq;
}

/*
an data packet is valid if the op code is 3 and the block # is
within the range of 0 to the max value of two bytes. The data block
is a sequence of bytes that terminates at some unspecified point
*/
HParser *dataParser() {

    H_RULE(opc, h_int_range(h_uint16(), 3, 3));               // when the op code is a 3
    H_RULE(blockNum, h_int_range(h_uint16(), 1, UINT16_MAX)); // can be from 1 to 65535

    // data bytes are a sequence of uint8 values terminated at some point.
    // parser will fail if the packet sequence is larger than 516 bytes: 512 data bytes + 2 byte op
    // + 2 byte block num
    H_RULE(dataBytes, h_sequence(h_many(h_uint8()), h_attr_bool(h_tell(), validate_dataBytes, NULL),
                                 h_end_p(), NULL));

    H_RULE(dataPacket, h_sequence(opc, blockNum, dataBytes, NULL));

    return dataPacket;
}

/*
an acknowlagement packet is valid if the op code is 4 and the block # is
within the range of 0 to the max value of two bytes
*/
HParser *ackParser() {

    H_RULE(opc, h_int_range(h_uint16(), 4, 4));               // when the op code is a 4
    H_RULE(blockNum, h_int_range(h_uint16(), 0, UINT16_MAX)); // can be from 0 to 65535

    H_RULE(ackPacket, h_sequence(opc, blockNum, h_end_p(), NULL));

    return ackPacket;
}

/*
an error packet is valid if the op code is 5 and the error code is a number
between 0 and 7. The error message can be written in netascii characters
and contains a trailing zero byte
*/
HParser *errpktParser() {

    H_RULE(opc, h_int_range(h_uint16(), 5, 5));     // when the op code is a 5
    H_RULE(errCode, h_int_range(h_uint16(), 0, 7)); // can be from 0 to 7

    // a zero byte x00
    H_RULE(zbyt, h_int_range(h_uint8(), 0, 0));

    // The error message can be printed in netascii, trailing zero byte
    H_RULE(validChars, h_ch_range(' ', '~')); // text
    // formatting characters for text could pass these as decimal values: x0D == 13
    H_RULE(frmtChars, h_choice(h_ch_range('\x01', '\x04'), h_ch_range('\x09', '\x0D'), NULL));
    H_RULE(errMsg, h_sequence(h_many1(h_choice(validChars, frmtChars, NULL)), zbyt, NULL));

    H_RULE(errPacket, h_sequence(opc, errCode, errMsg, NULL));

    return errPacket;
}
