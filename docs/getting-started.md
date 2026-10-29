# Getting started

## 1. Choose a workflow

To apply an existing trusted patch, you need Python 3.11 or later, the matching original firmware package, and a `.fxpatch` file. `patcher.py` uses the Python standard library. It does not connect to a camera or invoke a flashing utility.

To generate a patch, you also need LLVM's `clang`, `ld.lld`, `llvm-objcopy`, and `llvm-nm`, plus the Python dependencies in `requirements.txt`. The compilation target is AArch64 bare-metal; host x86 binaries are not interchangeable. On Windows and Linux, the tools are located through PATH, without a required drive letter or WSL distribution.

To develop modules, you need LLVM, Python, and this source tree. Original firmware and key material are not required. Each `.wlm` targets a specific firmware identity; build separately for ZR and Z9.

## 2. Prepare a workspace

Create these directories in the extracted source tree:

```text
inputs/      Original firmware packages or application components
private/     Your key material; never commit this directory
build/       Build outputs, private intermediate files, and generated patches
```

Create a virtual environment:

```sh
python -m venv .venv
```

Activate it using the command appropriate to your shell, then install dependencies:

```sh
python -m pip install -r requirements.txt
```

Alternatively, invoke the environment's Python executable directly. For USB host tools, also install `requirements-host.txt`. Linux PyUSB requires system libusb. Windows uses the existing WPD driver; replacing the camera driver is unnecessary.

Check `clang --version`, `ld.lld --version`, `llvm-objcopy --version`, and `llvm-nm --version`. Build records identify the compiler and dependencies. Different LLVM versions can produce different patch bytes.

## 3. Generate a ZR patch

Supply the original C1.10 package, a 48-byte AES key/IV file, an 8-byte digest suffix, and an RSA public key in PEM format. The default mode reproduces the r3 firmware bytes. See [Repacker](repacker.md) for material formats and reproduction requirements. Filenames below are examples; these materials are not included in the source distribution.

```sh
python fenestra.py make-patch --target zr-c110 --firmware inputs/ZR__0110.bin --key private/zr-body-key.bin --digest-suffix private/zr-digest-suffix.bin --envelope-key private/zr-envelope-public.pem --work build/zr-local --output build/Fenestra-ZR.fxpatch
```

`build/zr-local` must not already exist. The generator verifies the original package, decoded body, and application component before compiling and independently checking the core. It then recomputes component CRCs, outer digests, and ciphertext replacement ranges. A failed run does not produce a final usable patch. Intermediate files remain available for local troubleshooting and must not be published.

## 4. Generate a Z9 patch

Z9 C5.32 uses a different body encoding. ZR AES material cannot be used for it. Supply the matching 16 MiB periodic XOR stream:

```sh
python fenestra.py make-patch --target z9-c532 --firmware inputs/Z_9_0532.bin --key private/z9-body-mask.bin --work build/z9-local --output build/Fenestra-Z9.fxpatch
```

Digest tables and seeds are read locally from the original application; they are not included in this distribution. Maintainers with a verified original plaintext/ciphertext pair can use `original_twins` in `host/z9_update_codec.py` to derive and validate the stream locally. That function does not access a device or retrieve device credentials.

## 5. Apply a patch

```sh
python patcher.py --firmware inputs/ZR__0110.bin --patch build/Fenestra-ZR.fxpatch --output build/ZR__0110.bin
python patcher.py --firmware inputs/Z_9_0532.bin --patch build/Fenestra-Z9.fxpatch --output build/Z_9_0532.bin
```

The output must not already exist. In-place replacement of the original is not supported. The patcher prints the original and result SHA-256 digests and reads back the result for verification. A digest establishes byte identity, not a digital signature; obtain patches and their reference digests from a trusted release channel.

The tool does not copy files to a card, connect to a camera, or initiate installation. Installation uses the target camera's normal firmware update procedure. Confirm the model, firmware version, power supply, and storage requirements before installing.

## 6. Build only the core from an original application

```sh
python fenestra.py build --target zr-c110 --firmware inputs/zr-original-application.bin --output build/zr-core
python fenestra.py build --target z9-c532 --firmware inputs/z9-original-application.bin --output build/z9-core
```

Here, `--firmware` denotes the complete original application component, not the encrypted update package. Its complete SHA-256 must match the profile. The output is an application image with local build checks; it is not an installable camera update package.

## 7. Camera controls and modules

The Fenestra menu provides Activate, Auto activate, Shutdown guard, module management, and USB Debug. WonderLantern hosts module pages. Auto activate is off by default; existing card settings remain effective. A flag left by an abnormal shutdown prevents automatic loading and displays `Disabled due to abnormal shutdown`. Clear the guard only after checking the device state. Reboot to restore the default execution state; general hot unloading is not supported.

Place target-compatible `.wlm` files in `FENESTRA/` on the card. Disabled files use `.wlm.disabled`. Disabling an already activated module takes effect at the next boot and does not imply that native firmware has released its callback pointers. Do not enable two copies of the same module.

## 8. Troubleshooting

| Failure | Action |
| --- | --- |
| Original package SHA mismatch | Check the model, version, and download completeness. Do not edit the profile to bypass verification. |
| Decoded body SHA mismatch | Check the key/IV or stream and target identity. A matching version label alone is insufficient. |
| LLVM tool not found | Put all four tools from the same LLVM installation on PATH. |
| Output directory already exists | Use a new build subdirectory and retain previous results for local comparison. |
| SDK target mismatch | Rebuild the module with the correct `--target`. |
| Unexpected native menu or camera behavior | Stop using that build and report the model, firmware digest, and reproduction steps. |

Local build records can contain original firmware fragments, paths, and compiler commands. Share the relevant error, source revision, and nonsensitive identities instead of uploading the entire `build/` directory.
