# `.wlm` module format

This specification accompanies `include/fenestra_module.h`, `host/module_header.py` and `host/pack_module.py`. Integers are little-endian. The body checksum is SHA-256, not a signature. The SDK packer defaults to v2 for service modules, emits v3 for explicit stacks, and emits v4 for declared compatibility requirements.

## Fixed 128-byte header

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 8 | ASCII `FENESTRA` |
| 8 | 4 | Format version: 1, 2, 3 or 4 |
| 12 | 4 | Header length: must be 128 |
| 16 | 4 | ABI: currently 1 |
| 20 | 4 | ISA: currently 1 (AArch64) |
| 24 | 32 | Target firmware SHA-256 from the profile's `module_identity` |
| 56 | 32 | SHA-256 of `[128, end of file)` |
| 88 | 4 | Total package bytes |
| 92 | 4 | Total loaded-image bytes |
| 96 | 4 | Entry offset within the loaded image, aligned to 4 bytes |
| 100 | 4 | Segment count: 1–8 |
| 104 | 4 | Relocation count: 0–128 |
| 108 | 8 | Required capability bitmap |
| 116 | 4 | v2/v3/v4 entry ABI: 1 (copied services); reserved zero in v1 |
| 120 | 4 | v3/v4 native task stack bytes; reserved zero in v1/v2 |
| 124 | 4 | v3/v4 user stack bytes; reserved zero in v1/v2 |

Legacy v1 service-entry inference uses capability bits for compatibility. New packers should explicitly emit v2, v3 or v4: bytes 116–127 are not all reserved in those formats. The new loader preserves v1/v2/v3 behavior. Older loaders reject v4; use the matching core compatibility profile before installing one.

Service-entry packages in v1/v2/v3 implicitly require copied-service ABI 1. v4 declares the exact service ABI in its footer.

ZR uses the complete original update-package digest for `module_identity`; Z9 uses the original MAIN application digest. Read the identity from the matching profile rather than applying one convention to both targets.

## Segment table

The table starts at offset 128. Each 24-byte entry has format `<6I`:

1. Offset in the loaded image.
2. Offset of data in the package.
3. File-backed data length.
4. Memory length, including any zero-initialized tail.
5. Permissions: R=1, W=2, X=4.
6. Alignment: a power of two from 1 through 4096.

Memory segments and file-backed ranges must not overlap. Segment data cannot overlap headers or descriptor tables. Every segment must be readable; W+X is prohibited. Segments with different permissions cannot share a native page. The entry must be in file-backed executable data, not BSS.

For v4, file-backed ranges and descriptor tables must also end before the compatibility records. Compatibility metadata is not copied into the loaded image.

## Relocation table

The relocation table immediately follows the segment table. Each 16-byte entry has format `<IIQ`: destination image offset, relocation type and target image offset. Only type 1 is supported: a 64-bit pointer relative to the loaded image base. Destinations must be unique, aligned to 8 bytes and within writable segments. Targets must lie within readable image memory. External symbols are resolved through services rather than arbitrary symbol-name binding.

## Stacks, SIMD and capacity

Both v3/v4 stack fields must satisfy their individual ranges and be aligned to 4096 bytes. Native stacks range from 32768 to 1048576 bytes; user stacks range from 4096 to 1048576 bytes. v1/v2 default to 65536 bytes each; v4 writes these defaults explicitly unless overridden. Unsupported required capabilities cause rejection. `--simd` adds the SIMD capability requirement; clearing that bit does not make SIMD code compatible with a core that lacks context preservation.

The current native worker limits packages to 1048576 bytes and loaded images to 4194304 bytes. Changing these limits requires coordinated loader, worker preflight, memory-budget, documentation and boundary-test changes.

## v4 compatibility records and footer

Append zero to 256 records, immediately followed by this fixed 24-byte footer at EOF. Both records and footer are covered by the existing body SHA-256 and total package length. Header length, segment-table location and relocation layout remain unchanged.

| Footer offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 8 | ASCII `FXCMPT01` |
| 8 | 4 | Requirement count: 0–256 |
| 12 | 4 | Record size: exactly 80 |
| 16 | 4 | Required copied-service ABI: positive exact version |
| 20 | 4 | Reserved: zero |

Records start at `package_bytes - 24 - count * 80`. Every record is:

| Record offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 64 | Case-sensitive interface name: 1–63 printable ASCII characters, NUL terminated with zero padding |
| 64 | 4 | Kind: function=1, table=2, hook=3, device=4 |
| 68 | 4 | Call ABI: u64x8=1, bytes=2, events=3, device=4, AAPCS64=5 |
| 72 | 4 | Required interface version: positive exact version |
| 76 | 4 | Reserved: zero |

Duplicate names, unknown values, invalid padding, truncated/overlapping records and metadata referenced by a segment are rejected. The loader validates geometry and integrity before looking up declared interfaces, and checks the service ABI, interface kind, call ABI, exact version, availability and required grants before changing the image. Interface lookup reads the linked registry; it must not OPEN, ATTACH or invoke a device.

Required named interfaces also need `FX_SDK_BINDING` and the interface's existing capability grants in the module's required bitmap. Version matching is exact, consistent with `FX_BIND_OPEN`. Optional interfaces should still use runtime queries and normal error handling. The contract concerns static core/profile interfaces; modules can continue declaring their own interfaces at runtime.

Standalone C callers use `fx_module_inspect_environment` / `fx_module_prepare_environment` with an explicit `FxModuleEnvironment`. Default native firmware wrappers use their linked platform environment. A standalone caller without an environment rejects v4 instead of silently skipping the declaration.

## Implementing a packer or validator

Validate lengths and descriptor boundaries before hashing the body or allocating based on header values. Then check target identity, capabilities, segments and relocations. Reject truncated tables, integer overflow, overlapping segments, non-executable entries, unknown entry ABIs and nonzero reserved fields.

Use `host/module_header.py:validate_package` for host preflight and compare its behavior with the C loader. `module.json` records build provenance and preflight results; it is not read during camera-side discovery. Installing a module does not require recompiling Fenestra.

Host preflight without an environment validates v4 geometry but reports `compatibility_checked: false`. Use [the compatibility checker](module-compatibility.md) with a linked core's `compatibility.json` to validate interfaces. A declared contract is not a proof of module correctness or physical feature support.
