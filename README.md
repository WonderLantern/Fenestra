# Fenestra

> **Early development:** Fenestra is still at an early stage. Major changes, including breaking changes to interfaces and behavior, are likely as the project evolves. Backward compatibility is not yet guaranteed.

Fenestra is a camera firmware extension runtime that loads independent `.wlm` modules from a memory card. The Fenestra menu manages activation, modules, and debugging. WonderLantern hosts module pages and settings. Adding a module does not require rebuilding the core. A character-rain animation is included as `modules/demo/`.

This repository contains the core, module SDK, host debugging tools, firmware patch tools, and development documentation. Supply the original firmware and patch-generation material yourself. Build tools extract required original instructions locally from the specified firmware.

## Supported targets

| Profile | Original firmware | Source support | Prebuilt patch |
| --- | --- | --- | --- |
| `zr-c110` | Nikon ZR C1.10 | Core, SDK, native menus, and PTP debugging | Not available |
| `z9-c532` | Nikon Z9 C5.32 | Core, SDK, native menus, and PTP debugging | Available |

Other firmware versions require their own validated profiles. Fenestra is experimental and has not completed full hardware qualification. Auto activate defaults to off; existing card settings remain effective.

## Getting started

Download the source archive and the patch for your exact firmware from [Releases](https://github.com/WonderLantern/Fenestra/releases). Applying a prebuilt patch requires Python 3.11 or later and the matching original firmware. Keys and a compiler are not required.

```sh
python patcher.py --firmware inputs/Z_9_0532.bin --patch Fenestra-Z9-C532-r3.fxpatch --output build/Z_9_0532.bin
```

Create `inputs` and `build` first. Place the original firmware in `inputs` and the patch in the source root. The output file must not already exist. The tool verifies the complete original SHA-256 before producing a firmware file; it does not flash the camera.

Generating a patch requires LLVM, the Python build dependencies, and the appropriate key material. Developing standalone modules requires neither original firmware nor keys. See [Getting started](docs/getting-started.md) for setup and commands.

## Porting quick start

A platform port targets an exact camera model and firmware build. Start with the [architecture and resource lifetimes](docs/architecture.md), then follow the [platform porting guide](docs/porting.md). Existing profiles are examples of the integration structure; their addresses and calling conventions must be established independently for a new target.

1. **Prepare the inputs and toolchain.** Follow [Getting started](docs/getting-started.md) to install Python, LLVM, and build dependencies. Obtain the target's original update package and application component, record their complete SHA-256 identities, and distinguish package offsets from application virtual addresses. Keep firmware and key material local.
2. **Define the target profile.** Study [host/platform_profiles.py](host/platform_profiles.py) and the [SDK binding reference](docs/api-reference.md). Record the model, firmware identity, image layout, module identity, and native binding contracts. Locate startup-complete, shutdown, card, menu, and input paths in that target's firmware. For every hook, establish the original instruction window, continuation, register/stack convention, and execution context.
3. **Implement platform adapters.** Use [src/platform/](src/platform/) and the shared native adapters as entry points. Connect memory/cache, tasks, card access, UI/input, display, USB/PTP, and shutdown services through named bindings. Preserve owner and callback lifetimes. Report an unsupported capability as unavailable instead of returning success from a placeholder. See [Architecture](docs/architecture.md) and [API reference](docs/api-reference.md).
4. **Integrate the build and package format.** Extend the profile dispatch and checks used by [fenestra.py](fenestra.py), [host/displaced.py](host/displaced.py), and [host/native_binding_check.py](host/native_binding_check.py). Extract displaced instructions from the hash-verified input and validate relocation, branch destinations, and executable space. Add the target's container handling in [host/repack.py](host/repack.py) using the [repacker guide](docs/repacker.md). Verify a rebuilt container independently; an application image alone is not an update package.
5. **Bring up the core in stages.** Establish an original-firmware baseline, then test inactive startup and normal shutdown before activation, module loading, menu/input, and USB debugging. Check native startup behavior as well as flash activity. Follow [Testing and compatibility](docs/validation.md) and [Debugging](docs/debugging.md). Emulator results and hardware results are separate; document which paths each actually exercises.
6. **Validate a module and document the port.** Build the hello example and demo for the new target using [Module development](docs/module-development.md). Check the target identity, capabilities, message exchange, UI, and retained resources against the [module format](docs/module-format.md). Document supported bindings, known limitations, build commands, and input identities. Run affected shared-core regressions on existing targets and follow [Contributing and releases](docs/publication.md) when preparing the source distribution.

For a module targeting an already supported profile, begin with [Module development](docs/module-development.md) and [API reference](docs/api-reference.md). Use capability queries and named services, then build separately for each firmware identity. A module that needs an unavailable native service requires a platform adapter for that service.

## Documentation

- [Installation, builds, and troubleshooting](docs/getting-started.md)
- [Architecture and resource lifetimes](docs/architecture.md)
- [Module development tutorial](docs/module-development.md)
- [`.wlm` file format](docs/module-format.md)
- [SDK and named binding reference](docs/api-reference.md)
- [Platform porting guide](docs/porting.md)
- [Repacker and ciphertext patch format](docs/repacker.md)
- [USB/PTP, RTOS/Linux, and remote debugging](docs/debugging.md)
- [Testing and compatibility](docs/validation.md)
- [Contributing and releases](docs/publication.md)

## License

[CC BY-NC-SA 4.0](LICENSE), with a separate [commercial photography permission](EXCEPTIONS.md). Commercial photography, filmmaking, and paid shooting assignments are permitted. The permission does not authorize selling covered software or modules, or providing paid flashing services to third parties. Separately licensed dependencies retain their own terms.
