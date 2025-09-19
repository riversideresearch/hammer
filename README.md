```
 .----------------.  .----------------.  .----------------.  .----------------.  .----------------.  .----------------. 
| .--------------. || .--------------. || .--------------. || .--------------. || .--------------. || .--------------. |
| |  ____  ____  | || |      __      | || | ____    ____ | || | ____    ____ | || |  _________   | || |  _______     | |
| | |_   ||   _| | || |     /  \     | || ||_   \  /   _|| || ||_   \  /   _|| || | |_   ___  |  | || | |_   __ \    | |
| |   | |__| |   | || |    / /\ \    | || |  |   \/   |  | || |  |   \/   |  | || |   | |_  \_|  | || |   | |__) |   | |
| |   |  __  |   | || |   / ____ \   | || |  | |\  /| |  | || |  | |\  /| |  | || |   |  _|  _   | || |   |  __ /    | |
| |  _| |  | |_  | || | _/ /    \ \_ | || | _| |_\/_| |_ | || | _| |_\/_| |_ | || |  _| |___/ |  | || |  _| |  \ \_  | |
| | |____||____| | || ||____|  |____|| || ||_____||_____|| || ||_____||_____|| || | |_________|  | || | |____| |___| | |
| |              | || |              | || |              | || |              | || |              | || |              | |
| '--------------' || '--------------' || '--------------' || '--------------' || '--------------' || '--------------' |
 '----------------'  '----------------'  '----------------'  '----------------'  '----------------'  '----------------' 
```

Hammer is a parsing library. Like many modern parsing libraries, it provides a parser combinator interface for writing grammars as inline domain-specific languages, but Hammer also provides a variety of parsing backends. It's also bit-oriented rather than character-oriented, making it ideal for parsing binary data such as images, network packets, audio, and executables.

Hammer is written in C and provides a packrat parsing backend.

Hammer currently builds under Linux, OS X, and Windows.

## Features

- **Bit-oriented** -- grammars can include single-bit flags or multi-bit constructs that span character boundaries, with no hassle
- **Thread-safe, reentrant** (for most purposes; see Known Issues for details)
- **Benchmarking for parsing backends** -- determine empirically which backend will be most time-efficient for your grammar
- **Parsing backends:** -- currently only Packrat parsing is supported

## Installing

### Prerequisites

- [SCons](http://scons.org/)

### Optional Dependencies

- pkg-config (for `scons test`)
- glib-2.0 (>= 2.29) (for `scons test`)
- glib-2.0-dev (for `scons test`)

To build, type `scons`.
To run the built-in test suite, type `scons test`.
To avoid the test dependencies, add `--no-tests`.
For a debug build, add `--variant=debug`.

To make Hammer available system-wide, use `scons install`. This places include files in `/usr/local/include/hammer` and library files in `/usr/local/lib` by default; to install elsewhere, add a `prefix=<destination>` argument, e.g. `scons install prefix=$HOME`.

## Usage

Just `#include <hammer/hammer.h>` (also `#include <hammer/glue.h>` if you plan to use any of the convenience macros) and link with `-lhammer`.

If you've installed Hammer system-wide, you can use `pkg-config` in the usual way.

To learn about hammer check:

- the [user guide](https://github.com/UpstandingHackers/hammer/wiki/User-guide)
- [Hammer Primer](https://github.com/sergeybratus/HammerPrimer) (outdated in terms of code, but good to get the general thinking)
- [Try Hammer](https://github.com/sboesen/TryHammer)

## Examples

The `examples/` directory contains some simple examples, currently including:

- [base64](https://en.wikipedia.org/wiki/Base64)
- [DNS](https://en.wikipedia.org/wiki/Domain_Name_System)

## Contributing

For information on contributing to Hammer, including development setup, code formatting guidelines, and documentation generation, please see [DEVELOPMENT.md](DEVELOPMENT.md).

## Known Issues

The regular expression backend is potentially not thread-safe (thanks to Martin Murray for pointing this out). A full rewrite of this backend is on the roadmap already due to some unexpected nondeterminism in the current implementation; we plan to fix this problem in that rewrite.

## Contact

Send an email to parsing@riversideresearch.org

## Acknowledgment

This material is based upon work supported by the Defense Advanced Research Projects Agency (DARPA) under Prime Contract No. HR001119C0077. Any opinions, findings, and conclusions or recommendations expressed in this material are those of the author(s) and do not necessarily reflect the views of the Defense Advanced Research Projects Agency (DARPA).

This work is a fork of the repository found at: https://gitlab.special-circumstanc.es/hammer/hammer

Distribution A: Approved for Public Release