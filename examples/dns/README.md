# DNS Packet Parser

## Usage
Build
```shell
make
```

By default, the Makefile uses a checkout-local Hammer build at `../../build/opt/src` when available. Run this from the repository root first if you want to build against the current checkout instead of an installed Hammer:

```shell
scons examples
```

If no checkout-local Hammer library is available, the Makefile falls back to `pkg-config libhammer`.

Testing all packets
```shell
cd tests && python3 runtests.py
```
Testing individual packets
```shell
./dns_parser tests/<directory>/<packet_binary>
```
ex.
```shell
./dns_parser tests/good_packets/full_response_pass.bin
```
