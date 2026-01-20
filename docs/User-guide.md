# Introduction

## What's a parser combinator?

Hammer is a parser-combinator library; what does that even mean?  The obvious
answer is that it's a library that allows one to combine parsers... in this
case, that's precisely what it is, but why is this useful?  A few reasons, for
starters: many people find it easier to think in terms of parser building
blocks like those provided by Hammer, thus producing parser code that is more
reliable and more understandable, often in less time; also, because parser
composition is a central aspect, reusing parser code between projects is dead
easy.

In the Hammer world, parsers can be divided up into two classes: _primitive
parsers_ that match a specific, literal token and _combinators_ that take one
or more other parsers and combine them in interesting ways.  For instance, one
could create a parser `alpha` that matches a single lowercase alphabetic
character:

    HParser *alpha = h_ch_range('a', 'z');

And then use a combinator to create a parser for a single word, defined as a
consecutive sequence of at least one of the previously-defined `alpha`:

    HParser *word = h_many1(alpha);

Now comes the really great bit: note that both of these are of type `HParser`,
the powerful implication being that our `word` parser can _also_ be fed to a
combinator to create an even bigger, badder parser; for example, we might
define a sentence as a sequence of words, separated by spaces, ending with a
full stop:

    HParser *sentence = h_sequence(h_sepBy1(word, h_ch(' ')), h_ch('.'));

We'll get into the details of `h_sequence` and `h_sepBy1` in a later section;
the important thing note here is that the prose description of how we want the
parser to work and the code that implements that parser are shockingly
similar.  Thus, creating parsers with Hammer becomes a task of finding (or
creating) the literal parsers you want and then finding (or creating)
combinators that hook them together in a way that achieves the overarching
goal.

In the following sections, we'll cover the selection of literal parsers
already present in Hammer as well as the various combinators that exist for
composing parsers.  We'll also cover more advanced topics such as making
parsers return values other than the raw token bytes that were parsed.

## Tool prerequisites

You'll need `git` to download the Hammer source code, `scons` (and, by
extension, Python) to configure it, and a working C toolchain to compile it.
You'll also need `glib` to integrate with the included unit-testing framework.

Currently, Hammer is known to compile and run on OS X and 64-bit Linux.

## How to build and install

This part is easy:

    git clone https://github.com/UpstandingHackers/hammer
    cd hammer
    scons
    sudo scons install

Hammer will be installed into `/usr/local/` by default.  If you want to change
that, use the `prefix` parameter to `scons install`, like so:

    scons install prefix=/path/to/install/hammer

# Hammer

## Hello World

Because it wouldn't be a user guide without one.

     1  #include <hammer/hammer.h>
     2  #include <stdio.h>
     3
     4  int main(int argc, char *argv[]) {
     5      uint8_t input[1024];
     6      size_t inputsize;
     7
     8      HParser *hello_parser = h_token("Hello World", 11);
     9
    10      inputsize = fread(input, 1, sizeof(input), stdin);
    11
    12      HParseResult *result = h_parse(hello_parser, input, inputsize);
    13      if(result) {
    14          printf("yay!\n");
    15      } else {
    16          printf("boo!\n");
    17      }
    18  }

The code is fairly simple, and only a few lines are worth commenting on.

Firstly, the hammer header file is `hammer/hammer.h` (line 1).

On line 8, we create a parser that recognizes the literal string "Hello
World" and we explicitly specify its length of 11.

The heavy lifting is done by the `h_parse` function, called on line 12, which
takes as its arguments the parser to apply, a buffer to apply it to, and the
length of the buffer in bytes.  `h_parse` returns an `HParseResult *`, which
is `NULL` if the parse did not succeed and non-`NULL` otherwise.  (We'll cover
more interesting inspection of parse results a bit later.)

Compiling is easy, just be sure to use the appropriate compiler flags, as
provided by `pkg-config`:

    gcc -o hello-world hello-world.c $(pkg-config --cflags --libs libhammer)

And test like so:

    $ echo -n "Hello World" | ./hello-world
    yay!
    $ echo -n "Hi World" | ./hello-world
    boo!

Note that we use the `-n` option to `echo`---this is because the token we're
matching doesn't include a newline.  What happens when we _don't_ use `-n`?

    $ echo "Hello World" | ./hello-world
    yay!

It still succeeds!  The fact that there are trailing characters in the input
buffer after the parser is done does not cause a parse to fail!  This may or
may not be the behavior you're aiming for.  If it is, great; otherwise, we're
going to have to dig into our bag o' parser tricks to tweak our solution.

