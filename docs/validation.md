# Testing and compatibility

## Host tests

Install the dependencies and run from the source root:

```sh
python -B -m unittest discover -s tests -v
python -B verify_source.py
```

Unit tests cover patch round trips, input identity, truncation, overlapping and out-of-bounds ranges, content digests, file protection, and host protocols. `verify_source.py` compares files with the distribution inventory. In a freshly extracted tree without local outputs, add `--distribution` to reject unlisted files as well.

## Core builds

Install LLVM and the Python dependencies, then supply the original application component matching each target profile:

```sh
python fenestra.py build --target zr-c110 --firmware inputs/zr-original-application.bin --output build/verify-zr
python fenestra.py build --target z9-c532 --firmware inputs/z9-original-application.bin --output build/verify-z9
```

The builder checks input identity, binding windows, branch targets, linked-region sizes, and dependencies. Output directories must not already exist. The r3 reproduction toolchain is LLVM 19.1.1 targeting AArch64 bare-metal. Other toolchains may produce different machine code.

For `make-patch`, the default `--release r3` requires the complete application and update package to match fixed digests. Use `--release custom` for modified code, and test the affected startup paths, interfaces, modules, and resource lifetimes. Expected update-package identities are documented in [Repacker](repacker.md).

## Platform tests

Test native initialization, the inactive state, manual activation, module loading, fault recovery, and normal shutdown separately. Compare original and candidate firmware using the same device model, inputs, and configuration. Observe task creation, callback order, control registers, and menu responses. Check flash operations independently. See the [porting guide](porting.md) for the full platform workflow.

Function-level instruction tests, full-system emulation, and hardware tests address different behaviors. Emulation does not replace hardware testing of interrupts, cache coherence, DMA, power, or storage.

## Compatibility limits

- Supported firmware identities are ZR C1.10 and Z9 C5.32. Other versions require independent profiles.
- The core is experimental; complete hardware installation and runtime qualification is not finished.
- Auto activate defaults to off, but existing card settings may enable automatic loading.
- Module capacity depends on the file format, available memory, tasks, and retained resources. See the module format and lifecycle documentation.
- Published native callbacks can retain module code. Disabling a module takes effect at the next boot; general hot unloading is not supported.
- USB, remote debugging over SSH, and the Linux agent also depend on host drivers, network conditions, target ABI, and device permissions.
