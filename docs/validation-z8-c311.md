# Z8 C 3.11 platform port validation record (v0 headless)

> Corresponds to the `z8-c311` platform contribution; validation date 2026-10-31.
> Follows the `validation.md` conventions; every conclusion states its verification
> boundary explicitly. **All results are emulation-boundary validation; no hardware
> testing has been performed.**

## Verified items (evidence chain)

### 1. Container codec (`host/z8_update_codec.py`, clean-room)
- Unit tests `tests/test_z8_container.py` 5/5 green (requires local original
  package and K8 material via `Z8_FW`/`Z8_K8`; skips otherwise):
  - decode identity (eg2070 application SHA-256 `f85e0138…`, directory, per-module CRC)
  - **round trip bit-identical to the original package** (SHA-256 equal)
  - single-byte rebuild passes the full structure (module CRC + package CRC + SHA1[K8])
  - wrong length / wrong K8 rejected; replacement size and CRC enforced
- Independent cross-check with a separate local unpack verifier (V1–V8, 15/15),
  built on two independent key derivations (multi-image table extraction vs
  known-plaintext recovery from inside the package itself).

### 2. Platform profile (`host/platform_profiles.py:z8-c311`)
- `load()`: identity SHA, page namespace (`first=0x4BFFD20, stop=0x68b,
  kinds{2:1360,0:3,3:131,5:180}` with hole checks) and partial FCB ceiling
  coverage (`cmp x21,#0x1e; b.lo` at 0x408BA124).
- Conditional ABI is **explicitly pending** (`require_condition=False`
  configuration records the pending state; the UI condition hook is not enabled).
- `generate()`: 14 function bindings (8 byte-verified functions + 6 built-ins)
  and the page-roots table (0xA032B50C8, a 56-byte table of seven group roots).

### 3. UI dispatch sites (`emulation/native_ui_dispatch_sites.py:z8-c311`)
- `verify_original` pins the ten call sites across the five UI main loops
  with structural checks recomputed from the original image bytes on every
  core build: per-loop edge calls (BL event_group / event_wait), the
  `mov w2,wzr` timeout argument, poll BLs, argument setup (ADD x0,SP with
  zero-store-only gap validation), the current_task 8-byte hash (value
  shared with Z9/ZR), the wake triple (`orr w1,wzr,#0x200000`), and the
  setter 8-byte hash with its B→native and ret tail.

### 4. Startup evidence (equivalent of the Z9 C5.32 event table check)
- System event table at `0x43F907F8` (64×88B entries `{u32 id; u32 pad; u64
  name_ptr}`): id=7 `eSyscCommandId_BSShutdown`, id=19
  `…AppFirstStartComplete`, id=23 `…CardRemove`, id=24
  `…CardMountComplete` — names compared byte
  for byte on every core build. **The four lifecycle event ids match the Z9
  values** (the full 64-entry table was enumerated offline; slots and ids
  agree 64/64).

### 4a. System-event dispatcher and lifecycle gate (sysctl)
- Dispatcher `FUN_40d60ec8` at `0x40d60ec8`: reads the event id at
  `message+0x20` (`ldrsw` at entry+0x18), validates it against the pinned
  table (`id < 0x50` and entry-id equality), walks the 16-slot subscriber
  list at entry+0x10 via the registry at `0x43f90688`
  (`eSyscController_Device/Card/DefaultDevice/Lens/CyclicTimer`; events 23
  and 24 both reach `DefaultDevice` `0x40d62b04`), and takes the mutex
  `0xa03417b28` when `sync == 0`. All five pump call sites
  (`0x40d60eb8/0x40d61024/0x40d61080/0x40d611a8/0x40d68fb0`) funnel through
  this single entry, so the gate covers every dispatch path.
- The 16-byte entry window (four stack-push STPs — the same vendor prologue
  as the Zf reference window, identical prefix sha256) is pinned in
  `host/platform_profiles.py:z8-c311.sysctl` (prefix `90f0a190…`, span 268B
  `cf6a5129…`) and re-verified on every core build;
  `verify_platform_build.py` re-checks the window before accepting the hook.