What we really want is a parser that matches the literal "Hello World"
followed immediately by the end of the input.  Put another way, we want a
_sequence_ of tokens, first the literal "Hello World" then then end-of-input
token.  Fortunately, Hammer provides the `h_sequence` combinator to apply one
parser after another in order.  Hammer also provides the `h_end_p()` parser,
which consumes no input and parses correctly if and only if there is no input
left to parse.  So let's add this parser definition after line 8:

    9  HParser *full_parser = h_sequence(hello_parser, h_end_p(), NULL);

and change the call to `h_parse` (now on line 13) to use `full_parser` instead
of `hello_parser`.  Note that the second argument to `h_sequence` is a _call
to_ `h_end_p`.  This is because `h_end_p` is a _function_ that returns an
`HParser *`; it is _not_ a parser in and of itself!  (If you forget the
parentheses, the program will still compile, but you'll get a segfault when
you run it.)

Compile and test:

    $ echo -n "Hello World" | ./hello-world
    yay!
    $ echo "Hello World" | ./hello-world
    boo!

Sweet.  Exactly what we wanted.

So that's Hello World with Hammer: we started with a very simple example
initially parsing just a string literal, then we modified it using the
`h_sequence` combinator along with the `h_end_p` built-in parser.  Next we're
going to look at how to interpret the results of parses, beyond just the "did
it succeed?" test in our first example.

## Fun with parse results

In the previous examples, we only cared whether the parse was successful or
not; we didn't feel the need to examine or interpret or otherwise use the
results of the parse itself.  There are, however, situations where you might
want to do all manner of things to parse results.  For instance, if you're
parsing ASCII representations of integers, you would probably want your
integer parser to return an `int64_t` instead of a string of ASCII characters.
Let's start with just seeing what was parsed and then we'll move on to playing
with those results.

    HParseResult *result = h_parse(...);

The return value of `h_parse` is an `HParseResult *`.  If the parse failed,
this will be `NULL`; otherwise, we can peek inside to see precisely what was
parsed:

    HParsedToken *t = result->ast;

`HParsedToken` is a _discriminated union_: it contains a union (`token_data`)
and a field indicating which part of the union is relevant (`token_type`).
Check out the definitions of `HParsedToken` and `HTokenType` in `hammer.h` and
you'll see the various types of results a parse can return.

Here's a really dumb example that parses a single lowercase letter and prints
it back out:

     1  #include <hammer/hammer.h>
     2  #include <stdio.h>
     3
     4  int main(int argc, char *argv[]) {
     5      uint8_t input[1024];
     6      size_t inputsize;
     7
     8      inputsize = fread(input, 1, sizeof(input), stdin);
     9
    10      HParseResult *result = h_parse(h_ch_range('a', 'z'), input, inputsize);
    11      if(result) {
    12          printf("Token is type %d\n", result->ast->token_type);
    13          printf("You entered '%c'.\n", result->ast->uint);
    14      } else {
    15          printf("Parse failed.\n");
    16      }
    17  }

There are a couple important things to note here:

- First, on line 12, `result->ast->token_type` tells us the type of the parse
  result.  In this simple example, we don't do anything but print its value
  because we know that the `h_ch_range()` parser returns a value of type
  `TT_UINT`.  There are, however, parsers that return a result whose type is
  _not_ known when you're writing the code, in which case you'll need to
  examine `token_type` to know which field of the union to use.

- Secondly, on line 13, we get our hands on the actual token by grabbing the
  `uint` member of the union.  (Had we used a parser that returned something
  other than `TT_UINT`, we would have used another field.)  The authoritative
  list of token types, as well as the definition of the union, are in
  `hammer.h`.

Compile and test:

    $ gcc -o char char.c $(pkg-config --cflags --libs libhammer)
    $ echo -n 'a' | ./char
    Token is type 8
    You entered 'a'.
    $ echo -n 'A' | ./char
    Parse failed.

For completeness, check out the definition of `HTokenType` in `hammer.h` to
verify that, indeed, `TT_UINT` is 8.

### Parse actions

Let's get a little fancier now by modifying the character parser from above to
recognize an ASCII hexadecimal octet and print its value in base ten.  Here
are the parsers we'll use (see the documentation below for the behavior of
`h_choice`, `h_repeat_n`, and `h_left`):

    HParser *hex_digit = h_choice(h_ch_range('0', '9'), h_ch_range('a', 'f'), NULL);
    HParser *hex_octet = h_repeat_n(hex_digit, 2);

    HParseResult *result = h_parse(h_left(hex_octet, h_end_p()), input, inputsize);

