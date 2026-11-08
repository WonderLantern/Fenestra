# Z8 C 3.11 platform port validation record (v0 headless)

> Corresponds to the `z8-c311` platform contribution; validation date 2026-10-31.
> The `(v0 headless)` title names the v0 contribution scope: the default build
> is headless-equivalent, and the menu-enabled build (`build_z8_core.py
> --menu-enabled`) is the main verification form of the menu sections 4b-4f.
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
  propagate under forced delivery** (an earlier revision of this section
  claimed a worker shutdown flag observation; that evidence read a wrong
  pointer and is withdrawn). Superseded by the worker-stop verdict on
  natural delivery below: the forced gdbstub re-entry path never reaches
  the stop-handshake code the natural dispatch task exercises.
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
- Probe-evidence withdrawal (root cause revised twice, final): the earlier
  `probe-r2-eject` result ("no dispatch, no word change within 25 s") is
  withdrawn as invalid evidence caused by a probe defect, not firmware
  behavior. Final root cause (L2 forensics, 2026-11-08,
  `.superpowers/sdd/2026-11-07-agroup-v2/reports/l2-sysprobe-stall.md`):
  the probe's own software breakpoints left armed at hot entry points
  re-trap with zero instructions executed on every QMP continue (QEMU
  gdbstub has no step-over-on-resume), so the keep_gate observation
  windows livelocked at the event-34-hammered gate entry and never
  sampled the toggles. The first sticky-`Z8_UNKNOWN` soak-boundary
  explanation does not apply to this leg (its stderr has no Z8_UNKNOWN
  lines). The probe-side fixes landed afterwards (T10/T10b: one-shot
  breakpoints, keep_gate escape, vt-only liveness witness, ch8/9 service
  clocking; `scripts/z8_sysctl_probe.py`), and with the fixed probe the
  eject leg now classifies NATURAL on both the probe-built image
  (`build/re-runs/probe-t10b-eject-fin`) and the t9-exempt image
  (`--patched-main` leg) — a positive result that supersedes the
  withdrawn negative. The corrected qtest-only observation template is
  z8-qemu `.local/runs/e2b-run3/`. The
  EX-boot structural caveat (boot-cleanup event 23 latching sticky shutdown
  before the module chain can load; host-peer boots order 19 after mount
  and carry the verdict) is documented in the E2c report
  (`.superpowers/sdd/2026-11-07-followup-arcs/reports/e2c-natural-delivery.md`)
  with the Fenestra-side fix direction (pre-activation 23 exemption) since
  implemented — see the boot-cleanup exemption bullet below.
- Natural delivery (E2b/E2c, 2026-11-07): the runtime
  `cfexpress-card-present` QMP toggle (bit 1 of `0x17a00044`) is the live
  source of MAIN's storage-slot poller — task loop `0x4031b2d8`, enable
  byte `0xa02f28da0` set once at service init and never cleared, per-slot
  presence byte `0xa02f28da5+slot*0xc`, read every 50.18 ms median from pc
  `0x402b5458` (slot 0/CFe via `0x4028fc8c`→`0x402b542c`; slot 1/SD reads
  `0x17a0000c` bit 0 the same way). The earlier image scans found no
  reference because the register is reached through a driver
  halfword-offset table (`0xa02808508`), not ADRP immediates. Toggling
  delivers the natural events end to end: card-out → ring `0xa0341f2e0`
  id 23 (CardRemove) + word `0x303→0x301`; card-in → ids 22 + 24
  (CardInsert + CardMountComplete, closing the id-24 delivery-bridge open
  item) with a 769-read mount rescan and word back to `0x303`. Legs: stock
  MAIN mapping/control/re-run/equivalence (z8-qemu
  `.local/runs/e2b-run{3,4-ctl,5-map,6-equiv}`), patched menu-enabled MAIN
  delivery/re-run/control with zero unmodeled MMIO (`.local/runs/e2c-run{1,2}`,
  `.local/runs/e2c-ctl`).
