# Nikon Z6III C2.01 experimental port

This development port boots an emulator-only SYSCTL integration, adds Fenestra
at the top of Nikon's native Setup menu, and manually loads one pinned headless
`.wlm` from the card. The original scheduler starts the worker, Fenestra's loader
checks the package, and its private EL1/EL0 gates execute INIT, TICK and STOP.
Full-system tests observe successful returns and restored native context.

The current port supplies no camera SDK service capabilities. Module discovery,
the module manager, ordinary demo drawing, PTP/Linux debugging and update-package
production remain unfinished. It is not equivalent to the hardware-tested Zf
port. All candidate EGs are **emulator-only; DO_NOT_FLASH**. No physical camera
or updater was tested.

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
impossible count or failed close. Each request is at most 511 bytes. This bound
does not establish physical DMA/cache coherency. Use lowercase 8.3 filenames;
the native test opened `demo-z63.wlm`, while `probe-z63.wlm` returned
`0x80001001`. The adapter and SDK builder reject longer base names for now.

## Build the test module

Use Python 3.11+, Clang and an ELF-capable `ld.lld` on PATH:

```sh
python -B host/build_sdk_module.py examples/z63_probe/module.c \
  --id demo-z63 --target z63-c201 --required 0 \
  --output-dir build/demo-z63
```

Copy `demo-z63.wlm` to `FENESTRA/demo-z63.wlm` on a disposable virtual FAT card.
The builder below pins that exact package's digest and size. `module.json` is a
host receipt and is not copied to the card. This headless module publishes event
sequences 1, 2 and 5; it does not draw or call native camera services.

## Build an emulator candidate

Choose a new output directory and supply explicit LLVM/linker locations:

```sh
python -B fenestra.py build-z63-probe \
  --application /local/inputs/eg2010_0201i0.bi \
  --llvm-bin /local/toolchain/bin \
  --linker /local/toolchain/bin/ld.lld \
  --module build/demo-z63/demo-z63.wlm --menu \
  --output-dir build/z63-menu
```

The builder verifies the original image and native hook spans, checks the full
zero-filled mapped image reservation, extracts displaced register-save
instructions locally, compiles and links the candidate, checks branch ranges,
and proves byte-for-byte original reconstruction after reversing declared edits.
`probe.json` identifies the candidate and its symbols. The candidate retains the
original member CRC bytes unchanged and has no updater envelope. It is consumed
directly by the emulator, not by the camera updater.

Without `--menu`, `--module` enables an automatic card-mount experiment. Without
both, the image only observes original SYSCTL entry/return and starts a bounded
leaf task at startup. These are distinct experiments, not release modes.

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
using MCU buttons. With `--repeat-activation`, pressing Run twice produces one
worker and one INIT/TICK/STOP sequence. Media loss during IO and shutdown under
load need separate acceptance before broader runtime claims.

The final source passes 69 host tests with three platform skips, 43 original
FILESRV cases, and seven adapted original-kernel fixture cases. A clean explicit
source export builds byte-identical test module and resident candidate. These
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
