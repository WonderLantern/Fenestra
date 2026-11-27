# File and resource substitution

A consumer opens a resource by its canonical binding name. When a replacement has been published, the consumer gets a read-only handle to that version. Without a published replacement, it uses its ordinary original resolver. The card supplies the replacement bytes. Publication changes the runtime lookup and leaves the original file or embedded resource intact. A restore affects future opens. Any reader holding a previous handle keeps seeing that version until it closes.

## Provider integration

1. Register the exact resource name, existing immutable bytes and digest, a publisher owner, format fingerprint and maximum size with `fx_overlay_register`. Provide a RAM allocator and a policy that checks the consumer's real lease and compatibility.
2. After Activate, run `fx_overlay_activate` in a worker, yielding between `FX_BUSY` responses. Activate checks the original against the pinned digest.
3. Call `fx_overlay_card_begin` with the expected length and replacement SHA-256 and a basename inside the provider's card directory, such as `FENESTRA/OVERLAY/`. Run `fx_overlay_bulk_step` in the worker. It reads at most 64 KiB per call. EOF, close and digest must all pass before publication. A malformed or missing replacement leaves the current selection intact. A close with uncertain ownership faults the store and retains its resources.
4. With the consumer lease, publish the prepared version against the expected revision. New opens call `fx_overlay_file_open`. On `FX_EMPTY`, resume the original resolver. Propagate other errors. Use the returned handle for read, seek, size and close. For a mapped or DMA consumer, keep a native view lease through its last confirmed use.
5. To restore the original, obtain the lease and call `fx_overlay_restore`. During shutdown, stop incoming requests and drain the consumers before reclaiming snapshots. If ownership is uncertain, retain them until reboot.

The adapter receives exact binding names such as `camera.recognition.weights`. A platform can map a specific file or an embedded resource to a binding. The card filename is just the source of replacement bytes. Module-supplied names never register a new system target. The card path accepts a single filename within a directory chosen by the provider. Module payloads, model selection and any tensor or format validation belong to the provider and feature module.

A same-name replacement only affects consumers using this resolver. The current ZR C1.10 and Z9 C5.32 card builds include the shared runtime and native adapter API. They do not yet route original file opens or model loads through it. A model provider must verify the exact path, original digest, format and consumer lifetime before enabling that route. The reserved SDK substitution permission is separate from SIMD. It is outside the default module capability mask. A provider must authorize the publisher for the exact registered target.
