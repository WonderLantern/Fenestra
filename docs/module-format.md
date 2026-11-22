# `.wlm` module format

This specification accompanies `include/fenestra_module.h`, `host/module_header.py` and `host/pack_module.py`. Integers are little-endian. The body checksum is SHA-256, not a signature. The SDK packer defaults to v2 for managed service modules, emits v3 for explicit managed stack sizes, and v4 for native resident entry.

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
| 104 | 4 | Relocation count: 0–128 managed; 0–65536 native |
| 108 | 8 | Required capability bitmap |
| 116 | 4 | v2/v3: 1 (copied services); v4: 2 (native resident); reserved zero in v1 |
| 120 | 4 | v3 managed worker's RTOS task stack bytes; reserved zero in v1/v2/v4 |
| 124 | 4 | v3 managed EL0 stack bytes; reserved zero in v1/v2/v4 |

Legacy v1 service-entry inference uses capability bits for compatibility. New packers should explicitly emit v2, v3 or v4: bytes 116–127 are not all reserved. v4 is exclusively native entry ABI 2, with both stack fields zero; a v3 package cannot select native entry. Older cores reject v4.

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

The relocation table immediately follows the segment table. Each 16-byte entry has format `<IIQ`: destination image offset, relocation type and target image offset. Only type 1 is supported: a 64-bit pointer relative to the loaded image base. Destinations must be unique, aligned to 8 bytes and within writable segments. Targets must lie within readable image memory. v4 relocations must be strictly sorted by destination, allowing linear duplicate checking at the larger capacity. External functions use named bindings/providers; the package does not load shared libraries or arbitrary external symbol relocations.

## Stacks, SIMD and capacity

Both v3 stack fields must satisfy their individual ranges and be aligned to 4096 bytes. Native stacks range from 32768 to 1048576 bytes; user stacks range from 4096 to 1048576 bytes. v1/v2 default to 65536 bytes each. Unsupported required capabilities cause rejection. `--simd` adds the SIMD capability requirement; clearing that bit does not make SIMD code compatible with a core that lacks context preservation.

v4 native initialization uses the original Loader task's stack and mappings. It does not allocate either v3 stack. Native modules create their own tasks/stacks when needed and retain callback resources until reboot. See [native extensions](native-extensions.md).

| Entry | Package limit | Loaded image limit | Segments | Relocations |
| --- | ---: | ---: | ---: | ---: |
| Managed copied services | 1 MiB | 4 MiB | 8 | 128 |
| Native resident v4 | 32 MiB | 64 MiB | 8 | 65536, sorted |

These are parser/loader ceilings, not reserved or guaranteed available RAM. Images, package buffers, original page tables and module-created tasks all consume real heap. Native code starts only after image preparation and permission/cache publication. Static libraries can be linked into the image; TLS, an ELF interpreter and runtime shared-library dependencies are unsupported.

## Implementing a packer or validator

Validate lengths and descriptor boundaries before hashing the body or allocating based on header values. Then check target identity, capabilities, segments and relocations. Reject truncated tables, integer overflow, overlapping segments, non-executable entries, unknown entry ABIs and nonzero reserved fields.

Use `host/module_header.py:validate_package` for host preflight and compare its behavior with the C loader. `module.json` records build provenance and preflight results; it is not read during camera-side discovery. Installing a module does not require recompiling Fenestra.
