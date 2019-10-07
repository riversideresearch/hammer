from __future__ import absolute_import, division, print_function

import unittest
import hammer as h

class TestTokenParser(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.token(b"95\xa2")
    def test_success(self):
        self.assertEqual(self.parser.parse(b"95\xa2"), b"95\xa2")
    def test_partial_fails(self):
        self.assertEqual(self.parser.parse(b"95"), None)

class TestChParser(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser_int = h.ch(0xa2)
        cls.parser_chr = h.ch(b"\xa2")
    def test_success(self):
        self.assertEqual(self.parser_int.parse(b"\xa2"), 0xa2)
        self.assertEqual(self.parser_chr.parse(b"\xa2"), b"\xa2")
    def test_failure(self):
        self.assertEqual(self.parser_int.parse(b"\xa3"), None)
        self.assertEqual(self.parser_chr.parse(b"\xa3"), None)

class TestChRange(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.ch_range(b"a", b"c")
    def test_success(self):
        self.assertEqual(self.parser.parse(b"b"), b"b")
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"d"), None)

class TestInt64(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.int64()
    def test_success(self):
        self.assertEqual(self.parser.parse(b"\xff\xff\xff\xfe\x00\x00\x00\x00"), -0x200000000)
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"\xff\xff\xff\xfe\x00\x00\x00"), None)

class TestInt32(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.int32()
    def test_success(self):
        self.assertEqual(self.parser.parse(b"\xff\xfe\x00\x00"), -0x20000)
        self.assertEqual(self.parser.parse(b"\x00\x02\x00\x00"), 0x20000)
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"\xff\xfe\x00"), None)
        self.assertEqual(self.parser.parse(b"\x00\x02\x00"), None)

class TestInt16(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.int16()
    def test_success(self):
        self.assertEqual(self.parser.parse(b"\xfe\x00"), -0x200)
        self.assertEqual(self.parser.parse(b"\x02\x00"), 0x200)
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"\xfe"), None)
        self.assertEqual(self.parser.parse(b"\x02"), None)

class TestInt8(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.int8()
    def test_success(self):
        self.assertEqual(self.parser.parse(b"\x88"), -0x78)
    def test_failure(self):
        self.assertEqual(self.parser.parse(b""), None)

class TestUint64(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.uint64()
    def test_success(self):
        self.assertEqual(self.parser.parse(b"\x00\x00\x00\x02\x00\x00\x00\x00"), 0x200000000)
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"\x00\x00\x00\x02\x00\x00\x00"), None)

class TestUint32(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.uint32()
    def test_success(self):
        self.assertEqual(self.parser.parse(b"\x00\x02\x00\x00"), 0x20000)
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"\x00\x02\x00"), None)

class TestUint16(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.uint16()
    def test_success(self):
        self.assertEqual(self.parser.parse(b"\x02\x00"), 0x200)
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"\x02"), None)

class TestUint8(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.uint8()
    def test_success(self):
        self.assertEqual(self.parser.parse(b"\x78"), 0x78)
    def test_failure(self):
        self.assertEqual(self.parser.parse(b""), None)
        
class TestIntRange(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.int_range(h.uint8(), 3, 10)
    def test_success(self):
        self.assertEqual(self.parser.parse(b"\x05"), 5)
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"\x0b"), None)

class TestWhitespace(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.whitespace(h.ch(b"a"))
    def test_success(self):
        self.assertEqual(self.parser.parse(b"a"), b"a")
        self.assertEqual(self.parser.parse(b" a"), b"a")
        self.assertEqual(self.parser.parse(b"  a"), b"a")
        self.assertEqual(self.parser.parse(b"\ta"), b"a")
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"_a"), None)

class TestWhitespaceEnd(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.whitespace(h.end_p())
    def test_success(self):
        self.assertEqual(self.parser.parse(b""), None) # empty string
        self.assertEqual(self.parser.parse(b"  "), None) # empty string
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"  x"), None)

class TestLeft(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.left(h.ch(b"a"), h.ch(b" "))
    def test_success(self):
        self.assertEqual(self.parser.parse(b"a "), b"a")
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"a"), None)
        self.assertEqual(self.parser.parse(b" "), None)
        self.assertEqual(self.parser.parse(b"ab"), None)

