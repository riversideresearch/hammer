# Development Guide

This document provides guidelines and instructions for developers working on the Hammer parsing library.

Tested on Ubuntu 22.04 and 24.04 (VM and WSL).

## Contributor Setup

### Formatting

Install formatting tools:

```bash
sudo apt install -y clang-format
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

### Version management

The repository centralizes the semantic version in the `VERSION` file. Update that file to bump the project version. All downstream artifacts read from it:

- `VERSION`: semantic version (for example: `1.0.0`)
- GitHub tags: `v{VERSION}` (for example: `v1.0.0`)
- Debian packages: `{VERSION}-1`
- Shared library: `libhammer.so.{VERSION}`
- `pkg-config` entries read the same version

### Riverside Research workflow

If you have access to the internal Riverside Research Bitbucket repository, push your work to a branch there first. Treat the internal repository as the source of truth for integration, then promote approved release branches to the public GitHub repository so the public CI/CD pipeline can run before anything lands on `public/main`.

Configure both remotes in your local checkout so you can move changes between the internal Bitbucket repository and the public GitHub repository without changing clone directories:

```bash
git remote add internal ssh://git@ssh.bitbucket.riversideresearch.org:7999/secpar/hammer.git
git remote add public git@github.com:riversideresearch/hammer.git
git fetch internal
git fetch public
git remote -v
```

If one of those remote names already exists, update its URL instead:

```bash
git remote set-url internal ssh://git@ssh.bitbucket.riversideresearch.org:7999/secpar/hammer.git
git remote set-url public git@github.com:riversideresearch/hammer.git
```

Note: periodically run `git fetch --prune` to remove stale remote-tracking branches that no longer exist on the remote:

```bash
git fetch internal --prune
git fetch public --prune
```

The expected promotion path is:

```text
internal/<feature-branch>
  -> internal/release/vX.Y.Z
  -> internal/main
  -> public/release/vX.Y.Z
  -> public/main
```

Create a feature branch from the current internal main:

```bash
git fetch internal
git switch internal-main
git pull --ff-only internal main
git switch -c <feature-branch>
```

Push feature work to the internal Bitbucket repository:

```bash
git push -u internal <feature-branch>
```

Create or update the internal release branch:

```bash
git fetch internal
git switch -c release/vX.Y.Z internal/main
git push -u internal release/vX.Y.Z
```

For an existing release branch, start from its current remote state:

```bash
git fetch internal
git switch release/vX.Y.Z
git pull --ff-only internal release/vX.Y.Z
```

After review, merge the feature branch into `internal/release/vX.Y.Z`:

```bash
git fetch internal
git switch release/vX.Y.Z
git pull --ff-only internal release/vX.Y.Z
git merge --no-ff internal/<feature-branch>
git push internal HEAD:release/vX.Y.Z
```

Make release commits on `internal/release/vX.Y.Z`, such as incrementing the version or updating changelogs, then merge the finalized release branch into `internal/main` before publishing so the internal and public repositories are promoted from the same release state:

```bash
git fetch internal
git switch internal-main
git pull --ff-only internal main
git merge --no-ff internal/release/vX.Y.Z
git push internal HEAD:main
```

Push the release branch to the public GitHub repository to trigger CI/CD without touching `public/main`:

```bash
git fetch public
git push public internal/release/vX.Y.Z:release/vX.Y.Z
```

Open a GitHub pull request with `release/vX.Y.Z` as the compare branch and `main` as the base branch. After the public CI/CD pipeline passes and the release is approved, fast-forward `public/main` from the public release branch:

```bash
git fetch public
git switch main
git pull --ff-only public main
git merge --ff-only public/release/vX.Y.Z
git push public HEAD:main
```

Before pushing to `public/main`, inspect exactly what will be published:

```bash
git log --oneline public/main..public/release/vX.Y.Z
git diff --stat public/main..public/release/vX.Y.Z
```

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

### Building and testing language bindings

Install the required tools:

```bash
sudo apt install swig default-jdk libgtest-dev golang-go
pip install setuptools
```

Build and test all language bindings:

```bash
scons bindings=all test
```

To target a specific binding, pass it individually and use its alias (`testpython`, `testjava`, `testcpp`, or `testgo`):

```bash
scons bindings=python testpython
scons bindings=java testjava
scons bindings=cpp testcpp
scons bindings=go testgo
```

If `JAVA_HOME` is not set, the build locates `javac` via `PATH`. To use a specific JDK:

```bash
JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64 scons bindings=java
```

### Linting Python and SCons files

Install `ruff`:

```bash
sudo apt install pipx
pipx install ruff
```

Lint all Python and SCons files with:

```bash
ruff check $(find . -path ./build -prune -o \( -name "*.py" -o -name "SConstruct" -o -name "SConscript" \) -print)
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
