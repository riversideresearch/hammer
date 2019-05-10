#!/usr/bin/env python2

# Example parser: Base64, with fine-grained semantic actions
#
# Demonstrates how to attach semantic actions to grammar rules and piece by
# piece transform the parse tree into the desired semantic representation,
# in this case a sequence of 8-bit values.
#
# Those rules using h.action get an attached action, which must be declared
# (as a function).
#
# This variant of the example uses fine-grained semantic actions that
# transform the parse tree in small steps in a bottom-up fashion. Compare
# base64_sem2.py for an alternative approach using a single top-level action.

from __future__ import absolute_import, division, print_function

import functools
import sys

import hammer as h


# Semantic actions for the grammar below, each corresponds to an "ARULE".
# They must be named act_<rulename>.

def act_bsfdig(p, user_data=None):
    # FIXME See the note in init_parser()
    c = p if isinstance(p, h.INTEGER_TYPES) else ord(p)

    if 0x41 <= c <= 0x5A: # A-Z
        return c - 0x41
    elif 0x61 <= c <= 0x7A: # a-z
        return c - 0x61 + 26
    elif 0x30 <= c <= 0x39: # 0-9
        return c - 0x30 + 52
    elif c == b'+':
        return 62
    elif c == b'/':
        return 63
    else:
        raise ValueError

# Hammer's Python bindings don't currently expose h_act_index or hact_ignore

def act_index0(p, user_data=None):
    return p[0]

def act_ignore(p, user_data=None):
    return None

act_bsfdig_4bit = act_bsfdig
act_bsfdig_2bit = act_bsfdig

act_equals      = act_ignore
act_ws          = act_ignore

act_document    = act_index0


def act_base64_n(n, p, user_data=None):
    """General-form action to turn a block of base64 digits into bytes.
    """
    res = [0]*n

    x = 0
    bits = 0
    for i in range(0, n+1):
        x <<= 6
        x |= p[i] or 0
        bits += 6

    x >>= bits % 8 # align, i.e. cut off extra bits

    for i in range(n):
        item = x & 0xFF

        res[n-1-i] = item   # output the last byte and
        x >>= 8             # discard it

    return tuple(res)


act_base64_3 = functools.partial(act_base64_n, 3)
act_base64_2 = functools.partial(act_base64_n, 2)
act_base64_1 = functools.partial(act_base64_n, 1)


def act_base64(p, user_data=None):
    assert isinstance(p, tuple)
    assert len(p) == 2
    assert isinstance(p[0], tuple)

    res = []
    
    # concatenate base64_3 blocks
    for elem in p[0]:
        res.extend(elem)

    # append one trailing base64_2 or _1 block
    tok = p[1]
    if isinstance(tok, tuple):
        res.extend(tok)

    return tuple(res)


def init_parser():
    """Return a parser with the grammar to be recognized.
    """
    # CORE

    # This is a direct translation of the  C example. In C the literal 0x30
    # is interchangable with the char literal '0' (note the single quotes).
    # This is not the case in Python.
    
    # TODO In the interests of being more Pythonic settle on either string
    #      literals, or integers
    digit   = h.ch_range(0x30, 0x39)
    alpha   = h.choice(h.ch_range(0x41, 0x5a), h.ch_range(0x61, 0x7a))
    space   = h.in_(b" \t\n\r\f\v")

    # AUX.
    plus    = h.ch(b'+')
    slash   = h.ch(b'/')
    equals  = h.action(h.ch(b'='), act_equals)

    bsfdig      = h.action(h.choice(alpha, digit, plus, slash), act_bsfdig)
    bsfdig_4bit = h.action(h.in_(b"AEIMQUYcgkosw048"), act_bsfdig_4bit)
    bsfdig_2bit = h.action(h.in_(b"AQgw"), act_bsfdig_2bit)
    base64_3    = h.action(h.repeat_n(bsfdig, 4), act_base64_3)
    base64_2    = h.action(h.sequence(bsfdig, bsfdig, bsfdig_4bit, equals),
                           act_base64_2)
    base64_1    = h.action(h.sequence(bsfdig, bsfdig_2bit, equals, equals),
                           act_base64_1)
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
