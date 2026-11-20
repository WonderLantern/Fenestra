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

## 3. Place resident code where the camera keeps it

An all-zero region in the original image is not free space. The first Z6III C2.01 build linked its core into a large zero run at `0x40910000`. The region was empty, mapped and executable, and the build ran in an emulator. On a camera it does not survive startup: that build was reported to malfunction after installation.

### How these bodies load MAIN

MAIN text first runs in place, from the address range where the loader put the package. Later in startup the firmware moves text into DRAM in windows. For each window it copies the window's content, and nothing after the last content byte, then remaps the whole window to the copy.

| Part of a window | In the image | At runtime |
| --- | --- | --- |
| Content, up to the copy end | Code | The DRAM copy of that code |
| Padding, from the copy end to the remap end | Zero | DRAM the copy never wrote; unrelated data on a camera |
| Text outside every window | Code or zero | The package bytes, in place, for the whole run |

Code placed in the second row is present early in boot and then disappears. A hook that branches there executes whatever the DRAM holds.

The copy routine builds its bounds from immediates, so they are readable in the image:

| Build | Copy routine | First window copied to | Remapped to | Second window copied to | Remapped to |
| --- | --- | --- | --- | --- | --- |
| Z6III C2.01 | `0x41189B00` | `0x4090EBE8` | `0x40C00000` | `0x416692F8` | `0x41800000` |
| Zf C3.01 | `0x41147CE4` | `0x408D3EF0` | `0x40C00000` | `0x412667A4` | `0x41600000` |
| Z9 C5.32 | `0x41189C04` | `0x408C2570` | `0x40C00000` | `0x412A817C` | `0x41600000` |
| ZR C1.11 | `0x41383B34` | `0x40AA5C78` | `0x40E00000` | `0x4153BC38` | `0x41800000` |
| Z8 C3.11 | `0x41184CF0` | `0x408BF8F0` | `0x40C00000` | `0x412A31F4` | `0x41600000` |

The first window starts at `0x40200000` on all five. A smaller window below it ends at `0x40195000` on Z6III and near `0x4018D128` on the others. The padding at `0x401A0000`-`0x40200000`, used by every profile, lies outside all of them. Only the Z6III C2.01 row is checked by this repository's build; the other rows are a static reading to be confirmed when those targets adopt the check. Bounds change between firmware versions.

### Requirements for a new target

1. **Trace the relocation.** Find the routine that copies text to DRAM and record each window's start, copy end and remap end in the target profile, with the routine's address, length and digest. The build decodes the bounds from the original routine and refuses a profile that disagrees. See `text_relocation` in `targets/z63-c201.json` and `host/z63_placement.py`.
2. **Place the core outside every window**, in text that is mapped executable and never remapped. The build rejects a code region that overlaps any window, including its zero padding.
3. **Confirm on a camera when you can.** A read-only page-table walk of a stock body shows which ranges are served in place and which from DRAM. Reading a stock body cannot show that a zero region is safe, because the stock bytes are zero either way.
4. **Do not treat an emulator boot as placement evidence.** An emulator that loads the whole image, or pre-fills the DRAM windows from it, hides this failure. Poison the uncopied padding if the emulator allows it.
5. **Record what is still unproven.** The profile states whether the loader has been shown to place modified padding bytes, and whether the target is qualified for physical installation. A complete update package is refused for an unqualified target unless requested explicitly.

### Guard every installed hook

Hooks are patched into original code and run on every boot, before the user activates anything. Safeguards that live in the core cannot protect against the core being absent. So no hook branches straight into the core.

Each hook branches to a small guard placed in padding inside a window's copied content, where it travels with original code. The guard compares a marker stored at the end of the code region:

- Marker present: enter the core.
- Marker absent, function-entry hook: run the displaced first instruction and continue at the next one.
- Marker absent, call-site hook: branch to the original callee.

A guard uses only `x16` and `x17`. That limits hooks to function entries and call sites, and the displaced instruction must not read the program counter; the build rejects other sites. The verifier rebuilds every guard from the original image and requires the candidate to contain exactly those bytes.