- Worker-stop verdict on natural delivery (E2c, closes the §4a open item):
  a natural event 23 runs the pre-authored stop handshake — worker phase
  ACTIVE(2)→QUIESCED(4), shutdown flag 0→1 (monotonic), error=0,
  `startup.shutdown_done=1` (artifacts `.local/runs/e2c-noex{,2}`, two
  field-identical runs). Re-insertion delivers 22+24 naturally and
  re-mounts, but the worker stays QUIESCED: shutdown is sticky and
  re-insertion does not revive the module (firmware-side behavior, recorded
  as fact).
- Boot-cleanup CardRemove exemption (T9, 2026-11-07; closes that open item):
  every EX boot emits a natural CardRemove (23) through the installed gate
  before the first mount completes — running the stop handshake on it
  closed the startup service before any module could load (modules load
  from the card, so the chain can never win that race). The gate now
  exempts exactly that 23: `fx_z8_lifecycle_boot_cleanup_window(word,
  phase)` returns 1 iff the sticky `FX_Z8_LIFE_MOUNT_EVER` bit (0x8, set by
  event 24, never cleared, outside the PENDING mask) is unset AND the
  startup phase is one of {UNCHECKED, READY, ABNORMAL, IO_ERROR}
  (pre-ARMED; FAULT/STOPPED fail closed). An exempt 23 skips
  cleanup_window/shutdown entirely but is still recorded (CARD_PENDING,
  the forensic surface is unchanged). Three legs on the exemption build
  (patched MAIN from the T9 commit, QEMU sha `3757c7b3…`, artifacts under
  z8-qemu `.local/runs/t9-exempt/`):
  - EX leg (`leg1-ex/run4`): boot word `0x301` with CARD_PENDING and no
    MOUNT_EVER (the boot cleanup 23 exempted, no sticky shutdown); insert
    → 22/24 delivered naturally → word `0x30b`, module worker ACTIVE,
    FXBOOT1 flag valid and DCIM/NIKON/Z_8 created on the card; the later
    natural card-out 23 (post mount, MOUNT_EVER set) still runs the stop
    handshake verbatim (worker QUIESCED, sticky shutdown), and re-insertion
    re-arms the word `0x309→0x30b` with the worker staying QUIESCED — the
    E2c verdict unchanged on the exemption build.
  - noex re-verification (`leg2-noex/run1`): host-peer boot with the card
    present from t0 — boot ring exactly [19,24] with no cleanup 23 (the
    preinstall mount-hole boundary evidence), word `0x30b`, worker ACTIVE;
    the later natural 23 is not exempted (QUIESCED, shutdown monotonic,
    `shutdown_done=1`, State.shutdown=1).
  - Discriminant leg (`leg3-disc/run5`): noex boot with the shipped
    startup record rewritten to AUTO off, so the service inspects to READY
    and parks there (an otherwise-exempt phase). With MOUNT_EVER set by the
    boot 24, a forced 23 (captured real event-34 dispatch message patched
    to 23, re-entered through the pinned stock dispatcher entry
    `0x40d60ec8`; the T13 inject pattern) is NOT exempted: sticky
    startup.shutdown set, handshake completes asynchronously
    (`shutdown_done=1`, State.shutdown=1), word `0x30b→0x309` (CARD
    cleared, CARD_PENDING set, MOUNT_EVER and STARTED retained). This pins
    the discriminator's negative space: the READY phase alone is in the
    exempt set — only the mount witness blocks the exemption.
- Known boundary (accepted by decision, option A): the preinstall mount
  hole — in a host-peer (noex) boot the physical mount can complete before
  event 19 (installation), so the gate may never observe a 24 and
  MOUNT_EVER may stay unset while the card is really mounted; a real card
  removal in the window between installation and activation completion
  would then be wrongly exempted. The E2c noex runs measured the boot ring
  as [19,24] (a 24 is still delivered after installation) and the leg-2
  re-verification reproduced that sequence on the exemption build — the
  hole is closed empirically, not structurally. Every post-mount 23 keeps
  the conservative stop-before-remove semantics as the hardware-
  conservative layer (family comparison: Z9/ZR hook no card remove; Zf is
  notice-only on real hardware; Z63 shares the 8∥24 stop with no
  exemption — recorded as upstream-issue material).
