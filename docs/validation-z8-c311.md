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
  `…AppFirstStartComplete`, id=24 `…CardMountComplete` — names compared byte
  for byte. **The three key event ids match the Z9 values.**

### 5. Core build (`host/build_z8_core.py` + `emulation/z8_core.ld`)
- 73 source files compiled (LLVM 19.1.7, the same strict flag set as Z9,
  including -Werror -Wconversion).
- Linked at `0x401A0000` (original RX padding, asserted all-zero over 384KB
  before the build); core size 41,912 bytes in the earlier UI-hooks-only
  configuration, 177,280 bytes in this port configuration.
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
  half-press path.
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

## Unverified / not enabled (explicit boundaries)

| Item | State | Notes |
| --- | --- | --- |
| UI condition ABI window | pending | the 168B dispatch window is not pinned; the configuration records the pending state and the hook stays disabled |
| Native menu registration (menu reachability) | not enabled | menu.c is ported but the Z8 reverse engineering of SETUP pages/events/GUI object ids is not complete; v0 platform state stays uninitialised |
| hello `.wlm` load round trip | verified in emulation | full install→activate→load→user_call chain, 3/3 runs; menu-driven activation (module management entry) still pending |
| debug identity / native namespace pipeline | not wired | the shared tail of verify_platform_build is not yet z8-integrated |
| PTP / Linux bridge / USB debug / fonts / display | capability unavailable | excluded from the v0 source list; stubs return FX_CAPABILITY |
| task.create/resume/delay addresses | Z9 reference values | prologue probes exist; create/resume are not exercised by the validated configuration; the delay binding runs on the loader's yield path and is tolerated (all runs) |
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