Prove the fallback: boot an image with every hook and guard installed and the code region cleared. It must match an original boot, with no entry into the code region (`host/z63_core_absent_smoke.py`). A placement or loading mistake then costs the feature, not the boot.

### Checklist

- [ ] Relocation windows traced and recorded with the routine digest.
- [ ] Code region outside every window and inside in-place executable text.
- [ ] Guard region inside copied content, empty in the original.
- [ ] Every hook is a function entry or call site and goes through a guard.
- [ ] Core-absent boot matches the original in the emulator.
- [ ] Camera page-table walk, if a body is available.
- [ ] Profile states loader and installation qualification honestly.

## 4. Generate displaced instructions

`host/displaced.py` reads required instructions from a validated original application and generates local `.inc` files. Distributed `.S` sources contain the include references and project-owned gate code.

PC-relative ADR/ADRP, literal loads, conditional branches and relative calls require relocation or a different hook location. `host/c110_capture_trampolines.py` only handles its explicitly checked instruction forms; it is not a general AArch64 relocator. Reject unsupported instructions at build time.

Include generated files in compiler dependencies and build identity. Independently compare linked trampolines with their original windows and verify continuation targets.

## 5. Implement required services

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

## 6. Register named bindings

Use capability names such as `rtos.memory.allocate`, `camera.card.read` and `camera.display.scene`. Specify version, kind, ABI, context constraints and length. Existing function bindings use explicit conventions such as `aapcs64-u64x8`. Functions with structure parameters, different return conventions or floating-point arguments need suitable adapters.

Modules request names; profiles supply addresses and expected identities. Represent sensor formats and filesystem differences with general descriptors or distinct capability versions rather than extensive model-specific branches in modules.

## 7. Preserve owner and resource lifetimes

Associate every callback, hook, task, mapping and buffer with its owner. Determine whether stock code retains pointers, invokes callbacks asynchronously and stops access before reclamation. Retain resources until reboot if access cannot be proven to have stopped.

Successful address translation does not establish DMA readiness. Pixel leases and device buffers require checks for producer completion, consumer references, stride, packing, dimensions, cache attributes and invalidation timing. Capture capability must be demonstrated by actual acquisition, not container metadata or a synthetic image.

## 8. Integrate build and independent validation

The common sequence is `profile → original identity check → binding generation → compilation → independent validation → container reconstruction → ciphertext patch`. Extend CLI target choices and builder dispatch without depending on historical development directories.

Independent checks should cover:

1. Complete original application identity and every patch window's address and digest.
2. Link-region bounds, the region's position outside every relocated text window, and each hook's guard and ELF symbol target.
3. Byte-for-byte reconstruction of the original application when declared patches are reversed, rejecting undeclared changes.
4. Complete compiler dependency identity and consistent embedded identity copies.
5. Stock page slots, empty slots and special navigation values, with no new-page conflicts.
6. Container length, directory, CRC/digest rules and unchanged non-application components.

## 9. Emulator and hardware testing

QEMU is not bundled. Use an emulator matching the target firmware and keep generated images, recordings and snapshots in local build directories. Implement interfaces against native firmware APIs; emulator-only MMIO is not a substitute. Record missing device coverage, including touch, and exercise available input paths.

Test in order: stock baseline boot; core-absent boot matching the baseline; inactive cold boot and normal shutdown; manual activation; module discovery/management; touch and joystick navigation; USB enable/disable and disconnect; faults and abnormal-shutdown guard; restart recovery; memory pressure and concurrent modules.

Hardware tests must identify the exact build and begin with limited observations. Sensor modes, effective RAW bit depth, precapture buffers and decoded output files require separate feature testing. Distinguish static analysis, instruction-level emulation, full-system QEMU and physical-device results.

## Port deliverables

Provide the profile, project-owned adapters, API differences, build commands and compatibility information. Supply original-package hashes and explicit input formats for local patch generation. Exclude vendor disassembly/tables, keys, card contents and identifying device logs. Run shared regressions for existing profiles as well as the new target.
