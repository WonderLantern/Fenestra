# `.wlm` module format

This specification accompanies `include/fenestra_module.h`, `host/module_header.py` and `host/pack_module.py`. Integers are little-endian. The body checksum is SHA-256, not a signature. The SDK packer defaults to v2 for service modules and emits v3 when stack sizes are specified.

## Fixed 128-byte header

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 8 | ASCII `FENESTRA` |
| 8 | 4 | Format version: 1, 2 or 3 |
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
| 116 | 4 | v2/v3 entry ABI: 1 (copied services); reserved zero in v1 |
| 120 | 4 | v3 native task stack bytes; reserved zero in v1/v2 |
| 124 | 4 | v3 user stack bytes; reserved zero in v1/v2 |

Legacy v1 service-entry inference uses capability bits for compatibility. New packers should explicitly emit v2 or v3: bytes 116–127 are not all reserved in those formats.

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

## Relocation table

The relocation table immediately follows the segment table. Each 16-byte entry has format `<IIQ`: destination image offset, relocation type and target image offset. Only type 1 is supported: a 64-bit pointer relative to the loaded image base. Destinations must be unique, aligned to 8 bytes and within writable segments. Targets must lie within readable image memory. External symbols are resolved through services rather than arbitrary symbol-name binding.

## Stacks, SIMD and capacity

Both v3 stack fields must satisfy their individual ranges and be aligned to 4096 bytes. Native stacks range from 32768 to 1048576 bytes; user stacks range from 4096 to 1048576 bytes. v1/v2 default to 65536 bytes each. Unsupported required capabilities cause rejection. `--simd` adds the SIMD capability requirement; clearing that bit does not make SIMD code compatible with a core that lacks context preservation.

The current native worker limits packages to 1048576 bytes and loaded images to 4194304 bytes. Changing these limits requires coordinated loader, worker preflight, memory-budget, documentation and boundary-test changes.

## Implementing a packer or validator

Validate lengths and descriptor boundaries before hashing the body or allocating based on header values. Then check target identity, capabilities, segments and relocations. Reject truncated tables, integer overflow, overlapping segments, non-executable entries, unknown entry ABIs and nonzero reserved fields.

Use `host/module_header.py:validate_package` for host preflight and compare its behavior with the C loader. `module.json` records build provenance and preflight results; it is not read during camera-side discovery. Installing a module does not require recompiling Fenestra.
