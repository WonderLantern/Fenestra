# Repacker and ciphertext patches

## Two-stage workflow

`fenestra.py make-patch` verifies and decodes the original package, extracts the original application, compiles the source, runs independent checks, rebuilds the container, and generates a ciphertext patch. `patcher.py` only verifies the original package and applies that patch. Applying a patch requires no keys, LLVM toolchain, cryptographic library, or camera connection.

The source distribution, prebuilt patch, and original firmware are separate files. The source distribution contains no original firmware or key material. A `.fxpatch` contains replacement ranges for a specified result and must be used with its matching original package.

## Original package identities

| Profile | Bytes | Original SHA-256 |
| --- | ---: | --- |
| `zr-c110` | 117159984 | `43bf4f5f1278fa6b0a551ceb6fa4ee749dc74bbc8a5ef4fba19b30062184e546` |
| `z9-c532` | 101846611 | `2f13e75d38cd5b65da5b1f21a7828bc8a65ab17dc4c1b435b040ee62d2ed1d81` |

Generator profiles pin these identities. The patcher independently verifies the complete input SHA-256 from the manifest, rather than accepting a filename, size, or partial header. An input hash failure stops processing before replacement payloads are read or an output is created.

## User-supplied material

### ZR C1.10

- `--key`: 48 binary bytes: a 32-byte AES-256 key followed by the 16-byte initial CTR counter/IV.
- `--digest-suffix`: an 8-byte binary digest suffix. Neither the source nor examples embed this value.
- `--envelope-key`: an RSA-2048 public key in PEM format, required for exact r3 reproduction. An unencrypted private-key PEM is also accepted, but only its public component is used. No TPM access is required. The distribution supplies neither the public modulus nor a private key.

After decoding with the supplied material, both the complete body and original application digests must match. Correct material length alone is insufficient. The original package's digest, reverse fields, and 12-byte tail are also verified.

The body uses AES-CTR. Its first 32 bytes remain clear but consume the first two counter blocks. Directory/component CRCs are recomputed from actual plaintext. The outer header occupies `[0,608)` and the body starts at offset 608. The body SHA-1 input includes the supplied digest suffix; the tail is generated using the established digest-seed rule.

### Z9 C5.32

`--key` supplies a 16777216-byte periodic XOR stream. Stream position zero corresponds to body position zero after the outer header. This is neither a ZR AES key nor a signing private key. Users with a matching original plaintext/ciphertext pair can use `z9_update_codec.original_twins` to validate that pair and derive the stream locally.

The generator verifies the complete decoded body, component CRCs, and original MAIN identity. Outer-digest tables and seeds are read locally from the supplied original MAIN; the source includes their locations and algorithms only. Non-MAIN components and the directory must remain unchanged. The package CRC initial value is `0xcd18`; ZR body rules do not apply.

## Exact r3 reproduction

The default is `--release r3`. The pinned results are the firmware files inside these reference archives:

| Reference release | Member | Result SHA-256 |
| --- | --- | --- |
| `Fenestra-C110-core-20261029-r3.zip` | `ZR__0110.bin` | `977dd94b2da04a08fa0a2e0fe9334428c893321a722e0938fdee5df670166b2e` |
| `Fenestra-C532-core-20261029-r3.zip` | `Z_9_0532.bin` | `b52499a2b7a83bf5f39c809563ef649705468c709371daa8633e4da4112fef9d` |

r3 mode performs the normal build and independent checks, normalizes runtime identity constants at specified link locations to their r3 values, and then requires the complete application image SHA-256 to match r3. Any other binary difference causes failure.

Build records retain source identity; `release-reproduction.json` records the normalization separately. Toolchain differences that change machine code prevent r3 patch generation. The reproduction toolchain is LLVM 19.1.1 targeting AArch64 bare-metal.

ZR r3 Field A uses randomized RSAES-PKCS1-v1_5 wrapping. Fresh random padding would produce a different package each time. Therefore `releases/r3.json` records the nonsecret padding bytes used for that release and the expected digest. With the supplied RSA public key and body material, the generator reproduces the same envelope. The padding is not a key; this file contains no body secret, modulus, private key, or encrypted Field A.

This reproduction routine only accepts the pinned r3 envelope digest. It is not a general encryption interface for other secrets or key rotation. The complete final update package must also match the result digest above; otherwise no patch is produced.

Use `--release custom` for modified source that is not intended to reproduce r3 byte-for-byte. ZR custom mode preserves the original key envelope and reuses the key/IV verified against the complete original body, then recomputes the other integrity fields. Test custom builds according to their actual changes.

## `.fxpatch` format v1

The archive uses ZIP_STORED, with neither ZIP compression nor encryption. Processing does not extract members to the filesystem. Members are `manifest.json` and sequential payloads such as `ranges/0000.bin`.

The manifest contains exactly these fields:

| Field | Meaning |
| --- | --- |
| `schema` | `fenestra.ciphertext-patch/v1` |
| `target` | Profile ID |
| `firmware_bytes` | The identical length of the original and result |
| `original_sha256` | Complete original package SHA-256 |
| `result_sha256` | Complete patched package SHA-256 |
| `ranges` | Replacement records ordered by ascending offset |

Each record contains `offset`, `length`, `before_sha256`, `after_sha256`, and `member`. Offsets are absolute positions in the encrypted update file, not application virtual addresses or decoded-body offsets. Ranges may cover body ciphertext or outer header fields. Length must be positive; ranges must be ordered, nonoverlapping, and within the file. Each member name must match the canonical path for its record index.

The generator locates differences in 4096-byte blocks, merges adjacent changed blocks, and trims equal leading/trailing bytes. File length is unchanged, and all bytes outside the replacement ranges remain original. A range may contain unchanged interior bytes to avoid excessive fragmentation.

## Patcher validation and output order

1. Read the size-limited input and ZIP metadata. Reject duplicate, unknown, compressed, or encrypted members and an oversized manifest.
2. Parse the manifest. Reject duplicate JSON keys, unknown schemas, and incorrect types.
3. Verify the original package length and complete SHA-256.
4. Verify every range, original-range digest, payload length, and payload digest; apply replacements in memory.
5. Verify the complete result SHA-256.
6. Write and fsync a temporary file in the output directory, publish it with a non-overwriting atomic hard link, and read back the result.

An existing or concurrently created destination causes failure instead of replacement. Output requires a local filesystem with hard-link support, such as NTFS or ext4. Do not use FAT/exFAT as the direct output location. Generate locally, then copy the result to a card separately if needed.

Limits are 256 MiB of firmware, a 2 MiB manifest, and 8192 ranges. Members are accessed through fixed names; archive member paths are never passed directly to the filesystem.

## Integrity, origin, and device behavior

Manifest digests establish byte consistency, not authorship. Anyone can construct a manifest; obtain source, patches, and expected digests through a trusted release channel. The r3 generator additionally pins original inputs and release results, while the standalone patcher is a general range applicator.

The fixed r3 results are the `.bin` files listed above. Patch tools generate files without connecting to a camera or performing an update. Runtime behavior also depends on the model, card configuration, loaded modules, and device state.
