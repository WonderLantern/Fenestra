# Conflicts between modules

Module-to-core compatibility and module-to-module resource conflicts are
different checks. Compatible modules can still conflict when they change the
same table, install a hook on the same slot, or control the same hardware.
Installed files alone do not imply a conflict: arbitration happens when a
running module acquires a resource, before native attachment or modification.
There is no module-name blacklist or installed-module manifest.

ZR and Z9 native owners use one registry in their activated controller. The
registry records the owner ID, generation, sharing mode and resource identity.
Claims are in RAM, embedded in stable core-owned leases or hook policies.
There is no fixed global slot count or allocation on the arbitration path;
ordinary lease/policy memory budgets still apply. Closing a menu is unrelated
to resource ownership.

## Sharing rules

| Resource | Ordinary OPEN | Conflict identity |
| --- | --- | --- |
| Native function | Shared | Entry address, including aliases with different names |
| Read-only table | Shared | Actual byte range |
| Writable table | Exclusive | Actual byte range, including partial overlaps and aliases |
| Provider/device/event service | Shared | Binding name; provider entry address is not a device identity |
| Profile with `FX_BIND_EXCLUSIVE` | Exclusive; shared override refused | Its normal identity above |
| Semantic resource group | Explicit shared or exclusive | Agreed group name |
| Installed hook | Exclusive target slot; shared borrowed functions | Actual slot/function addresses |

Two shared claims can coexist. A claim from another owner or generation
conflicts if identities overlap and either claim is exclusive. Independent
handles within one owner/generation can coexist; each remains separately
counted so closing one cannot release another's protection. Disjoint byte
ranges and different provider services do not conflict merely because their
implementations share a dispatcher.

Calling a common function does not inherently conflict. A non-reentrant native
session, or one that changes shared state over several calls, should use
explicit exclusivity or an agreed resource group. Static compatibility metadata
cannot infer arbitrary native side effects.

## Module SDK

All calls use the ordinary asynchronous copied-request/reply protocol. Submit
one request, return, then take its reply on a later invocation.

```c
/* Explicitly own a non-reentrant binding until FX_BIND_CLOSE. */
fx_sdk_open_access(&client, api, "example.capture", 1u,
                   FX_RESOURCE_EXCLUSIVE);

/* An agreed name covers a pipeline reached through different APIs. */
fx_sdk_reserve(&client, api, "org.example.sensor.capture",
               FX_RESOURCE_EXCLUSIVE);

/* Read the last conflict observed by this owner. */
fx_sdk_resource_conflict(&client, api);
```

These are alternatives at their respective state-machine steps, not three
submissions in one invocation. A successful open/reservation returns the normal
handle in `reply.value[0]`; close it with `FX_BIND_CLOSE`. Reserve a semantic
group before native side effects and hold it for the entire operation. Module
authors using different sensor/ISP APIs must agree on the same group name.
Group reservations do not magically associate undocumented hardware aliases.
For a multi-resource transaction, use a common exclusive group or acquire in
an agreed order and release earlier claims if a later acquisition fails.

At the wire level, `FX_BIND_OPEN` keeps arguments version/independent-instance
and uses `arg[3]`: 0 automatic, 1 shared, 2 exclusive. Automatic opens retain the
previous handle-reuse behavior; explicitly requesting another mode for an
existing handle returns `FX_STATE` until it is closed or independently opened.
`FX_BIND_RESERVE` (10) takes a 64-byte NUL-terminated group name, `arg[1]`
independent-instance, `arg[2]` mode and zero `arg[3]`. These virtual reservations
never call a native provider. Shared writable-table handles permit copying,
but refuse STORE and hook installation.

`FX_CONFLICT` (14) is a normal SDK result, not Panic. Its reply data contains
`FxResourceConflict`: ABI/size, requester and holder IDs/generations, requested
and held modes, and both resource names. Modules can display this copied data
using their normal WonderLantern UI. `FX_BIND_CONFLICT` (11), with empty payload
and zero other arguments, returns that owner's last diagnostic; holder 0 means
none. `FX_BUSY` instead means transient lock contention and has no invented
holder. A denied acquisition never invokes the native provider, consumes a
published handle, evicts another module, or changes its ownership.

Older cores reject the new operations/nonzero reserved OPEN field. Probe
`FX_BIND_CONFLICT` before relying on explicit coordination; do not silently
fall back to an unprotected native session. Standalone platform fixtures must
attach their own shared `owner.conflicts` registry. Unconfigured fixtures keep
legacy automatic opens, but reject explicit OPEN modes and reservations.

## Release and callbacks

Normal CLOSE releases a claim after native detach acknowledgement. BUSY,
uncertain attach, failed detach and Panic retain ownership. If native detach
succeeds but the registry lock is contended, a retry only completes bookkeeping;
it does not repeat the acknowledged native detach. Normal owner STOP releases
acknowledged resources; FINISH or hiding a page is not an unload operation.
Panic does not claim that hardware stopped safely; remaining claims survive
until the existing recovery/reboot path.

Committed hooks retain independent claims for the actual target slot and
borrowed native-call targets, even if the module closes its original table or
function handles. Target restoration, closed admission and draining precede
claim release. A contended cleanup keeps policy storage and claims alive for
retry. Existing reboot-lifetime trampoline retention remains unchanged.

## Boundaries and validation

This coordinates cooperating Fenestra modules. It does not suspend the stock
firmware, intercept arbitrary pointer writes, or infer that two unrelated API
names reach the same physical device. Native escape access still requires the
module author to respect these contracts. No claim writes flash or changes
startup behavior; both native profiles attach the registry when module owners
are created after activation.

`tests/test_module_conflicts.py` compiles and exercises the actual C platform,
lease store, registry, copied SDK and hook implementation. Cases cover shared
calls/reads, exclusive aliases and overlapping tables, distinct providers,
semantic groups, uncertain/failed attachment, detach retries, normal STOP,
Panic retention, generations, hundreds of owners/growing lease storage and
hook lifetime after closing source handles. Native IO is synthetic; these are
not physical-camera or full-system QEMU qualification results.
