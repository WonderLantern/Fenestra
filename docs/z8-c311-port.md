# Nikon Z8 C 3.11 (z8-c311)

## Z8 integration

The Z8 shares the Z9's AArch64 core, but none of the Z9's verified bindings carry
over. The port pins its own container identity, page namespace, hook sites and
service addresses; two bindings that proved genuinely different are part of the
profile:

- Directory enumeration uses the Z8 callback API (FW_LIST) instead of the Z9
  find-first entries; staging is staged in the state heap because the module
  region is not writable on this target.
- Staged-module identity is pinned to the Z8 C 3.11 eg2070 digest; the Z9 C5.32
  value never matches this image.

Enabled today: core build, independent verification, container repack, the SDK
module target, startup-file activation with a full session load
(install → activate → stage → load → EL0 user call) and sysctl lifecycle
events (the system-event dispatcher gate records startup/mount and card
removal/shutdown into a lifecycle inbox; forcing the worker stop path is
pending natural-context delivery), all validated under a matching
Z8 emulator.

Enabled: native menu trio, font request sites, the render-profile cluster, the menu quintuple and the page construction are pinned and hooked (validation sections 4b/4c). The menu-enabled build (`build_z8_core.py --menu-enabled`) constructs the Fenestra pages at install and menu-key navigation/module activation are verified in emulation; the default build keeps construction dormant (headless-equivalent)
pending), device-side PTP (the profile reports `FX_CAPABILITY`), Linux bridge,
USB debug and fonts. Unsupported capabilities report `FX_CAPABILITY` rather than
faking success; per-item evidence and boundaries live in
[validation-z8-c311.md](validation-z8-c311.md).

## Build the core image

Requires the LLVM tools (`clang`, `ld.lld`, `llvm-objcopy`, `llvm-nm`) and the
original application component `eg2070_0311b0.bi`
(SHA-256 `f85e0138e2f5e660ece8560de62e0d12cb795e0e14983e50f20b61dce906c46c`).

```sh
export Z8_MAIN=/path/to/eg2070_0311b0.bi
python3 fenestra.py build --target z8-c311 --firmware "$Z8_MAIN" --output build/<new-dir>
```

The builder links the core into the image's unused RX padding at `0x401A0000`
(asserted all-zero before linking) and reconstructs the pristine image
byte-for-byte from the patched candidate; an unaccounted mutation fails the
build.

## Generate a local update package

Requires the original update package and the local K8 material (never printed,
never committed).

```sh
export Z8_FW=/path/to/Z_8_0311.bin        # original update package
export Z8_K8=/path/to/sig.json            # local K8 material
python3 fenestra.py make-patch --target z8-c311 --firmware "$Z8_FW" --key "$Z8_K8" \
    --work <new-workdir> --output build/<new-dir> --release custom
```

The container codec is held to a bit-identical standard: encoding the decoded
original must reproduce the package byte-for-byte, and a single-byte
modification must rebuild through the whole structure (module CRC, package CRC,
SHA1 header signature). Both are covered by the test suite.

## Build and install the hello module

```sh
python host/build_sdk_module.py examples/hello/module.c --id hello --target z8-c311 --output-dir build/hello-z8
```

Copy the produced `hello.wlm` to the card as `A:\FENESTRA\hello.wlm`. Activation
currently goes through the startup-file service; native menu-driven activation
is verified on the menu-enabled build (emulation boundary).

## Local verification

```sh
# Container codec and offline unit tests (skips cleanly without local material)
python3 -B -m unittest discover -s tests

# Independent build verification
python3 host/verify_platform_build.py --target z8-c311 --firmware "$Z8_MAIN" \
    --build build/<dir> --output build/<dir>/verification.json

# Reproducible session round trip on an isolated virt machine (no firmware):
# install-equivalent card IO, staging digest admission, private tables, EL0
# entry/return and context restore, plus a corrupted-package control that must
# fail. Requires LLVM on PATH and an explicit QEMU binary.
python3 tests/run_z8_session.py --output build/<new-session-dir> --qemu <qemu-system-aarch64>
```

Per-item evidence chains, unverified boundaries and the reproduction
environment are documented in [validation-z8-c311.md](validation-z8-c311.md).
