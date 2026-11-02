# Nikon Zf C3.01 (link81)

link81 was installed and tested on a Nikon Zf running C3.01. Fenestra activates,
discovers and executes `demo-zf.wlm`, and displays the demo animation. The tester
confirmed normal operation. There are currently no reported link81 problems.

The port keeps the existing Fenestra and WonderLantern menu entry points and
shared module API. Z9 and ZR use their existing build paths.

## Zf integration

- Native allocator, MMU/IRQ tables, card functions, menu/text/page structures,
  display and UI dispatch profiles are bound to C3.01 addresses and layouts.
- SYSCTL startup-complete (20), shutdown (8), card removal (24) and mount (25)
  events drive the retained owner, module workers and cleanup lifecycle.
- Modules use the existing loader, private execution arena, EL0 entry/return,
  services, module management and WonderLantern pages. The admission check uses
  the EG **payload** identity, excluding its two-byte CRC.
- Module reads use at most 511 bytes per native request. Zf's partial-sector
  cached copy path avoids direct DMA into buffers sharing cache lines with
  loader metadata. Native error codes, short reads and EOF are preserved.
- Zf FAT/exFAT directory names are canonicalized to lowercase before shared
  discovery, including names returned as `DEMO-ZF.WLM` by the camera.
- Ordinary module pages use the native visible viewport, rather than the whole
  scrolling backing allocation. This fixes the stretched character rain/text.
- Native PTP lookup/session/operations hooks and the host model/version
  contract are integrated. USB Debug remains a user-controlled setting.

Hardware confirmation above covers installation, activation and the demo. Other
individual SDK services have source bindings and synthetic coverage; the demo
test is not a test of every photo, preview or recording operation.

## Build the tested EG

Use Python 3.11+ and GNU AArch64 GCC **13.3.0** with binutils **2.42** (Linux or
WSL). The complete original EG, including its CRC, must be supplied locally:

| Input | Bytes | SHA-256 |
| --- | ---: | --- |
| Zf C3.01 EG | 79,144,343 | `0b53631396314e17e5b5a46a6758a35ae521c1e8bddc2fb59b61bcf72e4889f8` |

From the repository root, choose a new build directory:

```sh
python -B fenestra.py build-zf \
  --application /local/inputs/zf-c301-eg.bin \
  --output-dir build/zf-link81
```

`build/zf-link81/link81.eg.bin` must have SHA-256
`acfb28499112f73f75f31f82f2a442ea603e3e19affda54a0656204a13cdd7f8`.
The builder refuses a different base core, adapter layout or final EG.

To reproduce the exact hardware-tested layout, `build_zf_core.py` and
`build_zf_image.py` first build the frozen base. `build_zf_link81.py` then applies
the four checked corrections above: module identity, cached card reads,
directory case and visible viewport. Each replacement checks its original
bytes; the final module CRC is recomputed. The small C replacements live in
`card_read.c` and `directory_case.c`. The base source deliberately retains its
pre-correction layout. Use the link81 entry point for the delivered image.
Existing `FX_ZF_EXPERIMENTAL_*` macro names are retained to preserve that layout.
No emulator or camera is used by these build commands.

## Generate a local update package

The package tool takes the original update, its complete decoded body, the new
EG, the local eight-byte digest suffix and the complete body XOR stream.
These inputs must be obtained separately and stay outside the source checkout.
The repository contains no keys, streams, original firmware or dumps.

```sh
python -B fenestra.py make-zf-package \
  --firmware /local/inputs/Z_f_0301.bin \
  --body /local/inputs/zf-c301-body.bin \
  --application build/zf-link81/link81.eg.bin \
  --k8 /local/private/zf-digest-suffix.bin \
  --stream /local/private/zf-body-stream.bin \
  --output-dir build/zf-link81-package
```

The tool checks original input identities, reproduces the original package
digest and decoding, replaces only EG, repairs module/body CRCs, recalculates
the 20-byte package digest, and verifies equal size and decoding round trips.
Version-related header bytes remain C3.01. Other components are preserved.
Sensitive inputs are recorded only as present/size/local-only in the receipt.
The receipt describes local integrity checks; hardware results belong to the
specific reference package below.

The reproduced `Z_f_0301.fenestra.candidate.bin` is 107,820,261 bytes, SHA-256
`3e9fdbfac7647589cd5c68f7d7b7510903042f07eefa56434387b041b560da55`.
This is the link81 package tested on hardware. Build outputs stay local.

## Build and install the demo module

```sh
python -B host/build_sdk_module.py modules/demo/sdk.c \
  --id demo-zf --target zf-c301 --output-dir build/demo-zf
```

Copy `build/demo-zf/demo-zf.wlm` to **`FENESTRA/demo-zf.wlm`** on the card.
Enable it in Fenestra's Modules menu, then activate Fenestra. If it was enabled
after activation, restart before testing. Open the demo through WonderLantern.
`module.json` is a build receipt and is not copied to the card.

With the GNU toolchain above, the demo is 7,647 bytes, SHA-256
`c15a4ab59b8dc77a78f0b0212e2edb1914699680f84de244c252e1e721b1d5da`.
LLVM is also supported for module development. Build separately for each target:
Zf, Z9 and ZR module identities differ. Keep a model suffix in module IDs and
filenames, such as `demo-zf.wlm`, to prevent mixups.

## Local verification

```sh
python -B -m unittest discover -s tests -v
```

The suite covers the existing Z9/ZR routes and Zf owner lifecycle, admission,
card errors, module/worker execution, UI publication, PTP gates and adapters.
Some host fixtures require Linux memory mappings or ELF section collection;
Windows COM tests run in the Windows lane. To additionally check the original
Zf bindings, set `FENESTRA_ZF_EG` to the locally supplied original EG path.
Private inputs and generated instruction headers are never part of the source
distribution.