- The gate `fx_z8_system_dispatch` (`src/platform/z8_c311/menu.c`) is
  installed with a plain branch at the dispatcher entry; the displaced
  window is replayed verbatim by the generated `system_trampoline.S` (the
  original's saved-LR slot returns into the gate, which restores the true
  caller's LR). Z8 lifecycle ids are 7/19/23/24 — the Zf reference numbering
  (8/20/24/25) does not transfer. Events 7/23 run the startup stop handshake
  before the original handler, 19 installs the platform state, 24 requests
  an inspect; all four record into the one-word lifecycle inbox
  (`lifecycle.c`; the record body is inlined into the hooks at -O2).
- RE provenance (frozen for re-verification): the dispatcher entry
  disassembles as `stp x25,x24,[sp,#-64]!; stp x23,x22,[sp,#16]; stp
  x21,x20,[sp,#32]; stp x19,x30,[sp,#48]` (bytes `f963bca9 f75b01a9
  f55302a9 f37b03a9`), then `mov x19,x0; mov w20,w1; ldrsw x8,[x19,#0x20]`
  at entry+0x18 (word `b9802268`, little-endian memory order `682280b9`); the event table's only data reference in the
  image is the load at `0x40d60f04` inside this function; five call sites
  reach it (`0x40d60eb8/0x40d61024/0x40d61080/0x40d611a8/0x40d68fb0`); the
  subscriber registry at `0x43f90688` names
  `eSyscController_Device/Card/DefaultDevice/Lens/CyclicTimer`. The
  previously assumed hook site `0x40FAC944` is a registered GUI callback
  (its address is passed at `0x40fac91c`), disproved dynamically: boot
  events 19/24 are recorded through `0x40d60ec8`, which would be impossible
  were `0x40FAC944` the convergence point. The 64/64 table enumeration is
  reproduced offline by iterating `0x43F907F8 + i*88` `{u32 id; u32 pad;
  u64 name_ptr}` over the pinned image. (The "identical prefix sha256 to
  the Zf window" remark compares against the Zf branch text and was not
  re-verified in this tree.)
- Emulation evidence (z8-qemu, full-lab machine with the template card;
  probe artifact `probe-r2-e23`): a real boot records `STARTED|CARD`
  (word `0x303`) through the installed gate, stable across the observation
  window with live event-34 dispatch traffic sampled at the gate entry. A
  forced CardRemove (23) re-entry through the pinned dispatcher entry with
  a captured real message moves the word `0x303→0x301` (CARD cleared,
  CARD_PENDING lingers). With a magic-validated module worker (`0x465a3957`
  at the allocation, phase ACTIVE=2 before injection), the worker REMAINED
  ACTIVE after injection and no `fx_z8_worker_shutdown` call was observed
  through the end of the run: **the pre-authored stop handshake does not
  propagate under forced delivery, and worker-stop on card removal is
  unproven** (an earlier revision of this section claimed a worker shutdown
  flag observation; that evidence read a wrong pointer and is withdrawn).
- Forced event-7 re-entry (probe artifact `probe-r2-e7`): the word moves
  `0x303→0x705` — exactly `STOP|STOP_PENDING` set with `CARD` cleared —
  and STOP is terminal: a direct `fx_z8_after_startup_complete` call
  records nothing afterwards, and the word stays frozen under live traffic.
- Synthetic event-99 through the same path leaves the word unchanged
  (probe artifact `probe-r2-e99`); the observation window itself (live
  event-34 traffic, word stable at `0x303`) is the natural no-op control.
- The full patched-image regression stayed green in three consecutive
  gate-enabled runs (reg-t13a/b/c: FXBOOT1 record valid, DCIM/NIKON/Z_8
  created, no unmodeled MMIO outside the ISP boundary block).
- Natural delivery attempt (probe artifact `probe-r2-eject`): a runtime
  `cfexpress-card-present` QMP toggle (the modeled detect line,
  `0x17a00044` bit 1) produced no dispatch and no word change within 25 s.
  No direct reference to that register was found anywhere in the image:
  Ghidra cross-references 0 (analyzed functions only), a full-image
  byte-level ADRP+LDR pattern scan 0, literal-constant scan 0 — computed
  or indirect addressing is not covered by the scans; the runtime toggle
  covers the dynamic path. Natural card events therefore need a model
  extension (the Z 6III qemu documents the equivalent slot with a 50 ms
  firmware poll; the Z8's detect path is presumably MCU-linked).
- Boundary: the event 7/23 evidence is forced delivery under the emulator
  (gdbstub re-entry in an arbitrary stopped CPU context with sync=1, which
  skips the dispatcher mutex); the natural dispatch-task context and the
  sync=0 mutex path are not exercised, worker-stop propagation is an open
  item, and the worker INTERRUPTED continuation path remains unexercised.

### 5. Core build (`host/build_z8_core.py` + `emulation/z8_core.ld`)
- 74 source files compiled (LLVM 19.1.7, the same strict flag set as Z9,
  including -Werror -Wconversion).
- Linked at `0x401A0000` (original RX padding, asserted all-zero over 384KB
  before the build); core size 41,912 bytes in the earlier UI-hooks-only
  configuration, 177,280 bytes in the port configuration and 177,312 bytes
  in the sysctl-gate configuration (eleven hooks).
- In-build bit-exact reconstruction verification: clearing the core region and
  restoring every hook reproduces the original byte for byte.
- The module CRC16 trailer is recomputed explicitly (`verify_platform_build`
  z8 branch checks it).
- `verify_platform_build.py --target z8-c311`: reconstruction + branch/ELF
  symbol matching verification passes.

### 6. Emulation smoke (z8-qemu, QEMU 11.1.1)
- **Stock baseline: 932/932 checks.**
- **Fenestra v0 payload: 942/942 checks** (patched MAIN with the recomputed
  trailer CRC); includes full boot, menu navigation (ok/down/back ×2) and the
  half-press path. (Measured on the UI-hooks-only v0 configuration; the later
  sysctl-gate configuration is covered by the §4a patched-image regression
  triad.)
- **v0 behavioural-equivalence argument:** with `fx_native_ui_dispatch_domain()
  == 0` (platform state uninitialised) all ten UI hooks pass through to the
  original poll/wait on every path; the 942 green checks are the behavioural
  evidence for that argument.

### 7. Two-stage patch chain
- `repack.py` z8 full path: decode (user K8 material) → rebuild (full container
  structure checks) → make_patch.
- `patcher.py` **keyless application** is byte-identical to the generator's own
  artifact; patch size 45,910 bytes (ciphertext-range replacement).
- Negative control: wrong original-package SHA is rejected (the payload is not
  read before the identity check).

### 8. hello module
- `hello-z8.wlm`: FENESTRA v2 header, `module_identity` = the eg2070 digest
  `f85e0138…` (Z9 convention), body SHA-256 verified.

### 9. Full produce() and distribution chain
- `fenestra.py make-patch --target z8-c311 --release custom`: build +
  independent verification + re-encode + `.fxpatch`, full chain green.
- **End-to-end distribution chain**: `.fxpatch` → `patcher.py` keyless
  application → container decode (full structure checks) → MAIN extraction →
  **z8-qemu boot 942/942 checks**.
- `verify_platform_build`: **FULL-PASS** (identity over the complete
  compilation and input set recorded in `verification.json`, with
  linked-copy verification; native namespace explicitly pending —
  navigate/apply not pinned).
- Debug identity linked copy at 0x401AA3C8.

### 10. hello module load round trip (emulation, after the port fixes)
- Full chain, three consecutive runs (123/123 probe checks each in the
  independent re-verification of this branch): install → activate → arm (the
  camera itself writes the FXBOOT1 flag to the exFAT card and creates
  DCIM/NIKON/Z_8) → discovery (FW_LIST callback enumeration) → staging with
  digest check → session load → activate mapped → `user_call` into the hello
  module entry at EL0; invoke/heartbeat/exec_live keep running. Sustained
  operation was demonstrated for the full 420 s window in one run; in the
  other two, the sustained segment was truncated after ~25 s by a read of
  the unmodeled ISP register 0x1508001c (an emulator model boundary, not
  payload behaviour — the chain and all flag counts were complete and
  identical).
- Card contract: exFAT with `FENESTRA/startup-z8.0` (FXCONF1: identity
  0x00090538, generation 1, desired=3) and `FENESTRA/hello-z8.wlm`; the
  `boot-z8.flag` file must be absent (its presence selects the abnormal-
  shutdown path and suppresses activation).
- Filesystem error semantics observed: `0x80001003` = drive not registered,
  `0x80000010` = not found, `0x80000030` = directory end sentinel.

## 4b. Native menu bindings (T12)

Pinned against the original C311 image (all addresses RE-verified by
decompiled semantics plus call-graph evidence; method notes in the local
T12 engineering record):

| Binding | Address | Evidence |
| --- | --- | --- |
| `camera.menu.page.get` | `0x403db694` | `root[group] + (id & 0xffff) * 0x60` over the page-roots table `0xa032b50c8`; 325 call sites; 16B entry window sha256 `dec337c6...23a710c` |
| `camera.menu.text.get` | `0x4032e130` | `(language, id, &utf16, &bytes)` bank lookup matching the reimplemented stock path; sole reader of the bank roots; window sha256 `5615d42d...7fc9564` |
| `camera.menu.navigate` | `0x4039e810` | the 2973-site open-request funnel `(page, context)`; SETUP (638) verified flowing through it; 16B window (str/stp/mov pair, position-independent) sha256 `d62ce251...139784b7`, replayed by the generated `navigate_trampoline.S` |
| `camera.menu.open` | `0x403ad6d8` | the `(group, page)` open primitive (26 callers); the ported `0x403adbac` call was a Z9 reference landing mid-function on C311 and is replaced |
| Text bank roots | `0xa0281cd5c` count, `0xa02f2ac38` pool, `0xa02f2ac40` fonts, `0xa02f2ac48` offsets, `0xa02f2ac50` attributes | registered by the firmware text init (`FUN_403edf3c` via `FUN_4032e05c/074/08c/0a4/104`); cluster order mirrors Z9 |
| Font request sites | lock `0x402e5550` / unlock `0x402e5564` (mutex `0xa02e8c658`), 13 enter sites `0x402edc90..0x402f0248`, measure `0x4088bdac`, set_layout `0x4088c620`, line probe `0x402eeb28` | byte-verified pattern at all 13 sites (lock BL; orr w0,#2; register; event-op; orr w0,#2; wait; unlock BL at +28, the Z9 geometry); structural check in `emulation/native_font_sites.py:z8-c311` runs at build and verify time |
| SETUP navigation table | `0x43bf0680` (12 records, next page id `0x68c`) | C311 uses one table per page family; the ported `0x43014bb8` (Z9 single 18-event table) reads as high-entropy data here |

Installation: page.get/text.get/navigate are plain-branch full replacements
at their entries (stock semantics reimplemented; navigate keeps the
displaced window); the font sites are BL hooks around the original
lock/unlock calls. While the platform state is uninitialised every hook is
pure passthrough. Baseline equivalence: the patched image passes the full
RE0 regression (FXBOOT1 + DCIM/NIKON/Z_8 + hello module present, no
unmodeled MMIO; artifact `menu-reg-1` in the local evidence store).

Boundaries (explicit): the render-profile cluster (ui_layout/ui_text/
visibility/draw profiles, ~20 Z9 reference slots, all confirmed NOT
function entries on C311), the font fallback face (open_font/select_font/
fallback data) and display restore remain unpinned; the Fenestra page
construction and draw paths stay dormant until they are pinned, so
menu-driven navigation and activation are not yet exercised end to end.

## 4c. Render-profile cluster (T12, second round)

All pinned against the original C311 image by decompiled semantics; method
notes in the local T12 engineering record. Layout/visibility/draw profiles
in `src/platform/z8_c311/menu.c` now carry only pinned values.

| Slot | Address | Evidence |
| --- | --- | --- |
| `count_rows(page,sel)` | `0x403afe64` | participation count loop over `FUN_403afba0`; the `0x50002` selector is used verbatim on C311 (`FUN_403afcac`) |
| `highlight(page,i,0)` | `0x403aff78` | deselect/deselect pair in the selection-move path `FUN_403b07a4` |
| `reset_check(page,id,0)` | `0x403b1a5c` | checkmark bitmap `0xa032acac8` write + row check-display toggle; 486 callers |
| `repair_selection(page)` | `0x403db750` | view-chain refresh (weakest pin; navigation-probe arbitrated) |
| `place_rows(page)` | `0x403b01f0` | staged visible-row list rebuild (`0xa032aa718` block) |
| `resize_canvas(page)` | `0x403aec24` | canvas metrics publish (`0xa032b1434-44c`) |
| `prepare_viewport(page)` | `0x403b035c` | extent measure + min/max + show |
| `scrollbar(page)` | `0x403aed8c` | scrollbar object `0x4ba3` show/hide by content height |
| `row_lookup(id,rows)` | `0x40325b60` | children of owner+0xC8 compared by id |
| `set_active(obj,flag)` | `0x403f6228` | `FUN_40325f18`: +0x29 visible flag, +0x3c dirty, parent propagation |
| `row_draw` | `0x4031f9b8` | stored at widget+0x48 by the initializer `FUN_4031f8f0` |
| `child_lookup` | platform thunk | `FUN_40325b60` takes (id,owner); the thunk adapts the shared (row,id) call |
| value/arrow child ids | `0x4ba5`/`0x4ba6` | composite builder `FUN_403a8898` registration order (Z9-analogous) |
| row bytes | `0x128` | widget fields written to +0x122 by the initializer |
| draw `dirty` | `0x403f1940` | colour fields + dirty-bit invalidation |
| draw `dispatch` | `0x40325a80` | container dispatcher: iterates children, calls widget+0x48, clears +0x3c |
| draw `device` | `0x403f0db8` | context-by-id getter |
| draw `empty` | `0x40c7bea4` | zero-rect test |
| GUI mutex | lock `0x40d2bfb4` / unlock `0x40d2bf60` / object `0xa032b8044` | primitives shared by the GUI wrappers; object chained via movz/movk at the refresh sites |

Structural bonuses for T15: widget method slots (+0x48 draw, +0x58, +0x90),
teardown `FUN_4031f51c`, participation test `FUN_403afba0`, index/object
resolvers (`FUN_403afa34`, `FUN_403b01a0`), checked-state query
`FUN_403afaec` and the checkmark bitmaps (`0xa032acac8` write,
`0xa032aed40` read).

Boundaries (explicit): the rectangle clip-setup primitive, the modal body
id, the text-editor slots (`value_lookup`/`set_disabled`/`set_text`/
`release_text`/`value_bytes`/`set_alignment`/`measure`/`bounds`), the font
fallback face (`open_font`/`select_font`/fallback data) and display restore
remain unpinned. With `rectangle` null the shared `draw_pair` rejects the
profile and the repaint degrades to `repaint=0`, so menu navigation and
activation stay unexercised end to end; the reader/title/editor profiles
retain Z9 references as documented residuals. Baseline equivalence holds:
the patched image passes the full RE0 regression twice (`menu-reg-1`,
`menu-reg-2`).

## Unverified / not enabled (explicit boundaries)

| Item | State | Notes |
| --- | --- | --- |
| UI condition ABI window | pending | the 168B dispatch window is not pinned; the configuration records the pending state and the hook stays disabled |
| worker INTERRUPTED continuation | not exercised | the EL0 interrupt/resume path has no interrupt-injection evidence yet (planned follow-up) |
| Native menu registration (menu reachability) | bindings + render profiles enabled, navigation E2E pending | menu trio, font sites, layout/visibility/draw bindings pinned and hooked (sections 4b/4c); the residual set (draw rectangle/modal id, text-editor slots, fallback face, display restore, the Z9 event-id quintuple in the menu profiles) keeps construction dormant, so SETUP navigation is not yet exercised |
| hello `.wlm` load round trip | verified in emulation | full install→activate→load→user_call chain, 3/3 runs; menu-driven activation (module management entry) still pending |
| debug identity / native namespace pipeline | not wired | the shared tail of verify_platform_build is not yet z8-integrated |
| PTP / Linux bridge / USB debug / fonts / display | capability unavailable | excluded from the v0 source list; stubs return FX_CAPABILITY |
| task.create/resume/delay addresses | Z9 reference values | prologue probes exist; create/resume are not exercised by the validated configuration; the delay binding runs on the loader's yield path (all runs) and on the sysctl stop-handshake wait loop (forced-delivery context) |
| GUI mutex trio (0xa032dcb98 / 0x40d30004 / 0x40d2ffb0) | Z9 reference values | exercised on the load/refresh path (`refresh_core`) in the validated chain and tolerated across all runs; the Z8 mutex is not pinned |
| FCB error codes / card_capacity BL chain | partial | loop signature verified; error paths and the call chain not pinned |
| Hardware | **entirely unverified** | no device flashing; every conclusion in this file is emulation-boundary |

Beyond the rows above, the platform sources retain further Z9 reference
values (page-family helpers, editor/font/text tables, draw and dialog
addresses) that the validated configuration does not exercise; none of them
are pinned on Z8 and none are marked in-source.

## Reproduction

```sh
# Toolchain: LLVM 19 (clang, ld.lld, llvm-objcopy, llvm-nm on PATH)
export Z8_FW=/path/to/Z_8_0311.bin        # local original package
export Z8_K8=/path/to/sig.json            # local K8 material
export Z8_MAIN=/path/to/eg2070_0311b0.bi  # local original MAIN component (81,807,711 bytes)
python3 -B -m unittest tests.test_z8_container -v
python3 host/build_z8_core.py --firmware "$Z8_MAIN" --output-dir build/<new-dir>
python3 host/verify_platform_build.py --target z8-c311 --firmware "$Z8_MAIN" \
    --build build/<dir> --output build/<dir>/verification.json
```

Emulation requires a separate Z8 emulator matching the target firmware (z8-qemu,
QEMU 11.1.1); generated images, recordings and snapshots stay in local build
directories.
