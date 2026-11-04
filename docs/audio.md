# Module audio playback

Ordinary `.wlm` modules use the version 1 named device `camera.audio.output`.
The shared service implements signed 16-bit little-endian PCM queues, mono to
stereo expansion, stereo mixing, per-stream gain, pause, drain and stop.
It uses the existing binding grant (`FX_SDK_BINDING`), copied service exchange
and owner lifecycle. There is no audio-specific module type or new opcode.

**Hardware status:** neither published ZR C1.10 nor Z9 C5.32 profile currently
registers a verified PCM output adapter. On these profiles the binding can be
opened and queried, but `capabilities` is zero and CONFIGURE returns
`FX_CAPABILITY`. This contribution implements and tests the software path; it
does not establish audible speaker/headphone playback on a camera. Codec
control-register serialization alone is not evidence of a PCM playback ABI.
Microphone capture, compressed-file decoding and resampling are not implemented.

## Module SDK

Include [fenestra_audio_sdk.h](../include/fenestra_audio_sdk.h). All functions
submit asynchronously: return from the entry, then collect the matching
`FxServiceReply` with `fx_sdk_take` before submitting another request.

1. Declare APP, EXTENSION or SERVICE as appropriate. Open
   `FX_AUDIO_OUTPUT_BINDING` version `FX_AUDIO_ABI`.
   Use `fx_sdk_open_instance` for independent voices; `fx_sdk_open` deliberately
   reuses an existing binding.
2. Call `fx_sdk_audio_query`. Decode `FxAudioInfo` from the reply, checking its
   ABI, byte size and `FX_AUDIO_CAP_OUTPUT`. An older core may return
   `FX_CAPABILITY` when opening the name. Handle either unavailable case.
3. Call `fx_sdk_audio_configure` with `FxAudioConfig`. Use the queried sample
   rate, `FX_AUDIO_S16_LE`, one or two channels, an explicit queue size and
   gain from 0 (mute) through `FX_AUDIO_GAIN_ONE` (unity).
4. Prequeue PCM with `fx_sdk_audio_write`, then START. A frame contains one
   sample per input channel, not one byte or necessarily one sample.
5. Refill before the queue empties. WRITE returns `FX_BUSY` if the entire block
   does not fit; no part is accepted. Advance the source cursor only after
   `FX_OK`. Split blocks larger than the queried maximum or queue capacity.
6. DRAIN starts/resumes playback of the remaining queue and refuses further
   writes. Retry DRAIN after `FX_BUSY` until `FX_OK`, then close the binding
   using `fx_sdk_platform(..., FX_BIND_CLOSE, ...)`.
   STOP discards queued data and resets to READY for reuse.

The config, write, info and status wire structures are defined in
[fenestra_audio.h](../include/fenestra_audio.h), with fixed-size ABI assertions.
No native address or PCM buffer handle is returned to a module.

| Operation | Request payload / `arg[3]` | Behavior |
| --- | --- | --- |
| QUERY | None / 0 | `FxAudioInfo` in reply data, including live usage and explicit stream/byte limits. |
| CONFIGURE | `FxAudioConfig` / 0 | Allocate one queue per binding. A second CONFIGURE returns `FX_STATE`; close and reopen to change format. |
| WRITE | `FxAudioWrite` / 0 | Copy a whole block into the queue; `FX_BUSY` has no acceptance side effect. |
| START | None / 0 | Play/resume READY, PAUSED or PLAYING. |
| PAUSE | None / 0 | Retain the queue without consuming samples. |
| GAIN | None / unsigned Q16 | Change this stream's gain; mixing saturates at the signed 16-bit output limits. |
| STATUS | None / 0 | `FxAudioStatus`: state, queue depth and cumulative submitted/rendered frames and underrun blocks. |
| DRAIN | None / 0 | Enter DRAINING, then DRAINED when empty. Returns `FX_BUSY` until software consumption completes. |
| STOP | None / 0 | Discard this queue, reset its cursors and return to READY. Counters remain cumulative. |

DRAIN can start a paused or READY queue. START and PAUSE reject DRAINING and
DRAINED; use STOP before writing/restarting a drained stream. GAIN is valid
while draining. STATUS and QUERY do not consume PCM.

