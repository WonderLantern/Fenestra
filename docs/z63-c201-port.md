# Nikon Z6III C2.01 experimental port

This development port adds Fenestra to the native Setup menu, discovers WLM
packages from the card, and starts SDK workers on original Nikon tasks. Modules
execute at EL0 with private views; copied requests execute on original tables.
Native application pages and queued UI calls use the original input task.

Full-system emulator controls cover memory, event flags, caller context,
resource snapshots, queued native calls, page paths and RGBA images, native
input, file create/write/seek/readback, and same-request critical calls. Capability
grants are restricted to each package's required and optional declarations.
The native manager supports 8.3 module names, Off/On switching and native
keyboard search. Full-system controls cover cancellation, submitted text,
filter clearing and a subsequent activation. Native mutex/semaphore lifetimes
and read-only IRQ queries pass separately through the dedicated debug task.
The base mapping provider's query and translation pass through the copied SDK.

Ordinary modal dialogs and measured document pagination now pass native input,
finish and live-view restoration. Global drawing passes three visible paced
frames and clean close in two runs after publishing through the original
graphics owner and reacquiring a masked target on explicit resubmission. A
twelve-package control starts and finishes twelve workers with 120 successful
copied requests.

Startup controls now include default-off autostart, two configuration slots,
a retained boot guard and explicit clearing of a previous guard. Manual start,
guard blocking/clearing, autostart, repeated configuration changes and native
power-switch shutdown pass in the model. Readback verifies both slots' CRCs,
generations and target identity, and confirms guard removal after clean stop.
Loaded workers remain retained until reboot; guard clearing does not unload or
restart a faulted worker.

USB/IP status/echo/identity, the RTOS monitor, native calls/printf, synchronization,
IRQ queries, original Linux shell IPC and the Linux RAM agent pass with a
separate virtual USB startup-setting input. The supplied baseline USB startup
path remains unqualified. The RAM-agent control verifies the exact uploaded ELF, argv jobs,
owned-job cancellation, RAM-file readback, self-memory access and stop acknowledgement.
Raw SDK function/table names match the Z9 profile; original editor operations
still require the native UI task and a live singleton session. MMU context
map/translate/protect/unmap pass on owned, unpublished tables after every pool
page's physical address is checked. These tables are never installed on a CPU.
Stock selected-root mutation, original-writer exclusion and DMA aliases require
separate qualification.

The bundled character-rain SDK demo also passes sustained rendering through
445 native frames, Back/reopen and acknowledged power-switch shutdown.

The retained native panic display reports a deliberate EL0 data abort and
reacquires a foreground target masked by a native transition. Both error text
and CPU diagnostics are checked separately from the recorded renderer frames.
Live titles and measured label/value fitting have separate visual acceptance.
All candidates remain **emulator-only; DO_NOT_FLASH**. No physical camera or
updater was tested.

## Inputs and bindings

Supply the complete original C2.01 EG including its CRC locally:

| Input | Bytes | SHA-256 |
| --- | ---: | --- |
| Original EG | 88,830,367 | `d78e4d0358f550d9f3a3a9da3a8beee314a3b90de8bcc13117e029f7821cf252` |

`targets/z63-c201.json` separately records its payload identity, native card
wrapper fingerprints, SYSCTL table and menu-hook fingerprints. Firmware bytes,
loader/SNOR/NAND inputs, generated displaced instructions and build outputs are
not source-distribution files.

SYSCTL event 19 is the startup **sync hook**; event 20 is startup-complete.
Shutdown, card removal and card mount are 8, 24 and 25. The lifecycle inbox
coalesces notices, requires startup and media before activation, and makes STOP
terminal. A notice does not prove native work has retired.

The card scope owns one descriptor. It preserves native errors, short reads and
EOF, requires an actual-count output and retains uncertain ownership after an
impossible count or failed close. Native reads are aggregated in chunks of at most 511 bytes into bounded public
reads of at most 65536 bytes. This bound
does not establish physical DMA/cache coherency. Use lowercase 8.3 filenames;
the native test opened `demo-z63.wlm`, while `probe-z63.wlm` returned
`0x80001001`. The adapter and SDK builder reject longer base names for now.

