# Native resident extensions

Native execution is an optional `.wlm` entry ABI for extensions that own RTOS
tasks, callbacks, hooks or device protocols. Execution ABI and purpose are
independent: managed/native selects execution; APP/EXTENSION/SERVICE selects
behaviour. Both use card discovery, module enable settings and WonderLantern
pages. There is no additional module catalogue.

## Entry and publication

```c
int fenestra_native_entry(const FxNativeBootstrap *api, FxNativeExports *exports);
```

The Loader calls the entry **once, after Activate**, on its original RTOS task
and mappings. Initialization must return; create an owned RTOS task for a
resident loop. Native initialization does not get a separate stack, EL0 view,
interpreter, or per-call service trampoline. It must obey the original task's
calling convention and preserve its context. Keep the default general-register
build unless the actual native task/callback context preserves FP/SIMD.

The entry receives ABI 1's 128-byte `FxNativeBootstrap` and a 32-byte
`FxNativeExports`. Check `abi` and `bytes` before use. Fill a valid purpose,
optional `context` and `notify`; flags are zero. Return zero on success.
Code, exports and bootstrap storage remain resident, including failed
initialization that may already have published a pointer.

The Loader validates the v4 package and target, prepares a private heap image,
applies internal relocations, then publishes original-view RX/RO/RW permissions
and synchronizes executable caches. It rejects W+X segments. Allocation or
mapping failure does not permit jumping into partially published code.

## Bootstrap services

| Member | Contract |
| --- | --- |
| `resolve` | Resolve a named profile or owner-declared binding with its address, kind, version and call ABI. |
| `request` | Optional synchronous adapter for ordinary copied services, UI and binding declaration. The adapter serializes requests and assigns sequence/generation. |
| `fault` | Record the first error and 64-bit module detail; request Panic notification. |
| `publish` / `unpublish` | Publish a named/versioned native provider or stop new calls and drain existing calls. |
| `find_provider` | Match a provider's version interval and required feature bits. |
| `stream` | Resolve a registered shared-stream token and its owner/clock information. |

Direct native calls do not go through the copied service adapter. Resolve a
profile binding and call its real ABI in the appropriate context. A missing
profile binding can be declared through the existing binding-declaration
service when the module has independently established its preimage and ABI;
the core need not gain a feature-specific opcode.

The optional request adapter returns BUSY on concurrent use. Never call a
blocking original function or request adapter from an IRQ merely because its
address is available. Provider handlers execute on the caller's real RTOS
worker; concurrent callers are possible. The registry holds no lock across
module code. Native task scheduling, reentrancy and interrupt-safe handoff are
module responsibilities.

## Hooks and lifetime

Native code may install its own callback, inline hook, interrupt handler or
device adapter. `fenestra_native_hook.h` supplies an optional atomic pointer
hook with compare/exchange installation and restoration. It has no core slot
pool and does not implement instruction relocation, hook chaining, callback
ABI adaptation or cache maintenance for patched instructions.

Keep code, callback context, stacks and DMA buffers alive while any original
or module caller can reference them. Restoring a pointer does not prove that
another caller did not cache the previous pointer. Closing a page or disabling
a loaded module is not unloading; disable takes effect on the next boot.

`notify(context, SHUTDOWN/PANIC)` runs on the Loader task. BUSY requests another
poll; OK acknowledges module-owned quiescence. Only then can ordinary resource
cleanup finish. The core never force-kills a task holding original locks and
does not free native images on acknowledgment. A later Panic can follow an
acknowledged shutdown. Report faults with `fault`; an arbitrary native CPU
exception is not converted into a managed-module exception boundary.

Native code shares the stock firmware fault domain. The module owns its MMU,
cache/DMA, device reset and synchronization effects. Avoiding internal Flash
writes is an implementation and review obligation; this execution category
cannot enforce that promise against arbitrary privileged code.

## Build and use

```sh
python fenestra.py module examples/native-resident/module.c --native --id native_counter --target zr-c110 --output-dir build/native-zr
python fenestra.py module examples/native-resident/module.c --native --id native_counter --target z9-c532 --output-dir build/native-z9
```

The example publishes `example.counter` and builds a WonderLantern page. It
uses no device-specific hooks. Copy the selected target's `.wlm` into
`FENESTRA/` on the card. Native v4 packages need a core supporting this ABI;
older cores reject them. See [module format](module-format.md) for limits and
[module services](module-services.md) for ordinary/native interoperability.

`host/export_sdk.py --output <new-directory>` exports standalone public SDK
sources. External projects, additional C translation units, include directories
and static `.a` libraries are supported. There is no runtime shared-library
linker, TLS loader or C++ runtime. `--debug` keeps a local ELF for
`host/symbolize_module.py`; an ELF/debug-symbol file is not installed on camera.