class TestRight(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.right(h.ch(b" "), h.ch(b"a"))
    def test_success(self):
        self.assertEqual(self.parser.parse(b" a"), b"a")
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"a"), None)
        self.assertEqual(self.parser.parse(b" "), None)
        self.assertEqual(self.parser.parse(b"ba"), None)

class TestMiddle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.middle(h.ch(b" "), h.ch(b"a"), h.ch(b" "))
    def test_success(self):
        self.assertEqual(self.parser.parse(b" a "), b"a")
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"a"), None)
        self.assertEqual(self.parser.parse(b" "), None)
        self.assertEqual(self.parser.parse(b" a"), None)
        self.assertEqual(self.parser.parse(b"a "), None)
        self.assertEqual(self.parser.parse(b" b "), None)
        self.assertEqual(self.parser.parse(b"ba "), None)
        self.assertEqual(self.parser.parse(b" ab"), None)

class TestAction(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.action(h.sequence(h.choice(h.ch(b"a"), h.ch(b"A")),
                                         h.choice(h.ch(b"b"), h.ch(b"B"))),
                                lambda x: [y.upper() for y in x])
    def test_success(self):
        self.assertEqual(self.parser.parse(b"ab"), [b"A", b"B"])
        self.assertEqual(self.parser.parse(b"AB"), [b"A", b"B"])
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"XX"), None)

class TestIn(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.in_(b"abc")
    def test_success(self):
        self.assertEqual(self.parser.parse(b"b"), b"b") 
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"d"), None)

class TestNotIn(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.not_in(b"abc")
    def test_success(self):
        self.assertEqual(self.parser.parse(b"d"), b"d")
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"a"), None)

class TestEndP(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.sequence(h.ch(b"a"), h.end_p())
    def test_success(self):
        self.assertEqual(self.parser.parse(b"a"), (b"a",))
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"aa"), None)

class TestNothingP(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.nothing_p()
    def test_success(self):
        pass
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"a"), None)

class TestSequence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.sequence(h.ch(b"a"), h.ch(b"b"))
    def test_success(self):
        self.assertEqual(self.parser.parse(b"ab"), (b"a", b"b"))
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"a"), None)
        self.assertEqual(self.parser.parse(b"b"), None)

class TestSequenceWhitespace(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.sequence(h.ch(b"a"), h.whitespace(h.ch(b"b")))
    def test_success(self):
        self.assertEqual(self.parser.parse(b"ab"), (b"a", b"b"))
        self.assertEqual(self.parser.parse(b"a b"), (b"a", b"b"))
        self.assertEqual(self.parser.parse(b"a  b"), (b"a", b"b"))
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"a  c"), None)

class TestChoice(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.choice(h.ch(b"a"), h.ch(b"b"))
    def test_success(self):
        self.assertEqual(self.parser.parse(b"a"), b"a")
        self.assertEqual(self.parser.parse(b"b"), b"b")
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"c"), None)

class TestButNot(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.butnot(h.ch(b"a"), h.token(b"ab"))
    def test_success(self):
        self.assertEqual(self.parser.parse(b"a"), b"a")
        self.assertEqual(self.parser.parse(b"aa"), b"a")
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"ab"), None)

class TestButNotRange(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.butnot(h.ch_range(b"0", b"9"), h.ch(b"6"))
    def test_success(self):
        self.assertEqual(self.parser.parse(b"4"), b"4")
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"6"), None)

class TestDifference(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.difference(h.token(b"ab"), h.ch(b"a"))
    def test_success(self):
        self.assertEqual(self.parser.parse(b"ab"), b"ab")
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"a"), None)

class TestXor(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.xor(h.ch_range(b"0", b"6"), h.ch_range(b"5", b"9"))
    def test_success(self):
        self.assertEqual(self.parser.parse(b"0"), b"0")
        self.assertEqual(self.parser.parse(b"9"), b"9")
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"5"), None)
        self.assertEqual(self.parser.parse(b"a"), None)

