# Contributing and releases

## Licensing

Project code and documentation use [CC BY-NC-SA 4.0](../LICENSE), with the [commercial photography permission](../EXCEPTIONS.md). Submit original contributions only if you can grant them under those terms. Retain third-party license notices and establish compatibility before incorporating third-party code. The project license does not cover original camera firmware, keys, or vendor hardware documentation.

CC BY-NC-SA 4.0 requires attribution, noncommercial use, and ShareAlike licensing of shared adaptations. The additional permission allows paid photography, commercial filmmaking, and internal business media production. It does not authorize selling covered software or modules, or providing paid flashing services to third parties. Independently authored modules remain subject to their authors' rights; a module incorporating project code must comply with the license governing that code.

CC BY-NC-SA 4.0 does not contain software-specific source-delivery requirements or a patent license. See the [Creative Commons software FAQ](https://creativecommons.org/faq/#can-i-apply-a-creative-commons-license-to-software). The standard license text is unchanged; the additional photography permission is provided separately in EXCEPTIONS.md.

## Submitting changes

Describe the problem, reproduction conditions, resulting behavior, and relevant validation in a pull request. Evaluate shared-core changes for both ZR and Z9. Put platform differences in profiles or platform adapters. Modules should use existing services where possible. New interfaces need explicit versions, capability queries, ownership, error codes, concurrency rules, and interrupt-context requirements.

Update headers, packers, tests, and tutorials together when changing an interface or format. Use synthetic inputs in examples and tests. If reproduction requires original firmware, document its identity digest and local input steps without committing the firmware.

## Repository contents

Include project source, the SDK, module examples, maintained documentation, and necessary compatibility metadata. Keep original update packages, components, dumps, models, keys, device credentials, and generated original-instruction files local. Remove absolute workstation paths, device serial numbers, and private network details before submission. Camera paths such as `A:\FENESTRA` and operating-system API paths are interface definitions and must remain accurate.

## Preparing a release

1. Pin the source revision, run relevant tests, and confirm the target profiles and original firmware identities.
2. Create a source tree from an explicit file inventory, excluding `.git`, local inputs, build outputs, and caches.
3. Include LICENSE, EXCEPTIONS.md, NOTICE, and user documentation. Update SOURCE-INVENTORY.json.
4. Run `python -B verify_source.py --distribution`, then create the ZIP and SHA-256 file.
5. Read the archive back and compare its file list and bytes with the source tree.
6. Publish patches separately with matching original-firmware identities, usage instructions, and compatibility limits. Include the corresponding project source alongside executable release artifacts as part of the project release process.

Release notes should explain supported models, feature changes, downloads, usage, and compatibility limits. Keep development journals and internal test records outside release materials. A later license change does not automatically revoke permissions already granted for earlier copies.