- Boundary: the event 7/23 evidence is forced delivery under the emulator
  (gdbstub re-entry in an arbitrary stopped CPU context with sync=1, which
  skips the dispatcher mutex; the forced path never reaches the
  stop-handshake code — see the worker-stop verdict above), and the worker
  INTERRUPTED continuation path remains unexercised.

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

## 4f. Manager/apps/module routing: the shared families join the routed window (T5)

T4b left the module manager, the apps list and the module UIs on the
`0x7000`-family ids (unreachable: kind-2 gate, 4e). T5 routes them into
the same audited window through a single platform allocation table
(`src/platform/z8_c311/menu_route.c`, `fx_z8_route`) whose invariants are
validated at construct time (`fx_z8_route_validate`): every routed id
inside `[0x61c,0x65d]` with >= 5 spare ids at both ends and strictly below
the `0x68c` gate; every borrowed row base inside the 99-row stock family
`0x45cf..0x4631` (`0x43cf133c`, byte-verified in the mirror) and pairwise
disjoint.

| Family | Routed ids | Reached from | Notes |
| --- | --- | --- | --- |
| Fenestra core / wonder / auto / guard | `0x63c-0x63f` | 4e (unchanged) | own pages, full-shape rebinding |
| Manager UI window | `[0x624,0x636)` | core `Modules` row ok (`next` = `0x624`) | one `FX_NATIVE_UI_PAGES` window; rows base `0x4603` (0..23, back/notice `+40/+41` = `0x462b/0x462c`; moved off `0x4600` in T12 to host two module slots below it) |
| Apps list pages, ordinals 1..6 | `0x636-0x63b` | apps `Next`/`Previous` rows (ordinal 0 = wonder `0x63d`) | `FX_Z8_APPS_PAGES=7` (root + six list pages = 60 apps); a larger module count is the explicit shared notice |
| Module-UI dynamic window | `[0x640,0x652)` | apps row ok (per-module `page_base`) | `FxNativeUiSpace` bounds; every routed module UI takes a primary+picker pair and pages deeper navigation on demand (native pager); TWO bounded row slots |
| Apps rows | `0x461d-0x4626`, `0x462d-0x4631` | — | shared offsets 0..9 and `0x10..0x14` re-based (was `0x7b00..0x7b14`) |
| Module-UI row slots | `0x45cf-0x45e6` + `0x45e7/0x45e8`, `0x45e9-0x4600` + `0x4601/0x4602` | load order | controls/pickers 0..23, back/notice packed at `+24/+25` (T12) |

Design points (all pinned by offline tests + the virt fixture
`tests/test_z8_menu_route.py`):

- **Shared code stays target-neutral.** `fx_native_apps_init` now takes
  the page scheme (wonder id, application base, page count) and the row
  base; `fx_native_modules_init` takes the manager page base. Z9/ZR pass
  their historical constants; Z8 passes the routed table. The
  `0x7b00-0x7b14` apps row literals (whose error-page rows `0x7b12-0x7b14`
  are not registered Z8 stock ids and would abort the open-path row
  rebuild, T12 P3) are gone from the shared source.
- **The wonder id resolves to the apps root** (`fx_z8_menu_page` →
  `apps_page`): with no modules the root shows the same `No loaded
  modules` + Back pair the static T4b page did; with modules it lists
  them and each row's ok opens the module UI at its routed `page_base`.
  The static wonder page and its two borrowed rows (`0x46ef/0x46f0`) are
  gone. The apps back target is the 663 wrench list (the page the entry
  rows live on), not the 638 hub.
- **Bounded dynamic slots fail explicitly.** Module-UI row bases are
  assigned by load order from a two-entry table; a third module UI
  returns `FX_CAPABILITY` (surfaced as the apps notice page — explicit,
  never a silently unusable id). Page exhaustion of the routed window
  maps to `FX_CAPABILITY` the same way. The two slots pack back/notice
  rows at `+24/+25` (`fx_z8_route.module_nav_row_offset`; the shared UI
  row code takes the offset per instance, `FxNativeUi.nav_row_offset`),
  which is what fits two 24+2 families below the manager in the 99-row
  pool. Every routed module UI now sub-allocates its pages on demand:
  the primary+picker pair from the window plus two-page pager views for
  deeper navigation (the `controls>16` pager precedent generalized), so
  the first module no longer reserves the whole 18-id window.
