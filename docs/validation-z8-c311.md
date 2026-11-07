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
| SETUP navigation table | `0x43bf0680` (13 records = 12 routes + the `{0x3bb,0,0,0x68c}` terminator; stay next id `0x68c`) | C311 uses one table per page family; the ported `0x43014bb8` (Z9 single 18-event table) reads as high-entropy data here. Menu quintuple pinned from it: terminal `0x3bb`, back `0x27f` (cb `0x403b1bb0` -> family root), activation trio `0x286/0x28b/0x2ae` (151 child-table records route them to real sub-pages `0x68d-0x68f`), stay `0x68c`; cursor up/down `0x283/0x29d` and `0x284/0x29e`, view-5 pair `0x285/0x2a0`, value-notify `0x29f`, page-open route `0x27e`; conditions `0x403b1c5c` (always-false, stock-disabled) / `0x403b1c7c` (settings-gated) |

Installation: page.get/text.get/navigate are plain-branch full replacements
at their entries (stock semantics reimplemented; navigate keeps the
displaced window); the font sites are BL hooks around the original
lock/unlock calls. While the platform state is uninitialised every hook is
pure passthrough. Baseline equivalence: the patched image passes the full
RE0 regression (FXBOOT1 + DCIM/NIKON/Z_8 + hello module present, no
unmodeled MMIO; artifact `menu-reg-1` in the local evidence store).

Construction and E2E (final round, all values pinned against the C311
image): the draw profile completes from the stock pair site
`FUN_4039e848` — rectangle clip-setup `FUN_40340d64` (writes clip[4]
int16 from surface+0x9c/+0x1f4), pair dispatch `FUN_403f6388` (locks
`0xa032bbd68`, wraps raw dispatcher `FUN_403262a0`), empty/degenerate
test `FUN_40c7be74`; the compositor submit tail is
`FUN_403efed8(FUN_403efe74,3,7)` (the Z9 `0x403eb0dc/0x403eb078` pair
was a mid-function reference). `fx_z8_menu_construct` builds owned pages
from the pinned quintuple (offset-asserted layout compatibility with the
shared menu builder). The C311 SETUP page 638 is a 1-row hub page (stock
row id `0x3793`), so the construction prepends the two Fenestra entry
rows dynamically (setup.count = stock+2, template = last stock row) and
grows the text bank from the runtime roots `0xa02f2ac40/0xa02f2ac50`
(per-language attribute rows range-checked).

Emulation E2E (probe `scripts/z8_menu_probe.py`, single boot per leg,
stream-fed window, qtest-primary evidence via `fx_z8_menu_metadata` v5;
3/3 runs per phase):
- navigate (enabled image): SETUP descriptor holds the State magic; the
  owned core page carries identity `0x63c<<16` (routed id, section 4e) with
  7 rows; SETUP is rewritten to stock+2 rows with entry rows
  `0x2d1c/0x2d1d` (text ids 3204/3205).
- activate (enabled image): the original MCU key records (menu/ok/down/
  down/ok/ok) are delivered through the external frame transport with the
  guest stable and no unmodeled access, and the module activation chain
  completes — activation requested, modules > 0, worker magic
  `0x465A3957`, phase 2 (ACTIVE). Attribution caveat (review finding):
  the card template's `FX_STARTUP_AUTO` flag auto-activates the hello
  module through the startup-file service with ZERO keys (the navigate
  legs are the counterfactual: they send no keys and reach the same
  activation state), so menu-key-DRIVEN activation is NOT isolated by
  this evidence; an AUTO-off card variant is the follow-up input.
  UPDATE (attribution leg, artifact `menu-e2e-att-2`): the AUTO-off card
  variant (scripts/z8_card_auto_off.py clears FX_STARTUP_AUTO, desired
  3->2 with recomputed CRC) proves the attribution gap is real and the
  method works — pre-key state is activation 0/modules 0, and the key
  sequence produces NO activation. Menu-key-driven activation is
  additionally blocked by an input-protocol gap: vision-verified frames
  show the MENU key opens the photo-shooting tab and the four-way keys
  do not move the category tab strip (LEFT eventually exits the menu);
  reaching the SET UP tab requires a command-dial rotation record,
  which z8-qemu's z8_navigation_input BINDINGS does not carry. The dial
  record format is the follow-up input.