The result of the `hex_digit` parser will be a `TT_UINT` whose value is the
ASCII code of the character parsed; the result of the `hex_octet` parser will
be a `TT_SEQUENCE` of results of `hex_digit` (i.e., a bunch of `TT_UINT`s),
and the result of the `h_left` parser will be the result of `hex_octet`.
Ideally, we'd like our `h_left` parser to result in a `TT_UINT` whose value is
the decimal representation of the pair of ASCII hexadecimal digits that were
parsed.  For that, we require parse actions!

**PRO TIP: If you're having difficulty successfully composing parsers, think
very carefully about the _result types_ of the parsers you're composing.**

Hammer allows you to attach a function to a parser, whose prototype looks like
this (typedef'd to `HAction` in `glue.h`):

    HParsedToken *action(const HParseResult *p, void *user_data);

When you attach an action to a parser, upon a successful parse, the parse
result will be fed to the action function and the `HParsedToken` it returns
will become the result of the parser.  Here's how we can attach an action to
the `hex_digit` parser above:

    HParser *hex_digit = h_action(h_choice(h_ch_range('0', '9'), h_ch_range('a', 'f'), NULL), hex_to_dec, NULL);

The result of the previous incarnation of `hex_digit` was the result of the
`h_choice` parser; now, however, the result of the `h_choice` parser is passed
through the `hex_to_dec` function before being returned by the `hex_digit`
parser.  The `hex_to_dec` function could look like this:

     1  HParsedToken *hex_to_dec(const HParseResult *p, void *user_data) {
     2      uint8_t value = (p->ast->uint > '9') ? p->ast->uint - 'a' + 10
     3                                           : p->ast->uint - 'f';
     4      HParsedToken *t = H_MAKE_UINT(value);
     5      return t;
     6  }                   

In lines 2 and 3, we convert the single hex digit to decimal and in line 4 we
stuff that decimal value into a `TT_UINT` using the `H_MAKE_UINT` macro.
(Other, similar macros are defined in `glue.h`.)  The action for `hex_octet` is
attached similarly:

    HParser *hex_octet = h_action(h_repeat_n(hex_digit, 2), pair_to_dec, NULL);

though its implementation is somewhat more involved because it needs to deal with the `TT_SEQUENCE` returned by the `h_repeat_n` parser:

     1  HParsedToken *pair_to_dec(const HParseResult *p, void *user_data) {
     2      const HParsedToken *seq, *elem;
     3      uint8_t hi, lo, value;
     4
     5      seq = p->ast;
     6
     7      elem = h_seq_index(seq, 0);
     8      hi = elem->uint;
     9
    10      elem = h_seq_index(seq, 1);
    11      lo = elem->uint;
    12
    13      value = (hi << 4) | lo;
    14
    15      HParsedToken *t = H_MAKE_UINT(value);
    16      return t;
    17  }   

On lines 7 and 10, we use the `h_seq_index` function to extract the high and
low nibbles from the sequence, then we combine them in line 13, and stuff them
into a `TT_UINT` on line 15.

And here's the `main` function that brings it all together:

     1  int main(int argc, char *argv[]) {
     2      uint8_t input[1024];
     3      size_t inputsize;
     4
     5      inputsize = fread(input, 1, sizeof(input), stdin);
     6
     7      HParser *hex_digit = h_action(h_choice(h_ch_range('0', '9'), h_ch_range('a', 'f'), NULL), hex_to_dec, NULL);
     8      HParser *hex_octet = h_action(h_repeat_n(hex_digit, 2), pair_to_dec, NULL);
     9
    10      HParseResult *result = h_parse(h_left(hex_octet, h_end_p()), input, inputsize);
    11      if(result) {
    12          printf("Token is type %d\n", result->ast->token_type);
    13          printf("Value is %d\n", result->ast->uint);
    14      } else {
    15          printf("Parse failed.\n");
    16      }
    17  }

Compile and test:

    $ gcc -g -o hex hex.c $(pkg-config --libs --cflags libhammer)
    $ echo -n 'af' | ./hex
    Token is type 8
    Value is 175
    $ echo -n 'ff' | ./hex
    Token is type 8
    Value is 255
    $ echo -n 'xf' | ./hex
    Parse failed.

Note how the call to `h_parse` returns a token of type 8 (`TT_UINT`) rather
than the `TT_SEQUENCE` that `h_repeat_n` returns by default.  Behold the power
of parser actions!

Actions are so prevalent in parsers that Hammer has some macros to make
defining parsers with actions particularly easy.  Check out `H_ARULE` in the
section "Shorthand notation for instantiating parsers" below for the full
scoop.

### Validating parse results

There will be times you want a parse to fail for some reason that can't be
encoded in the parse rule itself (for instance, you may want to compare it to
some externally-specified value).  To suit this purpose, similar to defining
an action as above, you can define a _validation function_ that takes a parse
result and a user-defined data value and returns either true or false:

    bool predicate(HparseResult *p, void *user_data);

This predicate is attached with `h_attr_bool` like so:

    HParser *p = ...
    HParser *verified_parser = h_attr_bool(p, predicate, data);

If the parser `p` succeeds, the result will be passed to `predicate` (along
with the data given at rule creation time).  If `predicate` returns true,
`verified_parser` will as well, with the original result of `p`; otherwise
`verified_parser` will indicate parse failure.

Just like actions, Hammer provides some macros to make attaching validation
functions easy; they're also detailed in the "Shorthand notation for
instantiating parsers" section below.

### User-defined parse result types

**NOTE: This area of Hammer is in flux.  Expect it to change.**

Instead of the primitive parse result types provided by Hammer, suppose you
want to define your own.  This is easy!  The `HTokenType` enum leaves tons of
room for user-defined types and the `HParsedToken` union contains a `void *`
for precisely this purpose.  Not only that, but Hammer includes a token-type
registry to make sure your types don't trample on others---even other
user-defined types!

In your parser-creation code, you'll use `h_allocate_token_type` to create a
new token; e.g.:

     1  static HTokenType my_token_type;
     2
     3  void init_parser(void) {
     4      ...
     5      my_token_type = h_allocate_token_type("my_new_token_x");
     6      ...
     7  }

#### `HTokenType h_allocate_token_type(const char* name)`

Registers (and returns) a new token type named `name`.

#### `HTokenType h_get_token_type_number(const char* name)`

Looks up a token named `name`, returning its type, or zero if not found.

#### `const char* h_get_token_type_name(HTokenType token_type)`

Returns a pointer to a string containing the name of the requested token type
as originally given to `h_allocate_token_type`, or `NULL` if not found.  As
indicated by the `const` in the return type, do _not_ free this string!

## Other important things

### Shorthand notation for instantiating parsers

Because you're presumably going to be creating a bunch of parsers, Hammer
provides some handy macros in `glue.h` to make doing so much easier.

#### `H_RULE(rule, def)`

Defines a parser `rule` with definition `def`.  This is precisely equivalent
to `HParser *rule = def` (if you don't trust me, go read `glue.h`).

#### `H_ARULE(rule, def)`

Defines a parser `rule` with definition `def` and action `act_rule`.  This is
precisely equivalent to:

    HParser *rule = h_action(def, act_ ## rule, NULL)

You must implement `act_rule` yourself.

#### `H_VRULE(rule, def)`

Defines a parser `rule` with definition `def` and validation rule
`validate_rule`.  This is precisely equivalent to:

    HParser *rule = h_attr_bool(def, validate_ ## rule, NULL)

You must implement `validate_rule` yourself.

#### `H_VARULE(rule, def)`

Defines a parser `rule` with definition `def`, validation function
`validate_rule`, and action `act_rule`.  The validation function is executed
_after_ the action.  This is precisely equivalent to:

    HParser *rule = h_attr_bool(h_action(def, act_ ## rule, NULL), validate_ ## rule, NULL)

You must implement `validate_rule` and `act_rule` yourself.

#### `H_AVRULE(rule, def)`

Defines a parser `rule` with definition `def`, validation function
`validate_rule`, and action `act_rule`.  The validation function is executed
_before_ the action.  This is precisely equivalent to:

    HParser *rule = h_action(h_attr_bool(def, validate_ ## rule, NULL), act_ ## rule, NULL)

You must implement `validate_rule` and `act_rule` yourself.

#### `H_ADRULE(rule, def, data)`

Defines a parser `rule` with definition `def` and action `act_rule`.  The
action is provided `data` when invoked.  This is precisely equivalent to:

    HParser *rule =  h_action(def, act_ ## rule, data)

You must implement `act_rule` yourself.

#### `H_VDRULE(rule, def, data)`

Defines a parser `rule` with definition `def` and validation function
`validate_rule`.  The validation function is provided `data` when invoked.
This is precisely equivalent to:

    HParser *rule =  h_attr_bool(def, validate_ ## rule, data)

You must implement `validate_rule` yourself.

#### `H_VADRULE(rule, def, data)`

Defines a parser `rule` with definition `def`, validation function
`validate_rule`, and action `act_rule`.  The validation function is executed
_after_ the action; both are provided `data` when invoked.  This is precisely
equivalent to:

    HParser *rule =  h_attr_bool(h_action(def, act_ ## rule, data), validate_ ## rule, data)

You must implement `validate_rule` and `act_rule` yourself.

#### `H_AVDRULE(rule, def, data)`

Defines a parser `rule` with definition `def`, validation function
`validate_rule`, and action `act_rule`.  The validation function is executed
_before_ the action; both are provided `data` when invoked.  This is precisely
equivalent to:

    HParser *rule =  h_action(h_attr_bool(def, validate_ ## rule, data), act_ ## rule, data)

You must implement `validate_rule` and `act_rule` yourself.

### Built-in semantic actions

Similar to the macros for instantiating rules described in the previous
section, Hammer also provides some baked-in semantic actions to make your life
easier.  Recall that actions modify the result of a parser.

#### `HParsedToken *h_act_first(const HParseResult *p, void* user_data)`

Assuming `p` is an `h_sequence`, returns the first item of `p`.  If `p` is not
an `h_sequence` or does not contain at least one item, fails by `assert`.

#### `HParsedToken *h_act_second(const HParseResult *p, void* user_data)`

Assuming `p` is an `h_sequence`, returns the second item of `p`.  If `p` is
not an `h_sequence` or does not contain at least two items, fails by `assert`.

#### `HParsedToken *h_act_last(const HParseResult *p, void* user_data)`

Assuming `p` is an `h_sequence`, returns the last item of `p`.  If `p` is not
an `h_sequence` or does not contain at least one item, fails by `assert`.

#### `HParsedToken *h_act_index(int i, const HParseResult *p, void* user_data)`

Assuming `p` is an `h_sequence`, returns the `i`th item of `p`.  If `p` is not
an `h_sequence` or does not contain at least one item, returns a NULL AST.

#### `HParsedToken *h_act_flatten(const HParseResult *p, void* user_data)`

Given a parse result `p`, which may or may not be an `h_sequence`, recursively
descends into any `h_sequence`s within and moves its elements up to a single
top-level `h_sequence`.  If `p` contains a single non-`h_sequence` result,
returns an `h_sequence` containing that single item.

That is, if the AST of `p` contains the following result (in which
comma-delimited lists denote sequences):

    a, b, c, s1, g, s3
             /\     |
            d, s2   h
               /\
              e, f

`h_act_flatten` will return the sequence `a, b, c, d, e, f, g, h`.

#### `HParsedToken *h_act_ignore(const HParseResult *p, void* user_data)`

This is the action equivalent of the parser combinator `h_ignore`. It simply
causes the AST it is applied to to be replaced with NULL. This most
importantly causes it to be elided from the result of a surrounding
`h_sequence`.

### Indirect binding

Some data formats are recursive, in which an `x` may itself contain other
`x`s.  Clearly, this code won't work:

    HParser *x = h_choice(x, y, z, NULL);

because `x` has yet to be defined when you're using it as a parameter to
`h_choice`!  To get around this, you need to make a sort of forward
declaration of `x` using `h_indirect`, create a parser containing a reference
to `x`, then bind that parser back to `x` using `h_bind_indirect`.

    HParser *x = h_indirect();
    HParser *foo = h_choice(x, y, z, NULL);
    h_bind_indirect(x, foo);

### Unit testing

Hammer defines a bunch of macros in `test_suite.h` to make it easy for you to
implement testing of your own parsers; you'll need `glib` installed to take
advantage of it.  There are a bunch of ways to go about testing; in this
section I'll describe one.

First, put the code that _defines_ your parser in a different file (or set of
files) from the code that _uses_ your parser.  That is, all the `HParser`
definitions would go in, e.g., `my_protocol_parser.c` and any code that calls
`h_parse` would go in, e.g., `my_program.c`.  Then you create a separate file,
e.g., `my_protocol_parser_tests.c` that implements the tests, and might look
something like this:

     1  #include <hammer/hammer.h>
     2  #include "my_protocol_parser.h"
     3
     4  HParser *parser_under_test;
     5
     6  int main(int argc, char *argv[])
     7  {
     8      g_test_init(&argc, &argv, NULL);
     9      parser_under_test = my_protocol_parser();
    10      g_test_add_func("test1", test1);
    11      g_test_add_func("test2", test2);
    12      g_test_add_func("test3", test3);
    13      g_test_run();
    14  }

Line 8 initializes the `glib` testing framework, line 9 instantiates your
parser, lines 10-12 register your tests (to be described shortly), and line 13
runs them for you.  `g_test_add_func` takes as parameters a string to describe
the test you're adding and a function that implements the test; this function
must take no parameters and have no return value.  (One implication of this is
that, as demonstrated on line 4, your parser should be defined globally.)

Individual tests could look something like this:

     1  static void test1(void) {
     2      HParseResult *result = h_parse(parser_under_test, "some string", 11);
     3      g_check_cmp_uint32(result->ast->token_type, ==, TT_BYTES);
     4      g_assert(0 == memcp(octets->ast->bytes.token, "result", 6));
     5  }

Line 2 performs the parse, line 3 checks to see that the result of the parse
is of the correct type, and line 4 checks that the result of the parse has the
correct value.  `g_assert` is part of the `glib` testing framework, whereas
`g_check_cmp_uint32` is one of many convenience functions provided in
`test_suite.h`

Compile and run:

    $ gcc -o test my_protocol_parser_tests.c my_protocol_parser.c $(pkg-config --libs --cflags libhammer) $(pkg-config --libs --cflags glib-2.0)
    $ ./test
    test1: OK
    test2: OK
    test3: OK

Should one of the tests fail, `g_test_run` will by default terminate the
entire run.  If instead you want it to continue to run the rest of the tests,
invoke your program with the `-k` parameter ("keep going").

In addition to `g_check_cmp_uint32`, a bunch more wrapper functions are
provided in `test_suite.h`.  Refer to that file in the Hammer distribution and
to the [`glib` reference
manual](https://developer.gnome.org/glib/stable/glib-Testing.html) for more.

# Primitive parsers

There are bunch of these.  Note they all return values of type `HParser *`.

### `HParser *h_token(const uint8_t *str, const size_t len)`

Matches the given string `str` of length `len`.

Returns a result of type `TT_BYTES`.

### `HParser *h_ch(const uint8_t c)`

Matches the given character `c`.

Returns a result of `TT_UINT`.

### `HParser *h_ch_range(const uint8_t lower, const uint8_t upper)`

Matches a single character in the range from `lower` to `upper`, inclusive.

This parser matches all uppercase letters in the Latin alphabet:

    HParser *upper_alpha = h_ch_range('A', 'Z')

Returns a result of type `TT_UINT`.

### `HParser *h_bits(size_t len, bool sign)`

Matches a sequence of bits of length `len`.  If `sign` is True, the resulting
token is a signed integer, of type `TT_SINT`; otherwise the resulting token is
an unsigned integer, of type `TT_UINT`.

### `HParser *h_int64(void)`

Matches a signed 8-byte integer.

Returns a result of type `TT_SINT`.

### `HParser *h_int32(void)`

Matches a signed 4-byte integer.

Returns a result of type `TT_SINT`.

### `HParser *h_int16(void)`

Matches a signed 2-byte integer.

Returns a result of type `TT_SINT`.

### `HParser *h_int8(void)`

Matches a signed 1-byte integer.

Returns a result of type `TT_SINT`.

### `HParser *h_uint64(void)`

Matches an unsigned 8-byte integer.

Returns a result of type `TT_UINT`.

### `HParser *h_uint32(void)`

Matches an unsigned 4-byte integer.

Returns a result of type `TT_UINT`.

### `HParser *h_uint16(void)`

Matches an unsigned 2-byte integer.

Returns a result of type `TT_UINT`.

### `HParser *h_uint8(void)`

Matches an unsigned 1-byte integer.

Returns a result of type `TT_UINT`.

### `HParser *h_in(const uint8_t *charset, size_t length)`

Matches a single octet if and only if that octet appears in the array
`charset`, which is of length `length` octets.

Returns a result of type `TT_UINT`.

### `HParser *h_not_in(const uint8_t *charset, size_t length)`

Matches a single octet if and only if that octet _does not_ appear in the
array `charset`, which is of length `length` octets.  (Equivalent to
`h_not(h_in(...))`.)

Returns a result of type `TT_UINT`.

### `HParser *h_end_p(void)`

Matches the end of the parser input.  Consumes no input.  This parser is
useful when you want to make sure that the input contains no trailing data.

Returns a result with no type (and no associated AST).

### `HParser *h_nothing_p(void)`

This parser always fails.  Useful for stubbing out parsers-in-progress.

# Combinators

### `HParser *h_whitespace(const HParser *p)`

Consumes and discards leading whitespace, then applies parser `p` and returns
its result.  White space is defined as space (' ', ASCII 0x20), form
feed('\\f', ASCII 0x0c), newline ('\\n', ASCII 0x0a), carriage return ('\\r',
ASCII 0x0d), tab ('\\t', ASCII 0x09), and vertical tab ('\\v', ASCII 0x0b).

This parser is equivalent in behavior to:

    HParser *my_whitespace = h_right(h_many(h_ch_in(" \f\n\r\t\v", 6)), p);

### `HParser *h_left(const HParser *p, const HParser *q)`

Applies the parser `p`.  If `p` succeeds, applies the parser `q`.  If `q`
succeeds, returns the result of `p` and discards the result of `q`.

This combinator is useful, e.g., when you want to match a line that ends in a
particular character but you don't need to do anything with the actual
character token.  For example, to match a string of letters ending in '$', you
could use:

    HParser *prefix = h_left(h_many(alpha), h_ch('$'));

The result of the parser would only be the string of letters; the '$' token
(which must still be successfully parsed for the parent `h_left` parser to
succeed) is discarded.

### `HParser *h_right(const HParser *p, const HParser *q)`

Applies the parser `p`.  If `p` succeeds, applies the parser `q`.  If `q`
succeeds, returns the result of `q` and discards the result of `p`.

This combinator is useful, e.g., when you want to match a line beginning with
a particular prefix.  For example, to match a sequence of letters beginning
with the string "==> ", you could use:

    HParser *suffix = h_right(h_token("==> ", 4), h_many1(alpha));

The result of the parser would only be the string of letters; the "==> " token
(which must still be successfully parsed for the parent `h_right` parser to
succeed) is discarded.

### `HParser *h_middle(const HParser *p, const HParser *x, const HParser *q)`

Applies the parser `p`.  If `p` succeeds, applies the parser `x`.  If `x`
succeeds, applies the parser `q`.  If `q` succeeds, returns the result of `x`
and discards the results of `p` and `q`.

This combinator is useful in cases such as parsing quoted strings, where you
need to parse the quotes that delimit the string, but you don't care about the
actual quote tokens.  For instance:

    HParser *quoted_string = h_middle(h_ch('"'), h_many1(alpha), h_ch('"'));

### `HParser *h_sequence(...)`

Takes as arguments a NULL-terminated list of parsers, which are applied in
order.  If the `n`th parser succeeds upon consuming the `m`th input byte, the
`n+1`st parser begins parsing at the `m+1`st input byte.  If any of the
constituent parsers fails, `h_sequence` fails.  Result is a `TT_SEQUENCE` in
which each item is the result of the corresponding parser.

For example, an alternative to `h_token("Hammer", 6)` would be:

    HParser *hammer_literal = h_sequence(h_ch('H'), h_ch('a'), h_repeat_n(h_ch('m'), 2), h_ch('e'), h_ch('r'), NULL);

Note that not all of the parsers given to `h_sequence` need be of the same
type!  I snuck a `h_repeat_n` in among the `h_ch` parsers (purely contrived,
of course, but it illustrates the point).

### `HParser *h_choice(...)`

Takes as arguments a NULL-terminated list of parsers, which are applied in
order.  If the `n`th parser fails, the `n+1`st parser begins parsing at the
_beginning_ of the input given to `h_choice`.  If the `n`th parser succeeds,
its result is returned as the successful result of `h_choice`.  Despite the
similarity in prototype to `h_sequence`, `h_choice` differs dramatically: each
of the constituent parsers in a call to `h_choice` has the chance to parse the
_exact same input_; the first parser to succeed "wins".

For example, let's say you're parsing a file containing log messages, one per
line.  Lines describing errors are prefixed with "!!! ", lines describing
warnings are prefixed with ">>> ", and all other lines have no prefix.  You
could parse this file as follows:

    HParser *error_line = h_middle(h_token("!!! ", 4), message, h_ch('\n'));
    HParser *warning_line = h_middle(h_token(">>> ", 4), message, h_ch('\n'));
    HParser *other_line = h_left(message, h_ch('\n'));

    HParser *log_msg = h_choice(error_line, warning_line, other_line, NULL);

The parser `log_msg` parses _mutually exclusive_ options: a log message can
only be one of error, warning, or other.

### `HParser *h_butnot(const HParser *p1, const HParser *p2)`

Given two parsers, `p1` and `p2`, this parser succeeds in exactly these two
cases:

- if `p1` succeeds and `p2` fails
- if both succeed but the result of `p1` is _at least as long as_ the result
  of `p2`

The result is the result of `p1`.

### `HParser *h_difference(const HParser *p1, const HParser *p2)`

Given two parsers, `p1` and `p2`, this parser succeeds in exactly these two
cases:

- if `p1` succeeds and `p2` fails
- if both succeed but the result of `p1` is _shorter than_ the result of `p2`

The result is the result of `p1`.

### `HParser *h_xor(const HParser *p1, const HParser *p2)`

Given two parsers, `p1` and `p2`, this parser succeeds if and only if exactly
one of `p1` and `p2` succeeds, returning the result of the successful parser.

### `HParser *h_many(const HParser *p)`

Given a parser `p`, this parser succeeds for _zero_ or more repetitions of
`p`, return a `TT_SEQUENCE` containing the result of each application of `p`.

### `HParser *h_many1(const HParser *p)`

Given a parser `p`, this parser succeeds for _one_ or more repetitions of
`p`, return a `TT_SEQUENCE` containing the result of each application of `p`.
These two parsers are very similar but _not_ equivalent:

    HParser *foo = h_many1(p);

    HParser *bar = h_sequence(p, h_many(p), NULL);

They do indeed succeed on the exact same set of input strings, but their
return values are slightly different.  `foo` will return a sequence of results
of `p`, whereas `bar` will return a sequence whose first element is a result
of `p` and whose second element is _another_ sequence of results of `p`.

### `HParser *h_repeat_n(const HParser *p, const size_t n)`

Given a parser `p`, this parser succeeds for exactly `n` repetitions of `p`,
returning a `TT_SEQUENCE` of results of `p`.

### `HParser *h_optional(const HParser *p)`

This parser applies `p` to the input.  If `p` succeeds, returns the result of
`p`; otherwise, returns `TT_NONE`.  This parser never fails.

### `HParser *h_ignore(const HParser *p)`

This parser applies `p` to the input.  If `p` succeeds, returns success (i.e.,
a non-NULL `HParseResult *`) but the AST contained therein _is_ `NULL`.  If
`p` fails, this parser fails.

### `HParser *h_sepBy(const HParser *p, const HParser *sep)`

This parser returns a `TT_SEQUENCE` of results of alternately applying parser
`p` and parser `sep`, terminated with a single application of `p`.  The
results of `sep` are discarded.

For example, to parse a comma-delimited list of natural numbers:

    HParser *nat = h_many1(h_ch_range('0', '9'));
    HParser *nums = h_sepBy(nat, h_ch(','));

### `HParser *h_sepBy1(const HParser *p, const HParser *sep)`

This parser is identical to `h_sepBy` with the exception that `p` must succeed
at least once.  That is, the resultant `TT_SEQUENCE` will have one or more
elements.

### `HParser *h_epsilon_p(void)`

This parser consumes no input and returns a zero-length match.  Its result is
non-NULL but of no type; the AST contained within, however, _is_ NULL.

### `HParser *h_length_value(const HParser *length, const HParser *value)`

First applies parser `length`, which must return a result of type `TT_UINT`.
Then applies parser `value` that many times, returning a `TT_SEQUENCE` of
results of `value`.

### `HParser *h_attr_bool(const HParser *p, HPredicate pred, void *user_data)`

Applies the parser `p`, savings its result in `r`, then calls `pred(r,
user_data)` and returns success if and only if the call to `pred()` returns
true.

On success, returns the result of `p`.

### `HParser *h_and(const HParser *p)`

Applies the parser `p` but does _not_ consume the input.  This is useful for
lookahead; for example, suppose you already have a parser `hex_p` that parses
numbers in hexadecimal format (including the leading "0x").  This parser will
then apply the `hex_p` parser if and only if the input begins with "0x":

    HParser *hex_p_with_0x = sequence(and(token((const uint8_t *) "0x", 2)), hex_p);

Result is an empty token; the HParseResult is not NULL, but the AST within
_is_ NULL.

### `HParser *h_not(const HParser *p)`

Applies the parser `p`, does _not_ consume the input, and negates the result.
That is, `h_not` results in failure if and only if `p` results in success.
(This is the opposite of `h_and`.)

Result is an empty token; the HParseResult is not NULL, but the AST within
_is_ NULL.

### `HParser *h_int_range(const HParser *p, const int64_t lower, const int64_t upper)`

Given a parser `p` whose return type is either `TT_SINT` or `TT_UINT`, applies
`p` once then returns success if and only if the result of `p` lies between
`lower` and `upper`, inclusive.  This is equivalent to:

    HParser *my_h_int_range = h_attr_bool(p, intrange, NULL);

Where `intrange` is a function that returns true if and only if the given
integer lies between `lower` and `upper`, inclusive.