- **The new families deliberately keep their shared handler tables**
  (no full-shape rebinding): their row input goes through the shared
  condition-gate callbacks (`fx_native_ui_condition_gate` reads the
  caller's `x20`/`x22` page+event, the context T4a pinned for the record
  processor) and the deferred navigation handshakes
  (`fx_native_ui_prepare_navigation` / `fx_native_apps_prepare_navigation`
  -> `FUN_403ad6d8` with a routed id, which passes the gate because the id
  is renumbered — that is the whole point of T5). Rebinding them to
  59-record stock tables would break the shared integrity checks in
  `fx_native_ui_event` (`events[row]` must stay inside the shared handler
  or `FxNativeRowProgram` storage).

Evidence (probe legs on the enabled image, `build/re-runs/t5-*`, 2026-11-07):

| Leg | Result |
| --- | --- |
| `t5-base-wrench` (pre-change baseline) | `milestone`, all checks true |
| `t5-mgr-okc-r1` (okchain, core Modules row ok) | manager `0x624` PUSHED (stack 3->4, top `0x6240002`, ancestors `[root, 663, 0x63c, 0x624]`) and RENDERED: `page_count` 17, borrowed rows `0x4600`(PAGE control)/`0x4601`(Changes)/`0x4602`(Refresh)/`0x4603`(module switch)/`0x460f`(Search) built as live widgets, title id 3230, frame bands 0.94-0.98; wrench `milestone` held in the same round |
| `t5-apps-okc-r1` (okchain, 663 entry row 1 ok) | apps root `0x63d` `CHAIN_WORKS` (2->3, top `0x63d0002`) and RENDERED: `page_count` 2 (hello row + back), row id `0x461a` built, title id 3205, boundary clean |
| `t5-hello-okc-r1` (okchain, apps hello row ok) | hello module UI `0x640` PUSHED (3->4, stack `[root, 663, 0x63d, 0x640]`) -- the apps condition-gate -> deferred flush -> `FUN_403ad6d8` -> routed-page chain works; rows `0x45cf-0x45d3+` built, title id 3304 resolved; render PARTIAL (top bands painted, layout stalled at the boundary below) |
| `t5-wrench-r1` (regression) | `milestone`, every check true, rc=0 (zero regression) |
| `t5-mgr-ra-r2` (rowactions, manager plan) | `manage-modules` cb consumed at the record-processor cb site (hit x1, key index isolated); manager live tables resolved from the live descriptor: trio identical, PAGE-control row next `0x625` (its sub-page) exact; refresh/switch rows resolved with stay nexts (first-open controls land asynchronously -- see boundary) |
| `t5-stock-r1b` (default image control) | `rendered_stock`, every check true, rc=0 |

Discovered boundary — RESOLVED in T12 (2026-11-08): the first open of an
`FxNativeUi`-family routed page used to hit one in-window unmapped read
`cpu=0 pc=0x403b0278` (staged visible-row builder `FUN_403b01f0`:
`ldr x8,[x23,#8]` with `x23=0x600000000`). Root cause (RE, T12-①):
`FUN_403b01f0` loads its visible-row vector from `*(descriptor+0x40)` — a
REAL field of the stock 0x50-byte descriptor (ROM template `0x44d35040`
stride 0x50 carries a record-table pointer at `+0x40`; the pointed-to
`{count, content}` shape verified at `0x43ed1398`) — while the shared
builder published only the 64-byte `FxNativeDescriptor` and let the
copied stock header sit at `+0x40`; its first eight bytes (`{0, 6}`) read
as the pointer `0x600000000`. Fix: the profile now carries a descriptor
tail (`fx_z8_descriptor_tail` = an empty staging vector) published at
`+0x40/+0x48` for all three routed families; the builder loop is skipped
and the open path completes. `t12-hello-tail-r2`: `pc=0x403b0278` gone
(only the pre-existing boot-time `cpu=-1/pc=0` access remains), hello
module UI `RENDERED` (full frame, fresh content) where `t5-hello-okc-r1`
stalled after the top bands. Residual observation (documented, not
load-bearing): the count read at `[x23]` (`0x403b0254`, size 2) never
reached the emulator's unknown-access logger even though a monitor-level
2-byte read of `0x600000000` logs `Z8_UNKNOWN` — with the fix neither
access is unmapped, so the path no longer depends on that behavior.
Also observed (still true): the module-manager control copy lands
asynchronously (first-open switch rows still carry stay nexts until the
scan/publish completes and the page rebuilds).

