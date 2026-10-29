# Architecture and resource model

## Layers

| Layer | Responsibilities | Main sources |
| --- | --- | --- |
| Core | Module format, state machines, accounting, messages, scheduling | `src/module.c`, `src/services.c`, `src/module_manager.c` |
| Common native adapters | Binding registry, native calls, UI, display, tasks, memory | `src/platform_native.c`, `src/native_*.c` |
| Platform profile | Firmware ABI, addresses, event identifiers, page layouts, entry points | `host/platform_profiles.py`, `src/platform/` |
| Modules | Feature policy, settings, state machines, algorithms | `modules/demo/`, `examples/hello/` |
| Host tools | Compilation, validation, PTP, container encoding, patch application | `host/`, `fenestra.py`, `patcher.py` |

Addresses belong to profiles. Modules request named services and versions rather than embedding a camera's addresses in shared algorithms. The core exposes capability queries, resource allocation and described native interfaces. Sensor, codec and accelerator capabilities depend on the selected profile.

## Startup and activation

The firmware contains menu and event hooks that execute before activation. These hooks check extension state and preserve stock behavior while inactive. Menu registration and the startup guard also require extension code. Build checks validate patch locations and branch targets; startup behavior requires a separate comparison with stock firmware.

Auto activate defaults to Off. Activation creates a startup flag on the memory card. The normal shutdown path clears it after resources and tasks meet their completion conditions. A flag left over at the next boot prevents automatic loading. This card file is separate from internal firmware Flash. Existing card settings remain effective after a firmware update.

## Module entry and messages

A service module receives `FxModuleServices *`. Check its `bytes` field before treating it as `FxServiceModule`. The `service_abi` and capability bits identify available features. `FxSdkClient` retains sequence and pending-request state: submit a request, return from the module entry, and collect the reply on a subsequent invocation after the worker has processed it.

Requests and replies are copied through `FxServiceExchange`. Message bodies are limited to `FX_SERVICE_BYTES`, currently 768 bytes. Larger data belongs in buffers with explicit capacity and ownership, accessed through chunked services. Do not retain pointers to module stack storage across calls. An `FX_OK` return may indicate successful submission; inspect the matching reply for the operation's result.

## Lifecycle

Module kinds are APP, EXTENSION and SERVICE. Page visibility and execution state are managed separately. Closing a page does not destroy resident code. Foreground changes use events and focus epochs. Disabling a module for the next boot does not unload the current instance.

Owner and generation identify an instance. Resources, binding leases, callbacks and hooks belong to that owner. When execution ends, code or data still referenced by stock firmware may remain retained until references are removed, native access has demonstrably stopped, or the camera restarts. Reclamation requires both unreachable callbacks and completed native task/device access.

Handle `FX_BUSY`, `FX_EMPTY`, missing capabilities and capacity limits explicitly. Retain the current state when retrying and return control rather than waiting in a loop inside an entry call. Scheduling and wait services yield execution time. Total budgets, per-module image limits and available RAM remain finite.

## UI and display

Fenestra owns core management pages; WonderLantern hosts module pages. Modules submit `FxUiControl` objects of kind PAGE, ACTION, SWITCH, CHOICE, DIALOG, EDIT or DOCUMENT, using stable module-local control and parent identifiers.

Input arrives through a module event queue. Modules do not register arbitrary function pointers directly on stock touch objects. Control revision and focus epoch distinguish delayed input for obsolete pages or controls. A successful UI submission means the native compositor accepted the update, not that the panel has already scanned it out.

Graphics services use named bindings such as `camera.display.scene`. The demo module owns its character field, animation timing and title reveal; the core provides general drawing submission.

## Native calls, hooks and memory

Bindings describe kind, ABI, version, address and expected firmware bytes. Calls, code patches, MMU mappings and device access have distinct context requirements. A readable address is not necessarily callable. Respect task versus interrupt context, physical versus virtual addresses, cache coherence and outstanding DMA access.

Dynamic hook pages and resource indexes reduce fixed-table limits. They do not provide unlimited concurrent extensions or repeated installations. Published native references must retain their backing resources. Hot replacement requires stable indirection and a reliable reference-revocation protocol.

## Panic and debugging

Panic records the first fault, stops further module execution and enters the applicable recovery path. It does not isolate every possible stock-firmware failure or reverse arbitrary hardware effects. Native calls and MMIO operations retain their hardware-specific requirements.

USB Debug exposes explicit memory access, execution and shell operations. Normal Fenestra operation does not schedule internal firmware Flash writes. This does not impose hardware write protection on commands deliberately issued through the debugger.
