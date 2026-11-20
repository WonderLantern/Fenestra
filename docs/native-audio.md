# Original Nikon audio playback backend

The ZR C1.10 and Z9 C5.32 adapters reuse the firmware's SoundService and AudioIF
playback path. They do not program a codec, clock, speaker GPIO or DMA controller
directly, call the factory WAV-file test, or depend on QEMU audio devices.
An ordinary `.wlm` uses the same [PCM SDK](audio.md) on either model.

## Firmware evidence

All addresses are pinned by the complete original application SHA-256 in
`host/platform_profiles.py`. Original images remain external inputs. No firmware
bytes, disassembly, keys or private research artifacts are distributed here.

| Native contract | ZR C1.10 | Z9 C5.32 |
| --- | --- | --- |
| SoundService queue submission | `0x40f3580c` | `0x40d25554` |
| Playback dispatcher | `0x40f37084` | `0x40d26c68` |
| Playback state getter | `0x40f371d0` | `0x40d26da8` |
| Recording state getter | `0x40f36920` | `0x40d26534` |
| Beep state getter | `0x40f37924` | `0x40d2756c` |
| Playback consumer pointer | `0xa034038b0` | `0xa03219610` |
| Playback command base | `0x431d0010` | `0x431d0014` |
| INIT / START result byte offsets | 21 / 16 | 20 / 20 |
| INIT additional input byte at offset 20 | 2 | 0 (result field) |

The two original WAV playback implementations check for 48,000 Hz, 16 bits
and one channel. Their SoundService configurations use native port 1, rate
index 5, format index 0 and a 9,600-byte period. SET_PCM supplies the original
pool pointer, period size, two periods and the original circular-buffer flag.
The original consumer callbacks refill the completed 9,600-byte bank.

Each queue record is 56 bytes: callback at 24, command at 32, parameter pointer
at 40 and opaque callback context at 48. The callback receives the command and
the supplied opaque value. INIT, START and several results differ by model;
one model's raw lower AudioIF configuration is not passed to the other.

The binding generator emits the native functions, consumer table and a small
`audio_profile.h` from these profiles. Device addresses stay out of modules.

## Playback and ownership

`src/native_audio_driver.c` allocates a retained adapter from the native heap
on the first audio QUERY/CONFIGURE. The root occupies the existing core's heap
state. Discovery performs no device initialization, task creation or playback.
The linker still requires zero writable `.data` and `.bss` in ROM padding.
All these calls require a running module; audio adds no pre-Activate startup
hook or automatic sound. It introduces no firmware Flash operation.

The first START/DRAIN request lazily allocates one 16 KiB task stack and a
19,200-byte stereo mix scratch buffer, then creates an ordinary RTOS task.
The module receives `FX_BUSY` while the native session is being prepared.
The shared controller in `src/native_sound.c` performs:

1. Wait for native playback, recording and beep states to be idle.
2. Submit INIT and wait for its actual callback/result.
3. Request the original PCM pool and check space for both periods.
4. Submit the original circular PCM descriptor.
5. Once the module starts, prefill both banks and submit START with a retained
   core callback.
6. On each original completion, copy the transient bank pointer into an atomic
   notification. The normal Fenestra audio task renders into scratch, downmixes
   stereo to mono and cleans the completed native bank's cache range.
7. After all streams stop/drain, retain buffered content until both hardware
   bank completions have arrived. Submit STOP, wait for the original asynchronous
   device-completion callback and idle state, then submit CLOSE/standby.

Original AudioIF owns hardware routing and output volume. The adapter preserves
the current native mute/volume settings; per-stream Q16 gain is an additional
software gain. It does not force maximum volume or switch output routes.

Native operation callbacks and PCM callbacks execute in the original
SoundService task. They never execute a module, allocate, render, take a UI
lock or wait. The controller task does the mixing. Command records have fixed
command identities and remain in core-owned heap storage; an acknowledgment
does not make their storage safe to unload. The original dispatcher reads the
command after invoking its callback.

The software engine currently accepts up to 32 configured voices and a shared
2 MiB PCM allocation budget on these profiles, with up to 65,536 queued frames
per stream and 16,384 frames per WRITE. These are adapter configuration limits,
not a measured real-time voice count. QUERY reports actual limits and usage.

## Failure behavior and remaining limits

A busy native player causes START to return `FX_BUSY`. No STOP is sent to
take another native player's session. While running, the controller checks the
original state and consumer pointer before accessing completed banks. A foreign
consumer causes a terminal adapter error without stopping that consumer.

Duplicate/unknown bank notifications or a missed full ring cause an owned
session to stop; the controller does not write a bank that may be playing.
Operation/completion timeouts are 500 audio-task polling intervals. Outstanding
messages, callbacks, scratch and the task stack remain allocated through reboot
after an uncertain result. A failed STOP cannot authorize CLOSE or reclamation.
The default polling delay is one original RTOS tick, not a host millisecond.

STOP, close and owner Panic revoke software queues. PCM already copied into a
native bank can still sound for up to the buffered tail; they are not an
instantaneous physical-mute fence. DRAIN reports software consumption, while
the backend independently waits for native completion before stopping hardware.
An adapter fault is terminal until reboot; repeated requests do not overwrite
an outstanding native command.

The playback state machine is shared with original clients, not protected by
a newly invented exclusive device lease. Idle checks, original command-state
rejection and consumer verification provide cooperative ownership. Simultaneous
native movie/beep/recording transitions, output routing, tick latency, physical
cache/DMA timing and audible output still require camera validation. Microphone
capture, resampling and compressed-file decoding are outside this backend.

## Reproducible offline checks

The source-only tests require a C11 compiler and execute the real SDK, provider,
native controller/task entry and mixer against a synthetic RTOS/transport:

```sh
python -B -m unittest discover -s tests -p test_audio.py -v
```

`host/verify_native_audio.py` additionally requires Python `unicorn` and a
complete hash-matching original application image:

```sh
python -B host/verify_native_audio.py --target zr-c110 --firmware /path/to/original-zr-application --output build/native-audio-zr.json
python -B host/verify_native_audio.py --target z9-c532 --firmware /path/to/original-z9-application --output build/native-audio-z9.json
```

It executes original buffer-factory, submission, dispatcher, PCM callback and
asynchronous STOP instructions. It verifies target result offsets, buffer
capacity, refusal of INIT while playing, copied transient completion payloads,
original-owned event frees, STOP completion and caller-message retention.
Allocator/copy/queue primitives and synchronous AudioIF device operations are
explicit substitutes. Empty BSS models an idle initialized service; STOP's
device completion is supplied by the fixture. These checks establish the
message/state ABI and do not establish audible hardware playback.

Both complete firmware cores are also compiled/linked from their original
images to check references, ROM padding bounds and the prohibition on writable
ROM state. Candidate artifacts and generated binding bytes stay in ignored
`build/`; they are not a flashable package or a public firmware distribution.