T12 dual-module evidence (two SDK modules installed from one card;
`build/re-runs/t12-dual-*`, 2026-11-08 — second module = the hello
example variant `second-z8`, card built without mounts by
`scripts/z8_card_add_module.py`, spec-level exFAT append validated by the
same parser the probes use):

| Leg | Result |
| --- | --- |
| `t12-dual-mgr-r3` (dual card, manager walk) | manager `0x624` PUSHED (3->4, top `0x6240002`) `CHAIN_WORKS`, `page_count` 17, title id 3230, rows at the re-based `0x4603` family render (bands 0.94-0.98) |
| `t12-dual-apps-r3` (dual card, apps walk) | apps root `0x63d` `CHAIN_WORKS`, `RENDERED`, `modules=2` installed and listed |
| `t12-dual-hello-r4` (dual card, proven row-0 walk) | module UI `0x640` PUSHED and `RENDERED` with BOTH UIs live: probe-side `module_uis` reads UI0 `page=0x640/text=3304/rows=0x45cf/nav=24` and UI1 `page=0x642/text=3380/rows=0x45e9/nav=24` — two concurrent module UIs in the one window, distinct row bases, packed nav offsets |
| `t12-dual-second-r8/r9` (entry-swapped dual card, same walk) | the swapped directory order installs `second-z8` first; the row-0 UI (then the second module's) PUSHED and `RENDERED` |

## Unverified / not enabled (explicit boundaries)

| Item | State | Notes |
| --- | --- | --- |
| UI condition ABI window | pending | the 168B dispatch window is not pinned; the configuration records the pending state and the hook stays disabled |
| worker INTERRUPTED continuation | not exercised | the EL0 interrupt/resume path has no interrupt-injection evidence yet (planned follow-up) |
| Native menu registration (menu reachability) | emulation-verified (enabled build) | menu trio, font sites, layout/visibility/draw bindings, the draw-completion primitives, the menu quintuple and construction pinned and hooked (sections 4b/4c); construction verified on the enabled image (probe artifacts `menu-e2e-nav/neg-*`, 3/3 per phase) and the default build keeps construction dormant (negative leg + RE0); the activate leg exercises MCU key delivery with guest stability and observes the activation chain, but key-driven activation is not isolated from the card's auto-activate flag (see 4b caveat). Wrench-tab milestone (4d): the constructed SETUP list page (663) rebuilds and renders with the entry rows visible alongside stock. Own-page routing (4e): entry-row and own-row ok navigation pushes the routed Fenestra/Wonder/Auto/Guard pages and they render (`t4b-okc-*`, all probe checks true). Manager/apps/module routing (4f): the manager window `0x624`, the apps family (`0x63d` root + `0x636-0x63b` list pages) and the module-UI window `[0x640,0x652)` join the routed band via `fx_z8_route`; the `0x7000`-family ids remain reserved-but-unroutable. T12 closed the 4f boundaries: the staged visible-row builder fault at first open is fixed (descriptor tail published; `t12-hello-tail-r2` renders fully with no unmapped read), TWO concurrent module UIs are supported (packed row slots `0x45cf`/`0x45e9`, on-demand page sub-allocation; dual-module legs render both), and the apps list spans six pages (60 apps) with the shared notice beyond. Remaining 4f behaviors: the routed families' row input relies on the shared condition-gate + deferred-navigation handshakes (never the full-shape rebind of 4e), a THIRD module UI fails explicitly `FX_CAPABILITY` (row pool bound), and the module-manager control copy lands asynchronously (first-open switch rows keep stay nexts until the rebuild). Boundary: hardware untested |
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