The manager maps logical `id.wlm.disabled` names to physical `id.off` files.
Discovery skips `.off`. Rename never replaces an existing destination, checks
both resulting names and closes further manager IO after uncertain completion.
An enabled filename must have the same ID before its suffix; renaming does not
change a package's internal identity. Loaded code remains retained until reboot.
Search changes the copied inventory view and does not rename or reread packages.

The optional debug build exposes original native mutex, semaphore and IRQ
bindings. These retain their original ABI and lifetime requirements. A raw
native ID, pointer or IRQ descriptor has no automatic owner retirement.
The mapping provider similarly requires a caller-owned unused virtual address,
physical resource and exclusion of original writers before mutation. Its tested
query/translation path does not establish those mutation prerequisites.

## Build the test module

Use Python 3.11+, Clang and an ELF-capable `ld.lld` on PATH:

```sh
python -B host/build_sdk_module.py examples/z63_probe/module.c \
  --id demo-z63 --target z63-c201 --required 0 \
  --output-dir build/demo-z63
```

Copy `demo-z63.wlm` to `FENESTRA/demo-z63.wlm` on a disposable virtual FAT card.
The explicit pinned probe builder can pin that package's digest and size;
the normal discovered runtime does not. `module.json` is a
host receipt and is not copied to the card. This headless module publishes event
sequences 1, 2 and 5; it does not draw or call native camera services.

The standard `examples/hello/module.c` and `modules/demo/sdk.c` also build
with `--target z63-c201 --required 0x310000`; use IDs `hello` and `demo`.
See [Module development](module-development.md) for both commands.

## Build an emulator candidate

Choose a new output directory and supply explicit LLVM/linker locations:

```sh
python -B fenestra.py build --target z63-c201 \
  --firmware /local/inputs/eg2010_0201i0.bi \
  --llvm-bin /local/toolchain/bin \
  --linker /local/toolchain/bin/ld.lld \
  --output build/z63-runtime
```

Run the native Fenestra action to discover packages in `FENESTRA/`, then open
Applications for modules with visible controls. This path does not pin a test
package's filename, size or digest at compile time. The earlier headless sample
has no SDK capabilities; examples `sdk.c`, `services.c`, `tasks.c`, `image.c`,
`files.c`, `critical.c`, `text.c`, `mapping.c`, `dialog.c`, `document.c` and
`global.c`, `simd.c`, `mmu.c` and `bindings.c` provide separate SDK controls. The dialog, document and global
controls have native presentation/close acceptance in the model. Reader touch
pagination remains unqualified; its native buttons pass exact offset checks. Declare
only the capabilities each example needs when building its WLM.

The builder checks the complete original image, native binding fingerprints,
hook spans and zero-filled reservation before compiling. `verified.json` is an
independent check of linked code, exact hook destinations, displaced original
instructions, compiler dependencies and byte-for-byte original reconstruction.
`probe.json` and `identity.json` retain the matching build identities. The EG
retains its original member CRC bytes and is intended for direct emulator input.

`--experimental-ptp` includes the native debugging experiment; it does not
qualify USB transport. `build-z63-probe` remains available for explicit pinned,
discovery, drawing and headless negative controls.

`package-z63` accepts the matching complete original ciphertext/plaintext twins
and a candidate receipt. It repairs the MAIN and body CRCs and emits fixed-layout
plaintext and encoded-body research artifacts. The other four members must stay
identical. A complete envelope additionally requires local authentication
material that first validates the exact original package; that C2.01 path is
unqualified. Body reconstruction is not updater acceptance.

## Full-system acceptance so far

The tested environment uses a matching `nikon-z63` model, original C2.01 EG,
retained loader and SNOR inputs, cloned NAND/FAT storage, and synthetic Bayer
and IMU inputs. Neither inputs nor the emulator are bundled here. `host/z63_emulator_smoke.py`
accepts explicit external input paths, clones its NAND/card, controls QMP/MCU
keys, captures frames and checks module markers from a matching external
`z63log` plugin. Use its `--help` for the input list. Its automated result
checks execution markers and guest state; screenshots still require review.
It never mounts or modifies the input card and closes only its own QEMU process.

