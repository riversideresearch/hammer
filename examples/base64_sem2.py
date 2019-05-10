#!/usr/bin/env python2

# Example parser: Base64, with fine-grained semantic actions
#
# Demonstrates how to attach semantic actions to a grammar and transform the
# parse tree into the desired semantic representation, in this case a sequence
# of 8-bit values.
#
# Those rules using h.action get an attached action, which must be declared
# (as a function).
#
# This variant of the example uses coarse-grained semantic actions,
# transforming the entire parse tree in one big step. Compare base64_sem1.py
# for an alternative approach using a fine-grained piece-by-piece
# transformation.

from __future__ import absolute_import, division, print_function

import functools
import sys

import hammer as h


# Semantic actions for the grammar below, each corresponds to an "ARULE".
# They must be named act_<rulename>.

def bsfdig_value(p):
    """Return the numeric value of a parsed base64 digit.
    """
    c = p if isinstance(p, h.INTEGER_TYPES) else ord(p)
    if c:
        if 0x41 <= c <= 0x5A: # A-Z
            return  c - 0x41
        elif 0x61 <= c <= 0x7A: # a-z
            return  c - 0x61 + 26
        elif 0x30 <= c <= 0x39: # 0-9
            return  c - 0x30 + 52
        elif c == b'+':
            return  62
        elif c == b'/':
            return  63
    return 0

def act_base64(p, user_data=None):
    assert isinstance(p, tuple)
    assert len(p) == 2
    assert isinstance(p[0], tuple)

    # grab b64_3 block sequence
    # grab and analyze b64 end block (_2 or _1)
    b64_3 = p[0]
    b64_2 = p[1]
    b64_1 = p[1]

    if not isinstance(b64_2, tuple):
        b64_1 = b64_2 = None
    elif b64_2[2] == '=':
        b64_2 = None
    else:
        b64_1 = None

    # allocate result sequence
    res = []

    # concatenate base64_3 blocks
    for digits in b64_3:
        assert isinstance(digits, tuple)

        x = bsfdig_value(digits[0])
        x <<= 6; x |= bsfdig_value(digits[1])
        x <<= 6; x |= bsfdig_value(digits[2])
        x <<= 6; x |= bsfdig_value(digits[3])
        res.append((x >> 16) & 0xFF)
        res.append((x >> 8) & 0xFF)
        res.append(x & 0xFF)

    # append one trailing base64_2 or _1 block
    if b64_2:
        digits = b64_2
        x = bsfdig_value(digits[0])
        x <<= 6; x |= bsfdig_value(digits[1])
        x <<= 6; x |= bsfdig_value(digits[2])
        res.append((x >> 10) & 0xFF)
        res.append((x >> 2) & 0xFF)
    elif b64_1:
        digits = b64_1
        x = bsfdig_value(digits[0])
        x <<= 6; x |= bsfdig_value(digits[1])
        res.append((x >> 4) & 0xFF)

    return tuple(res)

# Hammer's Python bindings don't currently expose h_act_index or hact_ignore

def act_index0(p, user_data=None):
    return p[0]

def act_ignore(p, user_data=None):
    return None

act_ws          = act_ignore
act_document    = act_index0


def init_parser():
    """Set up the parser with the grammar to be recognized.
    """
    # CORE
    digit   = h.ch_range(0x30, 0x39)
    alpha   = h.choice(h.ch_range(0x41, 0x5a), h.ch_range(0x61, 0x7a))
    space   = h.in_(b" \t\n\r\f\v")

    # AUX.
    plus    = h.ch(b'+')
    slash   = h.ch(b'/')
    equals  = h.ch(b'=')

    bsfdig      = h.choice(alpha, digit, plus, slash)
    bsfdig_4bit = h.in_(b"AEIMQUYcgkosw048")
    bsfdig_2bit = h.in_(b"AQgw")
    base64_3    = h.repeat_n(bsfdig, 4)
    base64_2    = h.sequence(bsfdig, bsfdig, bsfdig_4bit, equals)
    base64_1    = h.sequence(bsfdig, bsfdig_2bit, equals, equals)
    base64      = h.action(h.sequence(h.many(base64_3),
                                      h.optional(h.choice(base64_2,
                                                          base64_1))),
                           act_base64)

    # TODO This is not quite the same as the C example, with uses act_ignore.
    #      But I can't get hammer to filter any value returned by act_ignore.
    ws          = h.ignore(h.many(space))
    document    = h.action(h.sequence(ws, base64, ws, h.end_p()),
                           act_document)

    # BUG sometimes inputs that should just don't parse.
    # It *seemed* to happen mostly with things like "bbbbaaaaBA==".
    # Using less actions seemed to make it less likely.

    return document


def main():
    parser = init_parser()

    s = sys.stdin.read()
    inputsize = len(s)
    print('inputsize=%i' % inputsize, file=sys.stderr)
    print('input=%s' % s, file=sys.stderr, end='')

    result = parser.parse(s)

    if result:
        #print('parsed=%i bytes', result.bit_length/8, file=sys.stderr)
        print(result)


if __name__ == '__main__':
    main()