class TestMany(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.many(h.choice(h.ch(b"a"), h.ch(b"b")))
    def test_success(self):
        self.assertEqual(self.parser.parse(b""), ())
        self.assertEqual(self.parser.parse(b"a"), (b"a",))
        self.assertEqual(self.parser.parse(b"b"), (b"b",))
        self.assertEqual(self.parser.parse(b"aabbaba"), (b"a", b"a", b"b", b"b", b"a", b"b", b"a"))
    def test_failure(self):
        pass

class TestMany1(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.many1(h.choice(h.ch(b"a"), h.ch(b"b")))
    def test_success(self):
        self.assertEqual(self.parser.parse(b"a"), (b"a",))
        self.assertEqual(self.parser.parse(b"b"), (b"b",))
        self.assertEqual(self.parser.parse(b"aabbaba"), (b"a", b"a", b"b", b"b", b"a", b"b", b"a"))
    def test_failure(self):
        self.assertEqual(self.parser.parse(b""), None)
        self.assertEqual(self.parser.parse(b"daabbabadef"), None)

class TestRepeatN(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.repeat_n(h.choice(h.ch(b"a"), h.ch(b"b")), 2)
    def test_success(self):
        self.assertEqual(self.parser.parse(b"abdef"), (b"a", b"b"))
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"adef"), None)
        self.assertEqual(self.parser.parse(b"dabdef"), None)

class TestOptional(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.sequence(h.ch(b"a"), h.optional(h.choice(h.ch(b"b"), h.ch(b"c"))), h.ch(b"d"))
    def test_success(self):
        self.assertEqual(self.parser.parse(b"abd"), (b"a", b"b", b"d"))
        self.assertEqual(self.parser.parse(b"acd"), (b"a", b"c", b"d"))
        self.assertEqual(self.parser.parse(b"ad"), (b"a", h.Placeholder(), b"d"))
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"aed"), None)
        self.assertEqual(self.parser.parse(b"ab"), None)
        self.assertEqual(self.parser.parse(b"ac"), None)

class TestIgnore(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.sequence(h.ch(b"a"), h.ignore(h.ch(b"b")), h.ch(b"c"))
    def test_success(self):
        self.assertEqual(self.parser.parse(b"abc"), (b"a",b"c"))
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"ac"), None)

