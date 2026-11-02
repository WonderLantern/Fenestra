# Nikon Z6III C2.01 port development

The local Z6III port currently supplies a read-only native card adapter and an
original-input verifier. It builds a relocatable AArch64 object. A resident
Fenestra core, module SDK target and update package are not available yet.

## Implemented boundary

The input is the complete original C2.01 EG member including its two CRC bytes:
88,830,367 bytes, SHA-256
`d78e4d0358f550d9f3a3a9da3a8beee314a3b90de8bcc13117e029f7821cf252`.
The target metadata independently fingerprints the native open, read and close
wrapper extents at mapped MAIN addresses `0x402e3674`, `0x402e3774` and
`0x402e36d4` (image base `0x40160000`). These are function entries.

The adapter accepts lowercase module paths under `A:\FENESTRA\`, read-only.
Its scope owns one native file descriptor. Reads preserve native errors, short
reads and EOF and reject transfer counts exceeding the native request. An
impossible count or failed close makes the scope uncertain; it cannot be
reinitialized or used to issue more calls. The owner must retain the scope and
associated storage until retirement is established. There is no timeout-based
cancellation of a native call.

Each native request is capped at 511 bytes. This keeps requests below a sector
while bringing up the port. Zf's reported cache/DMA failure motivated the bound;
physical Z6III DMA/cache behavior has not been tested, and the bound alone does
not prove buffer coherency. The caller must continue reading after a short
read when appropriate, using the actual returned count.

## Build and tests

Supply the original EG locally and select a new output directory:

```sh
python -B fenestra.py build-z63-adapter \
  --application /local/inputs/eg2010_0201i0.bi \
  --output-dir build/z63-adapter
python -B -m unittest discover -s tests -p test_z63_card.py -v
```

Clang must support `--target=aarch64-none-elf`. Output consists of `card.o` and
`adapter.json`; neither is an installable firmware image. Set `FENESTRA_Z63_EG`
to additionally verify the complete original and binding fingerprints in tests.
The host fixture substitutes native IO and checks path admission, actual-count
handling, request bounds, errors, close retention and scope reuse rejection.
It does not execute Nikon's lower storage implementation.

## Remaining integration and acceptance

Before exposing `z63-c201` through the normal core builder or SDK, independently
establish startup/shutdown/card event IDs and their owner context; executable
space and displaced hook instructions; allocator/MMU/cache/IRQ and native task
contracts; module execution and retirement; menu/text/page/input/display
bindings; and PTP/bridge capability availability. Wire only qualified services
into the retained owner, with unsupported capabilities absent.

Full-system acceptance must begin with stock boot, then inactive patched boot
and shutdown, manual activation, discovery, module entry/return/drawing, card
removal, faults and restart. Original-instruction replay and host tests are
separate from those full-system checks and from physical-camera qualification.
