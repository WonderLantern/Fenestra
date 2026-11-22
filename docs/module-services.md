# Generic module services

These services add reusable control and bulk-data paths to ordinary modules.
Open a named binding at version 1 through `fx_sdk_open`, consume the reply,
then call it through `fx_sdk_platform`. Service request/reply payloads remain
bounded by `FX_SERVICE_BYTES` (768). Large data belongs in a stream or validated
module memory. The headers define the wire structures and numeric operations.

## Providers and self information

`core.providers` supports QUERY, ENUM, LOOKUP, CALL and REMOVE. QUERY returns
provider ABI, metadata budget, used bytes and change revision; these byte counts
do not describe system-wide free RAM. ENUM takes the previous token as cursor.
LOOKUP matches a name, inclusive minimum/maximum versions and required feature
bits. CALL copies a `FxProviderRequest` followed by up to 752 argument bytes.

Native modules publish providers at runtime, without a core opcode/name
whitelist. A handler receives a stable 16-byte `FxProviderClient` containing ABI,
size, owner and generation. This is a value description, not a private core
owner object. Never retain a caller's request/reply pointers after returning.
Handlers may be concurrent and return a normal service result. A reply cannot
exceed 768 bytes.

Removal immediately rejects new calls and returns BUSY until in-flight calls
finish. A fresh publication gets a fresh token. Provider names are globally
unique while active. Publication/removal wakes existing clients; use LOOKUP
again after a wake. No automatic dependency solver or module-load ordering is
implied. Published code and contexts stay pinned until restart.

`core.self`, operation 0, returns `FxModuleSelfInfo`: owner/generation, purpose,
entry ABI, image address/size, capabilities and target identity. It does not
export private core structure layouts.

## Shared streams

`core.streams` registers, looks up and removes stream tokens. A stream is a
64-byte-aligned, module-owned writable range initialized with
`fx_stream_init`. The whole range must lie in validated writable image segments;
ordinary modules can reserve it in BSS. Registration does not map another
ordinary module's image or create arbitrary EL0 heap mappings. Native providers
access the same retained CPU address through the token descriptor.

`fenestra_stream.h` implements a single-producer/single-consumer ring with
fixed payload slots, a 128-byte header and a 32-byte per-slot descriptor.
Slot counts are powers of two. Producer/consumer claims carry epoch, position
and nonce so a cancelled claim cannot commit a later reservation. Publishing a
frame uses release/acquire ordering; no per-frame service request is needed.
Full rings return BUSY and increment dropped attempts rather than overwriting
unread data. Close blocks publication and allows committed data to drain.
`fx_stream_quiesced` concerns ring claims, not external DMA completion.

Set `clock_id`/`clock_hz` before publication with `fx_stream_set_clock`.
Timestamps may use monotonic nanoseconds, the architectural counter or a sample
index. The default is nanoseconds at 1 GHz; a producer must actually supply that
unit or select another clock. `fx_time_scale` converts ticks without overflowing
the intermediate counter multiplication. Neither the ring nor registration
performs device cache maintenance. A driver must finish DMA/cache work before
commit and acknowledge all users before reinitializing storage.

## Time, logs and continuations

`core.time` offers absolute millisecond one-shot/periodic timers. QUERY returns
milliseconds and frequency 1000, plus raw counter/frequency when the profile
provides them. CREATE returns a token; ARM takes `FxTimerRequest`. POLL returns
cumulative expirations since the last ARM and the next deadline, aggregating
missed periods. CANCEL disarms; RELEASE disarms and permits metadata reuse
with a fresh token. Old tokens become stale. Expiration wakes the ordinary
worker's WAIT path. These are cooperative worker timers, not hard IRQ deadlines.

`core.logs` offers per-owner structured records with sequence, time, severity,
owner/generation and up to 160 text bytes. Storage is allocated lazily, with 32
records per used client. QUERY reports oldest/next/dropped; READ below oldest
returns STALE so losses are visible. This is separate from the legacy 128-byte
last-log field and does not add a USB log-reader command or stack unwinder.

`FxSdkBatch` in `fenestra_async.h` is a module-owned continuation: it submits
one request at a time and copies each result before reusing the exchange.
Cancellation drains an already submitted result, then prevents subsequent
commands. It is not a multi-request wire protocol or a task scheduler.

The shared extension registry has a lazy 4 MiB metadata budget. Timers can reuse
released nodes; provider/stream metadata is retained until restart. Image and
stream storage is separate from that budget. Hundreds of installed modules do
not imply hundreds of simultaneously active tasks or unlimited RAM.

## Files and UI

`card.files` retains existing copied OPEN/READ/WRITE/CLOSE and 64-bit SEEK.
QUERY advertises available operations. READ_MODULE/WRITE_MODULE transfer an
entire validated image range directly through original file functions, without
768-byte copies. Return values include the actual transferred count and native
status. Partial transfers are valid; the caller advances by the returned count.

OPEN also accepts `FxFileOriginalOpen` for original flags/attributes; these are
not POSIX flags. DIRECTORY_OPEN/NEXT/CLOSE enumerate `A:\FENESTRA` with UTF-16
names, 64-bit sizes and original attributes. RENAME uses two validated card
names. Original rename semantics do not promise atomic replacement or
power-loss durability. Uncertain closes retain their resource and cannot be
blindly repeated. These adapters do not add stat, delete, fsync or internal
volume access; native extensions can provide additional protocols themselves.

UI remains the native PAGE/ACTION/CHOICE/SWITCH/DIALOG and document/edit system
with owner/revision/focus routing. Classic CHOICE holds eight options. The
`fenestra_ui_list_sdk.h` helper represents larger selections using paged native
ACTION rows with module-owned values, stable IDs and revision/serial fences.
Labels retain the existing 48-byte field and truncate at a UTF-8 boundary;
attach a document for long content. No raw touch/joystick device ABI is added.

## Audio providers

`fenestra_audio.h` and `src/audio.c` are an optional **module-side** provider
library. The core contains no codec, audio policy or physical PCM driver.
A native extension supplies `FxAudioDriver` operations and stream lookup,
then publishes any suitable provider name through the generic registry.

QUERY advertises directions, formats, rate/channel/period limits and operation
bits. OPEN binds a caller-owned stream to S16, packed S24, S32 or F32 periods.
START/STOP/STATUS/CONTROL/CLOSE govern the driver. BUSY leaves explicit pending
open/start/stop/close state so the same operation can continue. The driver must
make these retries continuation-safe. Changed OPEN parameters while pending
are rejected. Acknowledged CLOSE releases the device's owner; failed or pending
operations retain it. A running/starting/stopping device must stop before close.

The helper validates format sizes and stream ownership and preserves client
identity by value. It does not program a Nikon audio controller, add a mixer,
handle headphone routing or infer microphone gain registers. Providers must
declare concrete controls and their units. Synthetic PCM tests validate sample
data transport and pending-operation lifetime; they do not qualify a physical
capture/playback path.