class TestSepBy(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.sepBy(h.choice(h.ch(b"1"), h.ch(b"2"), h.ch(b"3")), h.ch(b","))
    def test_success(self):
        self.assertEqual(self.parser.parse(b"1,2,3"), (b"1", b"2", b"3"))
        self.assertEqual(self.parser.parse(b"1,3,2"), (b"1", b"3", b"2"))
        self.assertEqual(self.parser.parse(b"1,3"), (b"1", b"3"))
        self.assertEqual(self.parser.parse(b"3"), (b"3",))
        self.assertEqual(self.parser.parse(b""), ())
    def test_failure(self):
        pass

class TestSepBy1(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.sepBy1(h.choice(h.ch(b"1"), h.ch(b"2"), h.ch(b"3")), h.ch(b","))
    def test_success(self):
        self.assertEqual(self.parser.parse(b"1,2,3"), (b"1", b"2", b"3"))
        self.assertEqual(self.parser.parse(b"1,3,2"), (b"1", b"3", b"2"))
        self.assertEqual(self.parser.parse(b"1,3"), (b"1", b"3"))
        self.assertEqual(self.parser.parse(b"3"), (b"3",))
    def test_failure(self):
        self.assertEqual(self.parser.parse(b""), None)

class TestEpsilonP1(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.sequence(h.ch(b"a"), h.epsilon_p(), h.ch(b"b"))
    def test_success(self):
        self.assertEqual(self.parser.parse(b"ab"), (b"a", b"b"))
    def test_failure(self):
        pass

class TestEpsilonP2(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.sequence(h.epsilon_p(), h.ch(b"a"))
    def test_success(self):
        self.assertEqual(self.parser.parse(b"a"), (b"a",))
    def test_failure(self):
        pass

class TestEpsilonP3(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.sequence(h.ch(b"a"), h.epsilon_p())
    def test_success(self):
        self.assertEqual(self.parser.parse(b"a"), (b"a",))
    def test_failure(self):
        pass

class TestAttrBool(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.attr_bool(h.many1(h.choice(h.ch(b"a"), h.ch(b"b"))),
                                 lambda x: x[0] == x[1])
    def test_success(self):
        self.assertEqual(self.parser.parse(b"aa"), (b"a", b"a"))
        self.assertEqual(self.parser.parse(b"bb"), (b"b", b"b"))
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"ab"), None)

class TestAnd1(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.sequence(h.and_(h.ch(b"0")), h.ch(b"0"))
    def test_success(self):
        self.assertEqual(self.parser.parse(b"0"), (b"0",))
    def test_failure(self):
        pass

class TestAnd2(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.sequence(h.and_(h.ch(b"0")), h.ch(b"1"))
    def test_success(self):
        pass
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"0"), None)

class TestAnd3(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.sequence(h.ch(b"1"), h.and_(h.ch(b"2")))
    def test_success(self):
        self.assertEqual(self.parser.parse(b"12"), (b"1",))
    def test_failure(self):
        pass

class TestNot1(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.sequence(h.ch(b"a"),
                                h.choice(h.ch(b"+"), h.token(b"++")),
                                h.ch(b"b"))
    def test_success(self):
        self.assertEqual(self.parser.parse(b"a+b"), (b"a", b"+", b"b"))
    def test_failure(self):
        self.assertEqual(self.parser.parse(b"a++b"), None)

class TestNot2(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parser = h.sequence(h.ch(b"a"), h.choice(h.sequence(h.ch(b"+"), h.not_(h.ch(b"+"))),
                                                    h.token(b"++")),
                                h.ch(b"b"))
    def test_success(self):
        self.assertEqual(self.parser.parse(b"a+b"), (b"a", (b"+",), b"b"))
        self.assertEqual(self.parser.parse(b"a++b"), (b"a", b"++", b"b"))
    def test_failure(self):
        pass

# ### this is commented out for packrat in C ...
# #class TestLeftrec(unittest.TestCase):
# #    @classmethod
# #    def setUpClass(cls):
# #        cls.parser = h.indirect()
# #        a = h.ch(b"a")
# #        h.bind_indirect(cls.parser, h.choice(h.sequence(cls.parser, a), a))
# #    def test_success(self):
# #        self.assertEqual(self.parser.parse(b"a"), b"a")
# #        self.assertEqual(self.parser.parse(b"aa"), [b"a", b"a"])
# #        self.assertEqual(self.parser.parse(b"aaa"), [b"a", b"a", b"a"])
# #    def test_failure(self):
# #        pass


class TestRightrec(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        #raise unittest.SkipTest(b"Bind doesn't work right now")
        cls.parser = h.indirect()
        a = h.ch(b"a")
        cls.parser.bind(h.choice(h.sequence(a, cls.parser),
                                 h.epsilon_p()))
    def test_success(self):
        self.assertEqual(self.parser.parse(b"a"), (b"a",))
        self.assertEqual(self.parser.parse(b"aa"), (b"a", (b"a",)))
        self.assertEqual(self.parser.parse(b"aaa"), (b"a", (b"a", (b"a",))))
    def test_failure(self):
        pass

# ### this is just for GLR
# #class TestAmbiguous(unittest.TestCase):
# #    @classmethod
# #    def setUpClass(cls):
# #        cls.parser = h.indirect()
# #        d = h.ch(b"d")
# #        p = h.ch(b"+")
# #        h.bind_indirect(cls.parser, h.choice(h.sequence(cls.parser, p, cls.parser), d))
# #        # this is supposed to be flattened
# #    def test_success(self):
# #        self.assertEqual(self.parser.parse(b"d"), [b"d"])
# #        self.assertEqual(self.parser.parse(b"d+d"), [b"d", b"+", b"d"])
# #        self.assertEqual(self.parser.parse(b"d+d+d"), [b"d", b"+", b"d", b"+", b"d"])
# #    def test_failure(self):
# #        self.assertEqual(self.parser.parse(b"d+"), None)
