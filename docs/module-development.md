# Module development

## Create a module

Start with `examples/hello/module.c`. It creates a native PAGE and ACTION and receives input through the shared event queue. `modules/demo/sdk.c` also demonstrates display binding, capability queries, pages, foreground focus and batched animation frames.

Place sources under `modules/<name>/` or `examples/<name>/`. The builder currently requires the main source, additional C files and output to reside within the source tree so it can record the complete dependency set. The tree can be copied to any working directory.

```sh
python host/build_sdk_module.py examples/hello/module.c --id hello --target zr-c110 --output-dir build/hello-zr
python host/build_sdk_module.py examples/hello/module.c --id hello --target z9-c532 --output-dir build/hello-z9
python host/build_sdk_module.py modules/demo/sdk.c --id demo --target zr-c110 --output-dir build/demo-zr
python host/build_sdk_module.py modules/demo/sdk.c --id demo --target z9-c532 --output-dir build/demo-z9
```

Each output directory must be new. Outputs include `.wlm`, an intermediate ELF and `module.json`. The JSON file records the build; it is not a runtime module catalog. The camera discovers modules from the card directory without that file.

## Entry contract

```c
int fenestra_module_entry(const FxModuleServices *base);
```

The entry is invoked repeatedly during the module's lifetime. Use static storage for state that must survive calls. Check `base`, structure size, `service_abi` and required capabilities before accessing services. Platform-specific structures require their declared layouts.

A typical state machine:

1. Query capabilities and request named bindings. Submit a request and return.
2. On the next entry, use `fx_sdk_take` to retrieve the matching sequence's reply.
3. Create the module page and controls, completing each request before submitting the next.
4. Retrieve input with `fx_sdk_events`, or yield through scheduling/wait services.
5. Declare EXTENSION or SERVICE through lifecycle services when needed, and terminate according to resource ownership rules.

Success from `fx_sdk_submit` means local submission succeeded; the reply's `result` is the service result. On `FX_BUSY`, normally retain the current phase and retry later without duplicating a non-idempotent operation. `FX_EMPTY` means there is no input available.

## Named interfaces

`fx_sdk_open(&client, api, "camera.display.scene", 1)` requests version 1 of the scene service. The returned handle belongs to the current owner/generation. Read the descriptor before using it. Handles from other module instances are not valid persistent configuration values.

Functions, tables, hooks, devices and resources share a name-resolution boundary but have different operation ABIs. Consult the [API reference](api-reference.md) and headers. Integer handles do not make their operation parameters interchangeable.

For a new platform capability, define a general data structure and operation contract, then implement the profile adapter. Platforms without that capability must return an explicit unavailable result. Feature modules should use these contracts rather than module-specific core opcodes.

## UI

- Represent choices and switches with `FxUiControl` kind/value fields, rather than changing action titles to simulate state.
- Assign stable control IDs, parents and flags. Module settings belong in WonderLantern.
- Check control revision after rebuilding controls so delayed input cannot affect a replacement control.
- Stop expensive drawing when the page is not foreground. Track focus epochs, not just whether a FOCUS event was once received.
- Use logical layouts, paging and capacity queries for large menus.
- Use document/edit services for long text and follow their UTF-8 and message-length rules.

## Large modules, memory and tasks

The current native worker accepts packages up to 1 MiB, loaded images up to 4 MiB, at most 8 segments and 128 relocations. Discovery can enumerate many modules, but active instances also consume owners, stacks, heap, stock tasks and retained resources.

Request a v3 package when explicit stack sizes are needed:

```sh
python host/build_sdk_module.py examples/hello/module.c --id hello --target zr-c110 --native-stack-bytes 131072 --user-stack-bytes 131072 --output-dir build/hello-large-stack
```

Native stacks range from 32 KiB to 1 MiB; user stacks range from 4 KiB to 1 MiB. Both use 4 KiB alignment. v1/v2 default to 64 KiB for each stack. Use `--simd` for code that requires full SIMD context preservation; the capability requirement lets incompatible cores reject it.

Store large data in allocated buffers or card files and access it through block reads or mapping services. Model weights need not occupy static `.wlm` segments. Keep asynchronous task data alive until the task completes. Closing a page does not permit reclamation of resources retained by an owner.

## Distribution

Copy `build/<name>/<id>.wlm` to `FENESTRA/` on the card. IDs are at most 15 characters and use lowercase letters, digits, underscores and hyphens. Enable or disable through module management or the `.wlm` / `.wlm.disabled` suffix. No additional discovery catalog is required.

Document the SDK version, target profile, capabilities, memory requirements, license and supported features. Supply module source as part of the project distribution workflow. Firmware tables and external weights must be provided separately by users where required; do not bundle vendor resources, device logs or serial numbers in public source.

## Testing

Check package format and target identity first, then the message state machine, input, foreground transitions, resource exhaustion and owner termination. QEMU can exercise scheduling, ABI and menu paths. Sensor output, effective RAW depth, cache/DMA behavior and throughput require hardware measurements. Validate a feature's actual output rather than its menu label or container dimensions alone.
