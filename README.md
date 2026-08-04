![Hammer Logo](docs/assets/Hammer-Logo-Full-Color-Horizontal.png)

Hammer is a parsing library. Like many modern parsing libraries, it provides a parser combinator interface for writing grammars as inline domain-specific languages, but Hammer also provides a variety of parsing backends. It's also bit-oriented rather than character-oriented, making it ideal for parsing binary data such as images, network packets, audio, and executables.

Hammer is written in C and provides packrat, regex/RVM, LL(k), LALR(k), and GLR parsing backends.

## Features

- **Bit-oriented** -- grammars can include single-bit flags or multi-bit constructs that span character boundaries with no hassle
- **Thread-safe, reentrant** (for most purposes)
- **Parsing backends** -- Packrat for general parser-combinator grammars, regex/RVM for regular grammars, plus LL(k), LALR(k), and GLR for context-free grammars
- Windows and macOS installation is possible but not officially supported

## Installing

### Prerequisites

- [SCons](http://scons.org/)
- gcc

```bash
sudo apt install gcc scons
```

### Optional Dependencies for Testing

- pkg-config
- glib-2.0-dev

```bash
sudo apt install pkg-config libglib2.0-dev
```

To build, type `scons`.
To run the built-in test suite, type `scons test`.
To avoid the test dependencies, add `--no-tests`.
For a debug build, add `--variant=debug`.

To make Hammer available system-wide, use `scons install`. This places include files in `/usr/local/include/hammer` and library files in `/usr/local/lib` by default; to install elsewhere, add a `prefix=<destination>` argument, e.g. `scons install prefix=$HOME`.

To remove installed files, use `scons uninstall` (with the same `prefix` if non-default).

To check which variant and version of Hammer is currently installed:

```bash
PKG_CONFIG_PATH=/usr/local/lib/pkgconfig pkg-config --variable=variant libhammer
PKG_CONFIG_PATH=/usr/local/lib/pkgconfig pkg-config --modversion libhammer
```

## Usage

Just `#include <hammer/hammer.h>` (also `#include <hammer/glue.h>` if you plan to use any of the convenience macros) and link with `-lhammer`.

If you've installed Hammer system-wide, you can use `pkg-config` in the usual way.

### Parsing Backends

Packrat is the default backend:

```c
HParser *p = h_sequence(h_ch('a'), h_ch('b'), NULL);
HParseResult *result = h_parse(p, (const uint8_t *)"ab", 2);
```

To use another backend, compile the parser before parsing:

```c
h_compile(p, PB_REGULAR, NULL);    /* regex/RVM */
h_compile(p, PB_LL, (void *)2);    /* LL(2) */
h_compile(p, PB_LALR, NULL);       /* LALR(1), the default k */
h_compile(p, PB_GLR, NULL);        /* GLR(1), accepts table conflicts */
```

Backend names can also be queried with `h_query_backend_by_name("regex")`, `h_query_backend_by_name("llk")`,
`h_query_backend_by_name("lalr")`, or `h_query_backend_by_name("glr")`.
See [docs/backends.md](docs/backends.md) for backend differences, limitations, and examples.

To learn about hammer, check:

- the [user guide](https://github.com/UpstandingHackers/hammer/wiki/User-guide)
- [Hammer Primer](https://github.com/sergeybratus/HammerPrimer) (outdated in terms of code, but good to get the general thinking)
- [Try Hammer](https://github.com/sboesen/TryHammer)

## Language Bindings

Hammer provides bindings for use from languages other than C.

### Python

Requires [SWIG](https://www.swig.org/) 4.x, Python 3.8+, and `setuptools`.

```bash
sudo apt install swig
scons bindings=python
```

See [src/bindings/python/README.md](src/bindings/python/README.md) for the full API reference and usage guide.

### Java

Requires [SWIG](https://www.swig.org/) 4.x and JDK 11+.

```bash
sudo apt install swig default-jdk
scons bindings=java
```

See [src/bindings/java/README.md](src/bindings/java/README.md) for the full API reference and usage guide.

### C++

Requires `g++` and `libgtest-dev` (for tests).

```bash
sudo apt install libgtest-dev
scons bindings=cpp
```

See [src/bindings/cpp/README.md](src/bindings/cpp/README.md) for the full API reference and usage guide.

### Go

Requires [SWIG](https://www.swig.org/) 4.x and Go 1.22.2.

```bash
sudo apt install swig golang
scons bindings=go
```

See [src/bindings/go/README.md](src/bindings/go/README.md) for the full API reference and usage guide.

## Examples

The `examples/` directory contains some simple examples, currently including:

- [base64](https://en.wikipedia.org/wiki/Base64)
- [DNS](https://en.wikipedia.org/wiki/Domain_Name_System)
- [NTP](https://en.wikipedia.org/wiki/Network_Time_Protocol)
- [TFTP](https://en.wikipedia.org/wiki/Trivial_File_Transfer_Protocol)

See the example-specific READMEs and the wiki for build and walkthrough details.

## Contributing

For information on contributing to Hammer, including development setup, code formatting guidelines, and documentation generation, please see [DEVELOPMENT.md](DEVELOPMENT.md).

## Contact

Send an email to <parsing@riversideresearch.org>

## Acknowledgment

This material is based upon work supported by the Defense Advanced Research Projects Agency (DARPA) under Prime Contract No. HR001119C0077. Any opinions, findings, and conclusions or recommendations expressed in this material are those of the author(s) and do not necessarily reflect the views of the Defense Advanced Research Projects Agency (DARPA).

This work is a fork of the repository found at: <https://gitlab.special-circumstanc.es/hammer/hammer>

Distribution A: Approved for Public Release
