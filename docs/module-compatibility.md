# Module compatibility checks

This page covers modules against the core. For conflicts between running
modules sharing hardware or modifying the same memory, see
[cross-module resource arbitration](module-conflicts.md).

Compatibility checks happen during discovery and again during preparation,
before image copying, executable publication or module entry. They apply to
both ZR C1.10 and Z9 C5.32 through shared code and the actual linked registry.
No installed-module catalog, module-name allowlist or firmware addresses are
required in a module.

The loader checks package geometry/integrity, format, core ABI, AArch64 ISA,
exact camera/firmware identity and required capabilities. Format v4 additionally
declares the copied-service ABI and mandatory named interface contracts:
name, kind, call ABI and exact version. See [the binary format](module-format.md).

The module manager shows a specific cause, such as `Wrong camera or firmware`,
`Service ABI mismatch`, `Missing: core.example` or `Version: core.example`.
Rejected modules remain listed and can be disabled. Discovery skips invalid or
incompatible packages and continues with other valid modules; card read/close
errors and allocation failures still abort the operation. Files are not renamed
or deleted automatically. Enabling a parked module validates it before rename.

## Build and check an ordinary module

The example opens and closes the interface it declares, then finishes:

```sh
python -B host/build_sdk_module.py examples/compatibility-probe/module.c --id compat-probe --target zr-c110 --required 0x210000 --require-binding core.schedule:device:device:1 --compatibility-profile build/core-zr/compatibility.json --output-dir build/probe-zr
python -B host/check_module.py build/probe-zr/compat-probe.wlm --core-profile build/core-zr/compatibility.json
```

For Z9 use `--target z9-c532` and its own core profile. The required mask in this
example grants QUERY and BINDING. Each `--require-binding` has the form
`NAME:KIND:ABI:VERSION`; repeat it for additional mandatory interfaces.
Kinds are `function`, `table`, `hook`, `device`; call ABIs are `u64x8`, `bytes`,
`events`, `device`, `aapcs64`. Names and versions must match what the module
actually opens. Existing interface-specific grants still apply.

`--require-binding` emits v4. `--compatibility` emits v4 with only a service ABI
requirement; `--service-abi` selects that exact version (default 1). Custom
native/user stack sizes remain supported. Omitting these options preserves the
existing v2/v3 output. The low-level `pack()` API accepts `requirements=None`
for older output or a list, including an empty list, for v4.

`--compatibility-profile` makes the builder validate the declarations before
writing the `.wlm`. Without it, structural preflight is recorded with
`compatibility_checked: false`; do not treat this as interface qualification.
The standalone checker requires a core profile and returns JSON with the full
failing interface name, reason and expected/actual values. Exit status is 0 for
compatible, 1 for an incompatible/invalid module and 2 for input/profile errors.
For legacy modules it reports `declared_contract_checked: false`: only their
existing header/geometry/capability contract can be checked.

## Core compatibility profiles

Both core builders write `compatibility.json` alongside their build outputs.
It is exported from the actual compiled `FxPlatform`/`FxNamedBinding` tables,
using field offsets emitted by the C compiler. Conditional services absent
from that build are not invented by a separate host-side list. The export
contains names, kinds, call ABIs, exact versions, capabilities, availability,
supported module formats, target identity and the compiled core's SHA-256.
It contains no native addresses, original instruction bytes, keys or captures.

Use the profile from the intended installed core build. A profile is descriptive
metadata, not a signature or independent proof of what a camera is running.
Camera-side checking always uses its own linked registry. Adding a separately
reviewed service, such as audio, automatically appears when that service is
linked into a core; the compatibility checker has no feature-specific branch.

## Lifetimes and boundaries

Requirement records stay in temporary package storage and are not copied into
the resident image or retained as package pointers. Checking does not allocate
native resources, call an original firmware function, create a task or invoke
an entry point. A failed check leaves the destination image untouched and the
output module descriptor unprepared. The loader retains a diagnostic in
`FxLoader.issue` for a preparation rejection.

The declaration is voluntary module metadata. Legacy modules are not assumed
to declare every service they use. Nor can a declaration prove that code calls
an ABI correctly, avoids bugs, has enough real-time capacity, or produces a
particular physical sensor/audio result. Availability means the linked adapter
exists; busy device state still requires normal runtime handling. This feature
does not change debugger admission or native-access rules.

The new loader accepts existing v1/v2/v3 packages. Older loaders reject v4;
their older error handling may stop Fenestra activation. Check the installed
core profile before installing a v4 package. No firmware flashing is performed
by these tools or by this change.

## Validation

```sh
python -B -m unittest discover -s tests -p test_module_compatibility.py -v
```

The tests compare the independent Python parser/checker with real C preparation,
directory discovery and manager UI paths. They cover old formats, v4 packing,
missing/mismatched/unavailable interfaces, firmware/ABI/capability errors,
malformed/duplicate records, metadata overlap, 256 requirements, resource-free
lookup, unchanged failed images and preservation of card failures.

With Python `unicorn` installed and a local native core build, this additional
check executes its compiled preflight/copy function in simulated RAM:

```sh
python -B host/check_module_core.py build/probe-zr/compat-probe.wlm --build-dir build/core-zr
```

It maps the Fenestra core and scratch RAM only. No module entry, original Nikon
function or complete camera system is mapped. This validates compiled ABI and
registry behavior, not full-system QEMU or physical camera operation.
