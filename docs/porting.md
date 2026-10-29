# Platform porting guide

## Target definition and prerequisites

A port targets a camera model, exact firmware version and original application identity. ZR and Z9 share the core, but have separately validated startup events, menu namespaces, function ABIs, MMU/task interfaces and update containers. A shared processor or similar firmware version is not evidence of identical bindings.

Prepare the original update package and application component, their hashes, instruction-analysis tools and a target test environment. Keep firmware, disassembly, device dumps, keys and vendor tables in local input directories. Profiles contain the required addresses, offsets, lengths, digests, ABI descriptions and structure semantics. Builds extract required original instructions from user-provided firmware.

## 1. Define identities and container boundaries

Add a profile to `host/platform_profiles.py` with target ID, model, version, application size, SHA-256, module identity and image base. Add the outer-package identity and codec integration to `host/repack.py`.

Distinguish the encrypted update package, decoded body and application component. Preserve other components and directory layout. If the outer format is not yet implemented, provide an application-only build path and label its output accordingly.

Relocate and describe every required native binding for the new version. Changing an old profile's expected hash does not validate its addresses or ABI against another firmware.

## 2. Identify the minimum installation hooks

Locate stock startup-complete, normal-shutdown and card-mount events, native page/text lookups, navigation and input dispatch. Check touch, directional controls/joystick and locale-dependent layouts.

For each hook, record its virtual address, containing image, original instruction length and hash, register/stack convention, call versus tail-branch behavior, continuation address, reentrancy and task context. A mid-function window has a different contract from a function entry; for example, the common UI dispatch gate cannot treat x0 as an ordinary page argument.

Choose validated executable space or an explicit mapping strategy. Existing profiles use reserved RX space in the original image. Validate the original contents and available link size on the new target independently.

## 3. Generate displaced instructions

`host/displaced.py` reads required instructions from a validated original application and generates local `.inc` files. Distributed `.S` sources contain the include references and project-owned gate code.

PC-relative ADR/ADRP, literal loads, conditional branches and relative calls require relocation or a different hook location. `host/c110_capture_trampolines.py` only handles its explicitly checked instruction forms; it is not a general AArch64 relocator. Reject unsupported instructions at build time.

Include generated files in compiler dependencies and build identity. Independently compare linked trampolines with their original windows and verify continuation targets.

## 4. Implement required services

Create `src/platform/<profile>/` and reuse common `src/native_*.c` adapters where possible.

| Service | Required checks |
| --- | --- |
| Memory and cache | Allocation/free ABI, page size, execute permissions, cache/TLB synchronization |
| Tasks and waits | Creation, stacks, scheduling, timeout units, wake semantics, exit status |
| Card files | Path encoding, open flags, seek64, actual transfer counts, errors, close/flush |
| UI/input | Page namespace, object lifetime, touch hit-testing/routing, focus and revisions |
| Display | Surfaces, coordinates, synchronization, lock order, submission completion |
| PTP | Stock operation lookup, sessions, DeviceInfo extensions, cancellation/disconnect |
| Linux/RTOS bridge | Channel availability, pointer address spaces, privileges, request ownership |
| Panic/shutdown | First-fault retention, module execution stop, normal shutdown hook, flag cleanup |

Report unsupported services as unavailable in capability queries. A no-op returning success is not a valid adapter for missing functionality.

## 5. Register named bindings

Use capability names such as `rtos.memory.allocate`, `camera.card.read` and `camera.display.scene`. Specify version, kind, ABI, context constraints and length. Existing function bindings use explicit conventions such as `aapcs64-u64x8`. Functions with structure parameters, different return conventions or floating-point arguments need suitable adapters.

Modules request names; profiles supply addresses and expected identities. Represent sensor formats and filesystem differences with general descriptors or distinct capability versions rather than extensive model-specific branches in modules.

## 6. Preserve owner and resource lifetimes

Associate every callback, hook, task, mapping and buffer with its owner. Determine whether stock code retains pointers, invokes callbacks asynchronously and stops access before reclamation. Retain resources until reboot if access cannot be proven to have stopped.

Successful address translation does not establish DMA readiness. Pixel leases and device buffers require checks for producer completion, consumer references, stride, packing, dimensions, cache attributes and invalidation timing. Capture capability must be demonstrated by actual acquisition, not container metadata or a synthetic image.

## 7. Integrate build and independent validation

The common sequence is `profile → original identity check → binding generation → compilation → independent validation → container reconstruction → ciphertext patch`. Extend CLI target choices and builder dispatch without depending on historical development directories.

Independent checks should cover:

1. Complete original application identity and every patch window's address and digest.
2. Link-region bounds and each branch's actual ELF symbol target.
3. Byte-for-byte reconstruction of the original application when declared patches are reversed, rejecting undeclared changes.
4. Complete compiler dependency identity and consistent embedded identity copies.
5. Stock page slots, empty slots and special navigation values, with no new-page conflicts.
6. Container length, directory, CRC/digest rules and unchanged non-application components.

## 8. Emulator and hardware testing

QEMU is not bundled. Use an emulator matching the target firmware and keep generated images, recordings and snapshots in local build directories. Implement interfaces against native firmware APIs; emulator-only MMIO is not a substitute. Record missing device coverage, including touch, and exercise available input paths.

Test in order: stock baseline boot; inactive cold boot and normal shutdown; manual activation; module discovery/management; touch and joystick navigation; USB enable/disable and disconnect; faults and abnormal-shutdown guard; restart recovery; memory pressure and concurrent modules.

Hardware tests must identify the exact build and begin with limited observations. Sensor modes, effective RAW bit depth, precapture buffers and decoded output files require separate feature testing. Distinguish static analysis, instruction-level emulation, full-system QEMU and physical-device results.

## Port deliverables

Provide the profile, project-owned adapters, API differences, build commands and compatibility information. Supply original-package hashes and explicit input formats for local patch generation. Exclude vendor disassembly/tables, keys, card contents and identifying device logs. Run shared regressions for existing profiles as well as the new target.
