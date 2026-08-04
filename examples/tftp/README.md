#  TFTP Packet Parser

This program will parse TFTP packets to check their validity.

Parser implementation based on [RFC 1350](https://www.rfc-editor.org/rfc/rfc1350).

Testing was done with hand typed hex packets, and is not yet comprehensive.


## Build and Run

- Prerequisites:
```shell
sudo apt install build-essential scons
```

- To build against the current checkout, run this once from the repository root:
```shell
scons examples
```

- One-liner using `make`:
```shell
make clean && make && make run
```

By default, the Makefile uses a checkout-local Hammer build at `../../build/opt/src` when available. If no checkout-local Hammer library is available, it falls back to `pkg-config libhammer`.