PCM must reside in readable segments of the caller's loaded module image.
Keep it unchanged until the WRITE reply. The worker validates the range using
`fx_platform_module_transfer`; a source outside that image is rejected. Prepare
large inputs in an image-owned staging array, using existing memory/file
services when the source lives elsewhere. Payloads are copied in blocks up to
16,384 frames (64 KiB stereo), avoiding a separate 768-byte request per chunk.
Only the descriptor occupies the copied exchange.

On successful WRITE the queue owns a copy; the module may immediately reuse
its source. No renderer or device retains a module pointer. A failed wrapped
copy does not publish a partial block. Sequence replay returns the cached
reply rather than enqueueing the same block twice. Owner/generation and binding
lease rules apply exactly as for other services.

Calls are worker/task operations, not interrupt operations. Yield between
requests; do not spin inside a module entry. Choose a queue that covers the
module scheduler's actual refill latency. Underflow emits silence and increments
`underruns` once per short PLAYING render block; an exactly filled block and
intentional end-of-drain do not count as underruns.

## Platform output adapter

[fenestra_audio_output.h](../include/fenestra_audio_output.h) is the trusted
adapter contract. A platform adapter must first establish and acquire a native
playback path, including its original-firmware ownership/coexistence rules.
It then:

1. Supplies a core-resident `FxAudioOutput` and allocator/context that remain
   valid until reboot. Call `fx_audio_output_init` with sample rate, PCM byte
   budget, maximum streams, submit/queue sizes and render block size.
2. Calls `fx_audio_output_register` once. An alternate output cannot replace an
   installed one. Registration itself does not initialize a codec, write MMIO,
   start a task/IRQ, or perform any firmware Flash operation.
3. Calls `fx_audio_render` from its verified native playback callback to fill
   an adapter-owned stereo S16_LE buffer. The adapter manages device/DMA
   buffering, buffer ownership, cache synchronization and output timing.
4. Keeps the adapter, output and callback code resident. There is no public
   module callback registration or general output hot-unregister API.

The renderer allocates nothing, invokes no native/module callbacks, and never
waits for a lock. The worker and renderer use a try-lock; contention produces
silence with `FX_BUSY` and consumes no queued samples. On OK or BUSY the whole
valid output block is initialized. Invalid arguments/bounds return an error
without authorizing device submission. Render work is proportional to frames
times configured streams; the adapter must choose measured limits compatible
with its playback deadline. A byte budget alone is not a latency bound.

Stream storage is allocated on CONFIGURE, not discovery, and reclaimed on
binding close or owner stop. Limits are adapter-configured and reported, with
no global eight-voice array. Tests exercise 128 simultaneous software voices;
this is not a claim that either camera can meet that real-time load.

Closing or Panic revokes a stream before attempting the render lock.
`FX_BUSY` retains the cookie and memory for the normal worker retry. Once close
returns OK, no renderer retains that stream or its PCM, so that storage can be
freed. Other owners' streams remain intact.

Queue acceptance, software rendering, and audible completion are different
events. DRAIN proves consumption by the software renderer. A block already
copied to the adapter's hardware/DMA buffers can still play after STOP, close
or Panic; device flush and physical silence require an independently verified
adapter operation. This service does not claim to provide those operations.

## Example and checks

[examples/audio-tone/module.c](../examples/audio-tone/module.c) is an ordinary
APP that synthesizes a quarter-second 440 Hz triangle wave, queues it, drains
and closes. It handles unavailable audio by closing and finishing normally.

```sh
python -B host/build_sdk_module.py examples/audio-tone/module.c --id audio-tone --target zr-c110 --required 0x210000 --output-dir build/audio-zr
python -B host/build_sdk_module.py examples/audio-tone/module.c --id audio-tone --target z9-c532 --required 0x210000 --output-dir build/audio-z9
python -B -m unittest discover -s tests -p test_audio.py -v
```

The tests execute the real SDK, copied-message dispatcher, named binding
registry, provider, module-image range checks and software mixer with synthetic
PCM and a synthetic profile. They cover mixing/clipping/gain, ring wrap,
backpressure, failed copies, allocation/format limits, revocation under lock
contention, owner cleanup, request replay and the ordinary tone module with and
without a registered output. They do not validate physical audio, latency,
DMA/cache behavior, microphone capture or coexistence with native recording.
