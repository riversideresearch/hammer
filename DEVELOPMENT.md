# Development Guide

This document provides guidelines and instructions for developers working on the Hammer parsing library.

Tested on Ubuntu 22.04 and 24.04 (VM and WSL).

## Contributor Setup

### Formatting and pre-commit

Install formatting tools:

```bash
sudo apt install -y clang-format pre-commit
```

Use `clang-format` to keep C code consistent.
Examples:

- Format all C sources and headers:
```bash
clang-format -i **/*.c **/*.h
```
- Format a single file:
```bash
clang-format -i path/to/file.c
```

Install the pre-commit hooks defined in `.pre-commit-config.yaml` to run formatting automatically:
```bash
pre-commit install
```

### Version management

The repository centralizes the semantic version in the `VERSION` file. Update that file to bump the project version. All downstream artifacts read from it:

- `VERSION`: semantic version (for example: `1.0.0`)
- GitHub tags: `v{VERSION}` (for example: `v1.0.0`)
- Debian packages: `{VERSION}-1`
- Shared library: `libhammer.so.{VERSION}`
- `pkg-config` entries read the same version

### Test coverage

Install coverage tools:
```bash
sudo apt install lcov xdg-utils
```

Generate coverage and open the HTML report:
```bash
scons -c --variant=debug && scons --coverage --variant=debug test && mkdir -p coverage && lcov --directory build/debug/src --zerocounters && lcov --ignore-errors gcov --capture --initial --directory build/debug/src --output-file coverage/base.info && scons --coverage --variant=debug test && lcov --ignore-errors gcov --capture --directory build/debug/src --output-file coverage/test.info && lcov --add-tracefile coverage/base.info --add-tracefile coverage/test.info --output-file coverage/coverage.info && genhtml coverage/coverage.info --output-directory coverage/html && xdg-open coverage/html/index.html
```

Notes:
- On WSL, replace `xdg-open` with `wslview` (from the `wslu` package).
- Coverage and object files (`.gcov`, `.gcno`, `.gcda`, `.o`) are generated under `build/debug/` or `build/opt/`.
- To generate `.gcov` files manually: `scons --coverage --variant=debug gcov`.

### Linting Python and SCons files

Install `ruff`:
```bash
sudo apt install pipx
pipx install ruff
```

Lint all Python and SCons files with:
```bash
ruff check $(find . -name "*.py" -o -name "SConstruct" -o -name "SConscript")
```

Notes:
- `ruff` configuration lives in `ruff.toml`.

### Generating documentation (Doxygen)

Install Doxygen:
```bash
sudo apt install doxygen
```

Build the documentation:
```bash
doxygen Doxyfile
```

Output will be in `docs/html/`. Open the main page:
```bash
xdg-open docs/html/index.html
```

For WSL, use `wslview` instead of `xdg-open`.

## TODO Items (previously TODO)

- Make `h_action` functions be called only after parse is complete.
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
