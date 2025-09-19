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

Hammer is written in C, but provides bindings for other languages. If you don't see a language you're interested in on the list, just ask.

Hammer currently builds under Linux, OS X, and Windows. 

Features
========
* Bit-oriented -- grammars can include single-bit flags or multi-bit constructs that span character boundaries, with no hassle
* Thread-safe, reentrant (for most purposes; see Known Issues for details)
* Benchmarking for parsing backends -- determine empirically which backend will be most time-efficient for your grammar
* Parsing backends:
  * Packrat parsing
* Language bindings: 
  * C++
  * Java (incomplete)
  * Python
  * Ruby
  * Perl
  * [Go](https://github.com/prevoty/hammer)
  * PHP
  * .NET

Installing
==========
### Prerequisites
* [SCons](http://scons.org/)

### Optional Dependencies
* pkg-config (for `scons test`)
* glib-2.0 (>= 2.29) (for `scons test`)
* glib-2.0-dev (for `scons test`)
* [swig](http://swig.org/) (for Python/Perl/PHP bindings; Perl requires >= 2.0.8; Python 3.x requires >= 3.0.0)
* python2.7-dev (for Python 2 bindings)
* python3-dev (>= 3.5) (for Python 3 bindings)
* a JDK (for Java bindings)
* a working [phpenv](https://github.com/CHH/phpenv) configuration (for PHP bindings)
* [Ruby](https://www.ruby-lang.org/) >= 1.9.3 and bundler, for the Ruby bindings
* mono-devel and mono-mcs (>= 3.0.6) (for .NET bindings)
* [nunit](http://www.nunit.org/) (for testing .NET bindings)

To build, type `scons`.
To run the built-in test suite, type `scons test`.
To avoid the test dependencies, add `--no-tests`.
For a debug build, add `--variant=debug`.

To build bindings, pass a "bindings" argument to scons, e.g. `scons bindings=python`. `scons bindings=python test` will build Python bindings and run tests for both C and Python. `--variant=debug` is valid here too. You can build more than one set of bindings at a time; just separate them with commas, e.g. `scons bindings=python,perl`.

For Python, pass `python=python<X>.<Y>`, e. g. `scons bindings=python python=python2.7` or `scons bindings=python python=python3.5`.

For Java, if jni.h and jni_md.h aren't already somewhere on your include path, prepend
`C_INCLUDE_PATH=/path/to/jdk/include` to that.

To make Hammer available system-wide, use `scons install`. This places include files in `/usr/local/include/hammer` 
and library files in `/usr/local/lib` by default; to install elsewhere, add a `prefix=<destination>` argument, e.g. 
`scons install prefix=$HOME`. A suitable `bindings=` argument will install bindings in whatever place your system thinks is appropriate.

Usage
=====
Just `#include <hammer/hammer.h>` (also `#include <hammer/glue.h>` if you plan to use any of the convenience macros) and link with `-lhammer`.

If you've installed Hammer system-wide, you can use `pkg-config` in the usual way.

To learn about hammer check
* the [user guide](https://github.com/UpstandingHackers/hammer/wiki/User-guide)
* [Hammer Primer](https://github.com/sergeybratus/HammerPrimer) (outdated in terms of code, but good to get the general thinking)
* [Try Hammer](https://github.com/sboesen/TryHammer)

Examples
========
The `examples/` directory contains some simple examples, currently including:
* [base64](https://en.wikipedia.org/wiki/Base64)
* [DNS](https://en.wikipedia.org/wiki/Domain_Name_System)

Contributor Setup
==========
To contribute to Hammer, please make sure you have the following tools installed:

- **clang-format** (v18+) → automatic code formatting based on the `.clang-format` file  
- **Doxygen** → generate API documentation from comments  
- **pre-commit** (optional but recommended) → auto-run `clang-format` before commits  

### Formatting code
Run clang-format before committing to keep code style consistent:

All code files:
```bash
clang-format -i **/*.c **/*.h
```

Apply clang-format to a single file:
```bash
clang-format -i path/to/file.c
```

### Generating documentation
Build developer docs locally with:

```bash
doxygen Doxyfile
```
This will create HTML output in docs/html/index.html.

Known Issues
============
The Python bindings work with Python 2.7, and Python 3.5+.

The requirement for SWIG >= 2.0.8 for Perl bindings is due to a [known bug](http://sourceforge.net/p/swig/patches/324/) in SWIG. [ppa:dns/irc](https://launchpad.net/~dns/+archive/irc) has backports of SWIG 2.0.8 for Ubuntu versions 10.04-12.10; you can also [build SWIG from source](http://www.swig.org/download.html).

The .NET bindings are for Mono 3.0.6 and greater. If you're on a Debian-based distro that only provides Mono 2 (e.g., Ubuntu 12.04), there are backports for [3.0.x](http://www.meebey.net/posts/mono_3.0_preview_debian_ubuntu_packages/), and a [3.2.x PPA](https://launchpad.net/~directhex/+archive/monoxide) maintained by the Mono team.

The regular expression backend is potentially not thread-safe (thanks to Martin Murray for pointing this out). A full rewrite of this backend is on the roadmap already due to some unexpected nondeterminism in the current implementation; we plan to fix this problem in that rewrite.

Contact
=======
Send an email to parsing@riversideresearch.org

ACKNOWLEDGMENT
=====
This material is based upon work supported by the Defense Advanced Research Projects Agency (DARPA) under Prime Contract No. HR001119C0077. Any opinions, findings, and conclusions or recommendations expressed in this material are those of the author(s) and do not necessarily reflect the views of the Defense Advanced Research Projects Agency (DARPA).
 
This work is a fork of the repository found at: https://gitlab.special-circumstanc.es/hammer/hammer
 
Distribution A: Approved for Public Release
