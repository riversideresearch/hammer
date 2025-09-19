# Development Guide

This document combines development notes, hacking guidelines, and TODO items for the Hammer parser library. (TODO, HACKING, and )

## TODO Items (previously TODO)

- Make h_action functions be called only after parse is complete.
- Allow alternative input streams (eg, zlib, base64)
  - Bonus points if layered...
- Add consistency check to the bitreader
- We should support the use of parse-table-based parse methods; add a parse_compile method that must be called before the newly-created parser is used.
- Implement datastructure linearization func
- Implement free func for parsers

## Hacking Guidelines (previously HACKING)

### Privileged Arguments

As a matter of convenience, there are several identifiers that internal anaphoric macros use. Chances are that if you use these names for other things, you're gonna have a bad time.

In particular, these names, and the macros that use them, are:

- `state`: Used by `a_new` and company. Should be an `HParseState*`.
- `mm__`: Used by `h_new` and `h_free`. Should be an `HAllocator*`.
- `stk__`: Used in desugaring. Should be an `HCFStack*`.

### Function Suffixes

Many functions come in several variants, to handle receiving optional parameters or parameters in multiple different forms. For example, often, you have a global memory manager that is used for an entire program. In this case, you can leave off the memory manager arguments off, letting them be implicit instead. Further, it is often convenient to pass an array or `va_list` to a function instead of listing the arguments inline (e.g., for wrapping a function, generating the arguments programatically, or writing bindings for another language.)

Because we have found that most variants fall into a fairly small set of forms, and to minimize the amount of API calls that users need to remember, there is a consistent naming scheme for these function variants: the function name is followed by two underscores and a set of single-character "flags" indicating what optional features that particular variant has (in alphabetical order, of course):

- `__a`: takes variadic arguments as a `void*[]` (not implemented yet, but will be soon.)
- `__m`: takes a memory manager as the first argument, to override the system memory manager.
- `__v`: Takes the variadic argument list as a `va_list`.

### Memory Managers

If the `__m` function variants are used or `system_allocator` is overridden, there come some difficult questions to answer, particularly regarding the behavior when multiple memory managers are combined. As a general rule of thumb (exceptions will be explicitly documented), assume that

> If you have a function f, which is passed a memory manager m and returns a value r, any function that uses r as a parameter must also be told to use m as a memory manager.

In other words, don't let the (memory manager) streams cross.

## Implementation Notes (previously NOTES)

### Parse Result Behavior

**Regarding parse_result_t:**
- If a parse fails, the parse_result_t will be NULL.
- If a parse is successful but there's nothing there (i.e., if end_p succeeds), then there's a parse_result_t but its ast is NULL.

**Regarding input location:**
- If parse is successful, input is left at beginning of next thing to be read.
- If parse fails, location is UNPREDICTABLE.

### Compile-Time Options

If `CONSISTENCY_CHECK` is defined, enable a bunch of additional internal consistency checks.

### Parser Combinator Implementation Notes

**Regarding butnot and difference:**

There's a "do what I say, not what I do" variation in how we implemented these (versus how jsparse did it). His `butnot` succeeds if p1 and p2 both match and p1's result is longer than p2's, though the comments say it should succeed if p2's result is longer than p1's. Also, his `difference` succeeds if p1 and p2 both match, full stop, returning the result of p2 if p2's result is shorter than p1's or the result of p1 otherwise, though the comments say it should succeed if p2's result is shorter than p1's. Whatever; we're doing what the comments say.