- negative (default image): the headless State installs (early publish)
  with construction dormant (no owned core page, no entry-row rewrite) —
  baseline equivalence, plus the full RE0 regression re-run
  (`menu-reg-4/5/6`) on every default-build revision.

Boundaries (explicit): the modal body id is not pinned (constructed pages
are non-modal; the modal draw branch rejects with FX_ARGUMENT if ever
reached), the text-editor slots, the font fallback face (the C311 font
subsystem uses a face cache with 2-arg create/bind — `FUN_40881888`/
`FUN_40880d74` — and vtable configs `0x44b82608/0x44b82748`, an ABI
change from Z9 recorded for T15) and the display profile (VG library
verified unmoved at the Z9 addresses, 24 fns; the GUI cluster shifted,
anchor map in the local evidence store) remain unpinned. The loader runs
on the UI dispatch domain tick (the task family is pinned on C311; a
loader task stays deliberately unused); no loader task exists in the
enabled image.

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
remain unpinned **(superseded for the draw slots by the final round — see
section 4b: rectangle `FUN_40340d64`, pair dispatch `FUN_403f6388`, empty
`FUN_40c7be74`, submit `FUN_403efed8`; the table rows above predate those
pins and are kept as the round's history)**. The reader/title/editor
profiles retain Z9 references as documented residuals (plus the
callbacks[2] placeholder `0x403b2748`, z9-verbatim and runtime-replaced
from the stock page before use). Baseline equivalence holds: the patched
image passes the full RE0 regression (`menu-reg-1/2`, re-run as
`menu-reg-4/5/6` on every default-build revision).

## 4d. Wrench-tab milestone: the constructed SETUP list renders (T12, third round)

Verified in emulation on 2026-11-06 (probe legs `wrench-p3-r17`, `wrench-p3-final`,
and the P4 confirmation pair `wrench-p4-v1/v2` with the control `wrench-p4-ctl`,
local evidence store). With the constructed descriptor live on the wrench-tab
list page (663) at open time, the stock open-path rebuild now completes and
the panel renders the stock rows plus the two Fenestra entry rows.

Root cause of the former black panel (fixed in the platform commit carrying
this section): the C311 open-path rebuild (`FUN_403a5b90` row loop) validates
each row's id (row word[6]) against a **global registry of registered stock
row ids** and silently aborts the whole page build on an unregistered id —
zero rows, no exception, black panel. The Z9-style `0x7ff0`-family entry-row
ids were all rejected (probe rounds `wrench-p3-r13/r14`: single entry row,
count 45, zero rows built). The registry is global, so ids borrowed from
another page's stock rows pass: the entry rows now carry `0x2d1c`/`0x2d1d`
(page 25's first-row family, verified in the offline mirror; unique within
663; duplicate ids are tolerated by the rebuild but avoided for lookup
unambiguity).

Shipped together with the fix (upstream strictness absorbed, single commit):

| Measure | Value | Notes |
| --- | --- | --- |
| Exact stock gate | page-663 descriptor pointer `0xa028ca040`, count 44, header `0x43d595c8`, rows `0x43d596d8`, events `0x43d59e68`, row text sentinels `0x280`/`0x662` | checked in `construct_ready` and again in `construct_pages` (defense in depth; Z9/Zf/Z63 pattern) |
| CAS publish guards | 663 and 638 anchors swap only while the live pointer still equals the pinned stock pointer | residual race windows documented in-source (a lost CAS leaves the construct dormant, never corrupt) |
| Pinned clone row | stock `rows[0]` (plain `0x00101040` list row) | the generic last-row clone is gone |
| Descriptor tail fidelity | descriptors are 0x50 bytes: `+0x40` record-table pointer `0x43ed1398`, `+0x48` = 8 | `State.setup_tail` copies the stock tail verbatim; the former 64-byte struct copy exposed `State.magic` at `+0x40` and broke surface/content allocation |

Milestone evidence (probe `--phase wrench`, enabled image, two fresh runs of
the SAME image plus the committed P3 confirmation):

- Image fingerprints identical across `wrench-p4-v1`, `wrench-p4-v2` and
  `wrench-p3-final` (core sha256 `1ea16836...8622f2`, debug identity
  `7e57d94a...9d0fc`, MAIN sha256 `830a7932...36ba2`); both P4 runs classify
  `outcome == milestone` with every probe check true (published, rebuilt,
  fresh frame with content, entry-row ids/text verified: `setup_entry_rows`).
- Rebuild state at freeze: page 663 `page_count` 41, content children 524/520
  (run variance; the entry rows' `0x2d1c`/`0x2d1d` widget objects are the
  first two rows), descriptor ours with count 46 (44 stock + 2 entry),
  `setup_count` 46, `row0_id` `0x2d1c`.
- Final-frame content: 348 distinct colours, black fraction 0.0396 — byte-
  stable across v1/v2/final (the freeze moment is host-load sensitive; stock
  has measured 224-394 across days, so verdicts are judged on settle frames
  and repeat runs, never on a single colour count).
- Pixel reading of the rendered panel (pure-Python PPM analysis, no vision
  model): yellow SET UP header bar (`fbd624`), left tab strip with the wrench
  tab selected (view globals report view 5), top status bar; the first list
  row reads "Fenestra" and the second "WonderLantern" (rendered by the stock
  font/text pipeline from our text bank ids 3204/3205); the stock rows below
  are pixel-identical to the control run's rows at the one-row offset
  (column-signature Jaccard 1.00), i.e. the construct added rows without
  perturbing stock rendering.
- Control (`--default-image`, `wrench-p4-ctl`): `outcome == rendered_stock`
  (394 colours, stock descriptor live on 663, `page_count` 39, 496 content
  children, construction dormant) — baseline equivalence preserved.

Caveats (explicit, in addition to the boundaries table below):

- Emulation-only. No device flashing; z8-qemu is the sole execution
  environment for every claim in this section.
- The boot-time service park is present in EVERY run, stock included, and is
  causally unrelated to the construct (the P2' investigation established
  this): cpu3 is handed to a DRAM driver module at `0x0a02ab0000` during boot
  and parks on a never-granted hardware semaphore (`0x0d151404` read via the
  service-SRAM acquire `0x082013c8`, parking PC `0x082013d4`; service-request
  mailbox `0x15595b00-ff` never sees traffic). `0x0d151404` / `0x082013c8` /
  `0xa1559b00` are recorded as **tolerance boundaries**: not modeled, not
  "fixed". The corresponding z8-qemu fixtures (`syscon-semaphore-model`,
  `syscon-semaphore-trace`, `mailbox-trace`) are default-off, forensics-only.
- The ok-key activation chain is verified end-to-end (section 4e): the
  entry-row ok pushes the routed Fenestra page through the stock open path
  and the page renders (probe artifacts `t4b-okc-fen-r2` etc.). The
  ACTIVATE row's `activate` callback attribution stays as the 4b caveat
  (the card's auto-activate flag is not isolated).
- The platform's OWN menu pages carry borrowed registered row ids
  (`0x46e8-0x46f4` language/region family, `0x4600` manager family — T3
  BORROW) and routed page ids below the kind-2 gate (`0x63c-0x63f`,
  section 4e); the module-manager/UI/apps pages keep their `0x7000`-family
  ids and stay above the gate (recorded boundary below).
- Borrowed-id ownership: `0x2d1c`/`0x2d1d` belong to page 25's stock rows.
  Page 25 and 663 were not open simultaneously in any observed flow; a
  simultaneous rebuild collision is an open observation item.

## 4e. Own-page routing: kind-2 gate bypassed by audited id renumbering (T4b)

The wrench-tab row-ok navigation carrier (T2: the selected row's select
trio `next` in the full 59-record stock row-table family) resolves its
target through the stock navigation continuation `FUN_403af53c`, which
**drops every list target `>= 0x68c` before `page.get` runs** when the
context page is a kind-2 list: `movz w8,#0x68c; csel w22,w8,wzr,eq` at
`0x403af570-74` (context kind `u16&0xfffe==2`), `cmp w22,w20` at
`0x403af654` + `b.ls 0x403af680` at `0x403af658` (shared silent-return
epilogue). Instruction-window sha256[:16]: `0x403af654` = `b2988f806fb25268`,
`0x403af658` = `cbe0381ecab8774d`. This is a pure numeric range gate — no
registry, no stock-page validation — so the former `0x7000`-family page ids
could never be pushed from wrench rows (probe `okchain-t4a-f5`: target
`0x7000` reached the continuation and the whole open chain stayed silent).

Fix (option A of the T4a evaluation): the four navigable own pages are built
under stock page ids from the audited free window inside `[0x61c,0x65d]`,
matched EXACTLY in `fx_z8_menu_page`; every other id keeps its stock()
arithmetic, and the extension NULL interval and the navigate-hook interval
stay `[FX_NATIVE_FENESTRA_PAGE, FX_NATIVE_EXTENSION_PAGE_LIMIT)` — widening
them is what crashed the t4a `s2` probe round (68 stock dialog templates
live in `[0x5e0,0x7000)` and were hijacked by the shifted interval).

| Page | Routed id | Reached from | Probe artifact |
 | --- | --- | --- | --- |
 | Fenestra core | `0x63c` | 663 entry row 0 ok | `t4b-okc-fen-r2` |
 | WonderLantern | `0x63d` | 663 entry row 1 ok | `t4b-okc-wonder-r3` |
 | Auto activate | `0x63e` | core Autostart row ok | `t4b-okc-auto-r2` |
 | Shutdown guard | `0x63f` | core Guardflag row ok | `t4b-okc-guard-r1` |

Occupancy audit for the window (four independent sources; scripts and raw
sets under `build/re-runs/t4b-audit-r2/roots-audit.json` and the T4b
report):

- Descriptor-template census: the id-indexed ROM template array
  `0x44d28110` (stride `0x50`; 663 = `0x297` at `0x44d35040`, count 44)
  carries valid templates only for ids `0..0x4c2` — `[0x4c3,0x68b]` has
  none.
- Row-table next census: walking every template's events array (5012 row
  tables) yields 741 distinct navigation targets; the densest free band
  below the gate is `[0x5d4,0x68b]` (reproduces the T4a scan).
- Call-site immediates: back-tracking argument registers at all 739 static
  calls into the `page.get`/open/continuation family finds hardcoded opens
  at `0x5d7/0x5d8` (the runtime-observed kind-5 dialog), `0x61a/0x61b`,
  `0x65e/0x660`, `0x665-0x675`, `0x679-0x67b`, `0x684/0x688/0x68b` — the
  chosen window `[0x61c,0x65d]` keeps >= 5 ids of margin on both sides
  (nearest references `0x61b` and `0x65e`).
- Live roots dump (`--roots-audit`, boot + wrench soak): every group-2
  slot in `[0x4c3,0x68b]` holds a boot-time scaffold (pool descriptor,
  `page_count` 0 — never rebuilt; the default identity is `(id<<16)|kind`,
  mixed across the band and uniformly `|5` in the routed window), i.e. no
  stock flow opens these ids; 1505 nonzero-identity slots (mixed kinds)
  exist elsewhere in `[0x100,0x700)` per the original image.

Also fixed in the same change (exposed by the now-reachable own pages): the
T2 `adopt_row_tables` rebind wrote `&tables[first][r]` — the r-th RECORD of
the first table — instead of the r-th table, so the row-ok consumer
resolved the ok trio of the wrong row (probe `t4b-diag-auto-r1`: ok on the
Autostart row staged the Activate row's `next 0x68c`). The consumer indexes
`desc->events[selected + scroll]` (`FUN_40508bd4`: `ldrh [page+0x32]` +
`ldrh [page+0x58]`), confirming the per-table pointer semantics.

Evidence (probe `--phase okchain`, enabled image, one round per page):

- Fenestra: `CHAIN_WORKS` + ownpage `RENDERED` with every probe check true
  (stack 2->3, top `0x63c0002`; live page `page_count` 7, 64 content
  widgets including the borrowed row ids `0x46e8-0x46ed`, title id 3204;
  final frame black fraction 0.041 / 351 colours; wrench classification
  stays `milestone` in the same round).
- Wonder/Auto/Guard: `CHAIN_WORKS` + `RENDERED` each (top `0x63d0002` /
  `0x63e0002` / `0x63f0002`; ancestry `[root, 663, Fenestra, child]` for
  the core-child legs; `page_count` 2, title ids 3205/3216/3217). The
  whole-frame black-fraction cutoff would misjudge these 2-row pages, so
  the probe's own-page frame check is density-aware (lit top bands on a
  fresh frame; `frame_band_fractions` in `okchain-ownpage.json`).
- Regression control: `--phase wrench` re-run stays `milestone` with every
  check true and page 25 keeps rebuilding (`page_count` 45, descriptor not
  ours); `--default-image` control stays `rendered_stock`.

## Unverified / not enabled (explicit boundaries)

| Item | State | Notes |
| --- | --- | --- |
| UI condition ABI window | pending | the 168B dispatch window is not pinned; the configuration records the pending state and the hook stays disabled |
| worker INTERRUPTED continuation | not exercised | the EL0 interrupt/resume path has no interrupt-injection evidence yet (planned follow-up) |
| Native menu registration (menu reachability) | emulation-verified (enabled build) | menu trio, font sites, layout/visibility/draw bindings, the draw-completion primitives, the menu quintuple and construction pinned and hooked (sections 4b/4c); construction verified on the enabled image (probe artifacts `menu-e2e-nav/neg-*`, 3/3 per phase) and the default build keeps construction dormant (negative leg + RE0); the activate leg exercises MCU key delivery with guest stability and observes the activation chain, but key-driven activation is not isolated from the card's auto-activate flag (see 4b caveat). Wrench-tab milestone (4d): the constructed SETUP list page (663) rebuilds and renders with the entry rows visible alongside stock. Own-page routing (4e): entry-row and own-row ok navigation pushes the routed Fenestra/Wonder/Auto/Guard pages and they render (`t4b-okc-*`, all probe checks true); the module-manager and module-UI/apps pages keep `0x7000`-family ids and remain unreachable from wrench rows (kind-2 gate, recorded boundary). Boundary: hardware untested |
| hello `.wlm` load round trip | verified in emulation | full install→activate→load→user_call chain, 3/3 runs; activation to ACTIVE also observed on the enabled image (`menu-e2e-act-*`: worker magic + phase 2), attribution per the 4b caveat (auto-activate flag not isolated) |
| debug identity / native namespace pipeline | not wired | the shared tail of verify_platform_build is not yet z8-integrated |
| PTP / Linux bridge / USB debug / fonts / display | capability unavailable | excluded from the v0 source list; stubs return FX_CAPABILITY |
| task.create/resume/delay addresses | pinned on Z8 (C311) | `0x40185064` / `0x401855e0` / `0x40187b00` re-derived from C311 bytes alone (semantics: 56-byte record + TCB pop + ready-list insert / id bounds + id-table wake / wake-list link now+ticks; firmware caller cluster `0x40d2d524`→`0x40d2d5fc`→`0x40d2d658`; 16B entry-window sha256 pins in `tests/test_z8_task_pins.py`). Values equal the former Z9 references. Runtime: create/resume run on every module load (worker start), delay on the loader yield path (all runs) and the sysctl stop-handshake wait loop (forced-delivery context); the earlier "create/resume not exercised" wording was stale — the verified hello round trip (§10) requires them |
| GUI mutex trio | pinned on Z8 | `0xa032b8044` / acquire `0x40d2bfb4` / release `0x40d2bf60`, used by both refresh paths and the pair dispatch wrapper `FUN_403f6388` (object `0xa032bbd68`); the former Z9 values (0xa032dcb98/0x40d30004/0x40d2ffb0) are absent from the sources |
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