Observed results include stock live view; patched native live view; startup and
virtual card event forwarding; native task creation/resume/entry; the Fenestra
Setup entry and page; manual activation; native card loading; INIT/TICK/STOP
returns; zero recorded faults/context-restoration errors; and a later native
shutdown event followed by the model's power-hold shutdown. The negative-control
module in `examples/z63_probe/fault.c` deliberately writes an unmapped EL0 page.
Its run records `FX_INTEGRITY`, one fault and zero context-restoration errors;
the guest continues. This is bounded fault evidence, not arbitrary-code or
hardware fault containment.

The current owner retains session/task state until reboot and permits one
activation attempt per boot. There is no unload/retry contract. The native menu test covers activation, Back to Setup and return to live view
using MCU buttons. With `--repeat-activation`, pressing Start twice produces one
worker and one INIT/TICK/STOP sequence. Media loss during IO and shutdown under
load need separate acceptance before broader runtime claims.

The current host suite runs 82 tests with three platform skips. Earlier
bring-up checks covered 43 original
FILESRV cases, and seven adapted original-kernel fixture cases. The source export is verified independently against its inventory; build
outputs and private firmware inputs stay outside it. These
checks are separate from the full-system success and deliberate-fault runs.
Use `--expect fault` for a candidate pinned to `examples/z63_probe/fault.c`; use
`--expect rejected` with the success candidate and a different on-card package
to check rejection before module entry. Full-system controls reject both a
different package size (`FX_BOUNDS`) and a same-size one-byte mutation
(`FX_INTEGRITY`), without invoking the module; native menu return still works. Each run uses a fresh output directory.

## Host checks

```sh
python -B -m unittest discover -s tests -v
python -B verify_source.py
```

Set `FENESTRA_Z63_EG` to the original EG path to enable its optional binding test.
Host fixtures cover actual-count failures, native errors, close uncertainty,
task create/resume retention, lifecycle STOP races and translated resident-code
mapping. They substitute native IO/tasks and remain separate from full-system
results. `build-z63-adapter` also builds the standalone card object after original
input verification; it emits no resident core or firmware package.

## Related ports and validation

The [Zf C3.01 PR](https://github.com/WonderLantern/Fenestra/pull/3), reviewed
at head `d3a00a59ae09c63fb27655225bf8981271e514f4`, is a useful EXPEED7
reference. This port also admits an exact original payload, caps native cached
reads at 511 bytes, compares discovered names without ASCII case sensitivity,
and uses the native visible viewport for ordinary page drawing. Its bindings,
hooks and original-image checks are specific to Z6III C2.01. Zf's reported
physical-camera success does not qualify this target.

The host USB profile `z63-c201` accepts the model/version pair `Z6_3` / `V2.01`
observed in the full-system guest, along with explicit spelling aliases.
A different firmware version is rejected before opening a PTP session.

With a locally supplied eight-byte digest suffix, packaging first verifies the
exact original C2.01 digest, then changes only header bytes `[0x240,0x254)`.
It preserves all other 608-byte header fields, including the twelve following
bytes, version fields and RSA slots. Fixed-length paired ciphertext/plaintext
reconstruction and repaired member/body CRCs produce a complete local container.
This follows the Zf repacker's header-preservation approach; it does not create
a new vendor signature or establish updater admission. Neither suffix nor
firmware bytes belong in the source archive.

This is the first Z6III C2.01 support change. The existing ZR C1.10 target and
the Zf C3.01 PR supply shared-core and packaging references. The implementation
follows the upstream [porting guide](porting.md), with target-specific bindings
and independent build checks.

Current validation covers local host tests and full-system emulator controls.
Physical-camera installation and runtime testing remain pending; generated
firmware artifacts retain `DO_NOT_FLASH`. No firmware, local material or
device logs are included in this source change.
