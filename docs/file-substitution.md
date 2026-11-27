# File and resource substitution

Modules can replace a file or selected ranges of a ROM resource while preserving the original stored bytes. Substitutions last for the current boot. Each replacement must be enabled again after reboot. Load a replacement before its consumer opens the resource, or arrange a consumer-controlled reload.

## Linux files

The Linux helper publishes a RAM snapshot over an existing file using a read-only bind mount. Any process in that mount namespace that subsequently opens the same path sees the snapshot through ordinary Linux file APIs. Existing file descriptors and memory mappings retain their previous contents. A program that already parsed a configuration or loaded a network must reload it.

Build with an AArch64 Linux compiler.

```sh
python host/build_linux_overlay.py --cc aarch64-linux-gnu-gcc --output-dir build/linux-overlay
```

Run through the existing Linux execution service after activation, with the caller's normal mount privileges. Use a RAM directory already present on the camera. The example paths are illustrative.

```sh
fenestra-file-overlay /card/FENESTRA/OVERLAY/settings.conf /etc/settings.conf SHA256 /run
```

The arguments are source file, existing target file, source SHA-256 and RAM staging directory. An optional final decimal byte limit overrides the default 64 MiB. The helper verifies that staging uses tmpfs or ramfs, copies and hashes the source, closes the writable descriptor, makes its staging mount read-only, then moves it onto the target. A missing source, bad digest, excess length or staging error leaves the currently visible target in place. An empty replacement is allowed. It preserves the original file's ordinary ownership and permission bits. Staging files and directories are removed after publication. Kernel references keep the published snapshot alive.

The helper uses standard Linux 4.14 mount APIs and runs in its caller's namespace. The provider must choose the namespace that contains the intended consumers. A namespace created after publication inherits the replacement, while an existing independent namespace may not see it. File xattrs and security labels require a platform-specific policy where the kernel uses them. Linux 4.14 tmpfs can swap, so the helper rejects active swap when staging on tmpfs. Enabling swap later would invalidate that RAM-only assumption.

Original and source files are opened read-only with O_NOATIME. All files created by the helper are on verified RAM storage. The helper uses the caller's existing privileges. Camera deployment and real recognition-model reload remain separate qualification work. Linux integration tests use a disposable mount namespace with a read-only lower directory.

## NOR ranges and version selection

A module may ship one FNR1 recipe containing multiple compatible firmware versions. Each version supplies the exact original application SHA-256, a source region, multiple non-overlapping replacement segments and replacement content. A provider gives the runtime the original read-only region and current application identity. The runtime selects one exact identity, checks the region geometry and verifies the original and resulting view before exposing reads.

Coordinates are byte offsets into the supplied original image. Start is inclusive and end is exclusive. Segments keep their original lengths. Different versions may describe different regions, segment locations and content. Overlap within a version and duplicate identities are rejected. An unsupported application identity returns FX_EMPTY so the caller can keep its original path.

Prepare a manifest beside the original reference and replacement fragments.

```json
{
  "schema": "fenestra.range-substitution/v1",
  "versions": [
    {
      "application_sha256": "<64 hexadecimal characters>",
      "original": "original-image.bin",
      "region": {"start": "0x1000", "end": "0x2000"},
      "segments": [
        {"start": "0x1100", "end": "0x1104", "file": "four-bytes.bin"},
        {"start": "0x1800", "end": "0x1808", "file": "eight-bytes.bin"}
      ]
    }
  ]
}
```

Add more version records for other supported images. An optional original_sha256 pins the declared original region before packaging. Build the recipe with the common entry point.

```sh
python fenestra.py range-substitution --manifest replacement.json --output module-data.fnr
```

The packer stores hashes and replacement segments. The original image stays local. A module can bundle the recipe as data or load it from the card. The runtime borrows immutable provider-owned recipe bytes and an original read-only region. Call fx_overlay_ranges_init, then fx_overlay_ranges_verify in a worker, yielding on FX_BUSY. Verification processes at most 64 KiB per call. Once verified, fx_overlay_ranges_read composes reads from the original bytes and replacement spans. Random reads use a binary search over sorted segments. RAM usage scales with replacement data and descriptors.

A direct CPU load or instruction fetch from the old NOR mapping still follows that mapping. A platform adapter must publish RAM shadow pages through the applicable MMU or reroute the resource consumer to the range reader. DMA consumers need their own mapping and cache handling. These physical ZR/Z9 mapping and consumer bindings are still outstanding. Retain both the recipe and original mapping until all users have finished.

## Shared RTOS snapshot service

The shared Core includes the RAM snapshot store, card staging, exact-name resolver and range reader. This also supports embedded resources without a filesystem path.

1. A provider registers the canonical resource name, original bytes and digest, publisher owner, compatibility fingerprint and size limit. It supplies a RAM allocator and consumer-lease validation.
2. After Activate, fx_overlay_activate verifies the original in bounded worker steps.
3. fx_overlay_card_begin and fx_overlay_bulk_step stage and verify a card replacement. Publish the completed snapshot using the expected revision and a real consumer lease.
4. A hooked consumer calls fx_overlay_file_open. FX_EMPTY resumes its original resolver. Other errors propagate. It uses the returned handle for read, seek, size and close.
5. Restoring a target affects future opens. Old handles retain the prior snapshot. Use fx_overlay_card_reset to reuse a completed loading job after publication, cancellation or rejection. It refuses to abandon an unpublished staged snapshot or an uncertain file close.
6. On module shutdown, close admission and drain consumers before releasing resources. Uncertain native references remain retained until reboot.

ZR and Z9 still need qualified hooks at their original resource/model loading entry points. Linking these services alone does not redirect those consumers. The reserved SDK substitution bit is separate from SIMD and outside the default module capability mask. Module owner and target bindings determine access. A wrapper provider can expose the shared native API using the existing named-binding mechanism.

## FNR1 transport

All integers are little-endian. The total recipe size fits uint32. Version records follow the header. For each version, its segment table is followed immediately by its replacement bytes. Version blocks appear in record order. Parsers reject extra trailing data.

| Record | Fields |
| --- | --- |
| Header, 16 bytes | Magic FNR1, format 1, total bytes, version count |
| Version, 120 bytes | Application SHA-256, original-region SHA-256, result SHA-256, region offset u64, region bytes u32, segment count u32, table offset u32, reserved zero u32 |
| Segment, 16 bytes | Start relative to region u32, exclusive end u32, replacement-data offset u32, reserved zero u32 |

Hashes detect data and compatibility mismatches. Retain the immutable source data for the lifetime of a range view. Multiple independent resource publications require consumer quiescence or a provider-owned transaction when the consumer needs a consistent bundle.
