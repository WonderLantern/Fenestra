# Nikon Z8 C3.11

The **do not flash** notice in the project README applies to this experimental port. The target is tied to the original Z8 C3.11 update and the exact MAIN application identified in [the target profile](../targets/nikon-z8-c311.json).

## Build and verify locally

Install Python and the LLVM tools listed in [Getting started](getting-started.md). Use the original eg2070 MAIN application with the trailing two-byte component CRC removed. The application must be 81,807,709 bytes with SHA-256 `2b43a31c9de838f84b12416de4348a7004e65be07f853ec56ec2b10f3a8e3853`.

```sh
python -B fenestra.py build --target z8-c311 --firmware inputs/z8-c311-main.bin --output build/z8-core
```

Choose a new build directory. The builder emits the ELF, core binary, patched application and verification receipt. It checks the original code reservation and hook preimages, extracts the displaced instructions from the local input, and reconstructs the entire original application from the candidate. The independent verifier checks all 37 branches against the linked ELF and exercises compiled page lookup and navigation with explicit Unicorn fixtures.

## Optional local container round trip

For a container test, supply the official `Z_8_0311.bin` and an external GPL-3.0-only NikonFirmwarePatch container codec. The codec path and both input digests are in the target profile. The matching revision contains the [container codec and resource profile](https://github.com/PIPA12344/Fenestra-Z8-C311-source/tree/af1db10c43c295213aa897f373849b6c4f37cc33/vendor/NikonFirmwarePatch-container). Point the command at the external directory containing `z8prores/package.py` and `resources/input.json`.

```sh
mkdir -p build
python -B fenestra.py make-patch --target z8-c311 --release custom --firmware inputs/Z_8_0311.bin --external-project /path/to/NikonFirmwarePatch-container --work build/z8-patch-work --output build/z8-local.fxpatch
python -B patcher.py --firmware inputs/Z_8_0311.bin --patch build/z8-local.fxpatch --output build/z8-local-roundtrip.bin
```

The round-trip check verifies the rebuilt MAIN, original component directory, and unchanged plaintext outside MAIN and the package CRC. Keep original and generated firmware, local patches and build receipts out of source control.

## Validation scope

The [platform notes](../src/platform/z8_c311_full/README.md) describe menu ownership and the model-specific bindings. Tests cover text-bank growth, card-volume selection and the cache-safe read adapter using synthetic host fixtures. The build and verification receipts describe the checks performed on the exact linked application. Physical-camera qualification of this source configuration remains outstanding.
