# SDK and binding reference

This index follows the SDK headers and profiles in this distribution. Refer to the headers for parameters, structure layouts, and error codes, and query capabilities and versions before use. These interfaces do not make arbitrary native addresses safe to call.

## Service families

| Service | Entry points | Contract |
| --- | --- | --- |
| Messages and bindings | `fenestra_sdk.h` | One in-flight request per client; retrieve the reply for the matching sequence. |
| UI and input | `fenestra_sdk.h`, `fenestra_document_sdk.h` | Owner, control ID, revision, and focus epoch. |
| Drawing | `fenestra_scene_sdk.h` | Submission acknowledgment does not confirm physical display scanout. |
| Files and substitution | `fenestra_file_sdk.h` | User files and owner lifetime; no system ROM rewrite. |
| Mapping and MMU | `fenestra_mapping_sdk.h`, `fenestra_mmu_sdk.h` | Address spaces, permissions, caches, invalidation, and reclamation. |
| Tasks, locks, and events | `fenestra_tasks_sdk.h`, `fenestra_mutex_sdk.h`, `fenestra_events_sdk.h` | Context and blocking rules depend on the operation. |
| Video configuration | `fenestra_video_sdk.h` | General record/configuration services do not add sensor capabilities by themselves. |
| Resources and communication | `fenestra_accounting_sdk.h`, `fenestra_bus_sdk.h` | Owner/generation, shared budgets, drops, and congestion handling. |

## Before calling a service

Query the platform and binding descriptors. Check version, kind, ABI, availability, and capabilities. Reject unsupported versions or use a documented fallback. Native functions use the calling convention declared by the profile. A generic integer-call wrapper cannot replace an API requiring object state, floating-point arguments, or a particular interrupt context.

## SDK function index

### fenestra_accounting_sdk.h

Source: [fenestra_accounting_sdk.h](../include/fenestra_accounting_sdk.h)

- `fx_sdk_account`

### fenestra_bus_sdk.h

Source: [fenestra_bus_sdk.h](../include/fenestra_bus_sdk.h)

- `fx_sdk_bus_query`
- `fx_sdk_bus_enum`
- `fx_sdk_bus_capacity`
- `fx_sdk_bus_reserve`
- `fx_sdk_bus_named`
- `fx_sdk_bus_send`
- `fx_sdk_bus_poll`
- `fx_sdk_bus_remove`

### fenestra_critical_sdk.h

Source: [fenestra_critical_sdk.h](../include/fenestra_critical_sdk.h)

- `fx_sdk_critical_call`

### fenestra_document_sdk.h

Source: [fenestra_document_sdk.h](../include/fenestra_document_sdk.h)

- `fx_sdk_document`

### fenestra_events_sdk.h

Source: [fenestra_events_sdk.h](../include/fenestra_events_sdk.h)

- `fx_sdk_event`

### fenestra_file_sdk.h

Source: [fenestra_file_sdk.h](../include/fenestra_file_sdk.h)

- `fx_sdk_file_seek`
- `fx_sdk_file_tell`

### fenestra_mapping_sdk.h

Source: [fenestra_mapping_sdk.h](../include/fenestra_mapping_sdk.h)

- `fx_sdk_mapping`
- `fx_sdk_mapping_root`
- `fx_sdk_mapping_locked`

### fenestra_mmu_sdk.h

Source: [fenestra_mmu_sdk.h](../include/fenestra_mmu_sdk.h)

- `fx_sdk_mmu_context_write`
- `fx_sdk_mmu_map`
- `fx_sdk_mmu_protect`
- `fx_sdk_mmu_unmap`
- `fx_sdk_mmu_query`

### fenestra_mutex_sdk.h

Source: [fenestra_mutex_sdk.h](../include/fenestra_mutex_sdk.h)

- `fx_sdk_mutex_config`
- `fx_sdk_mutex_create`
- `fx_sdk_mutex_call`

### fenestra_scene_sdk.h

Source: [fenestra_scene_sdk.h](../include/fenestra_scene_sdk.h)

- `fx_sdk_scene_capacity`
- `fx_sdk_scene_submit_images`
- `fx_sdk_scene_submit`
- `fx_sdk_scene_submit_global`

### fenestra_sdk.h

Source: [fenestra_sdk.h](../include/fenestra_sdk.h)

- `fx_sdk_submit`
- `fx_sdk_take`
- `fx_sdk_platform`
- `fx_sdk_open`
- `fx_sdk_wait`
- `fx_sdk_capacity`
- `fx_sdk_open_instance`
- `fx_sdk_declare`
- `fx_sdk_call_frame`
- `fx_sdk_memory_allocate`
- `fx_sdk_memory_release`
- `fx_sdk_memory_module`
- `fx_sdk_ui`
- `fx_sdk_ui_get`
- `fx_sdk_ui_hide`
- `fx_sdk_events`
- `fx_sdk_ui_queue`
- `fx_sdk_ui_control_capacity`
- `fx_sdk_ui_move`
- `fx_sdk_ui_remove`
- `fx_sdk_ui_reorder`
- `fx_sdk_ui_status`
- `fx_sdk_event_revision`
- `fx_sdk_text_event`
- `fx_sdk_ui_query`
- `fx_sdk_lifecycle`

### fenestra_semaphore_sdk.h

Source: [fenestra_semaphore_sdk.h](../include/fenestra_semaphore_sdk.h)

- `fx_sdk_semaphore_config`
- `fx_sdk_semaphore_create`
- `fx_sdk_semaphore_call`

### fenestra_tasks_sdk.h

Source: [fenestra_tasks_sdk.h](../include/fenestra_tasks_sdk.h)

- `fx_sdk_task_post`
- `fx_sdk_task`

### fenestra_video_sdk.h

Source: [fenestra_video_sdk.h](../include/fenestra_video_sdk.h)

- `fx_sdk_video_query`
- `fx_sdk_video_begin`
- `fx_sdk_video_header`
- `fx_sdk_video_mode`
- `fx_sdk_video_commit`

## Profile native function names

The following table lists native function bindings. Composite services such as `camera.display.scene` are registered by the shared runtime; this table is not a complete runtime service enumeration.

| Name | ZR C1.10 | Z9 C5.32 |
|---|---|---|
| `camera.card.close` | Available | Available |
| `camera.card.directory.close` | Available | Available |
| `camera.card.directory.next` | Available | Available |
| `camera.card.directory.open` | Available | Available |
| `camera.card.open` | Available | Available |
| `camera.card.read` | Available | Available |
| `camera.card.rename` | Available | Available |
| `camera.card.seek64` | Available | Available |
| `camera.card.write` | Available | Available |
| `camera.menu.page.get` | Available | Available |
| `camera.menu.text.get` | Available | Available |
| `camera.ui.bounds.get` | Available | Available |
| `camera.ui.bounds.set` | Available | Available |
| `camera.ui.editor.delete-byte` | Available | Available |
| `camera.ui.editor.hit` | Available | Available |
| `camera.ui.editor.init` | Available | Available |
| `camera.ui.editor.insert-byte` | Available | Available |
| `camera.ui.editor.read` | Available | Available |
| `camera.ui.text.align` | Available | Available |
| `camera.ui.text.measure` | Available | Available |
| `native.string.length` | Available | Not registered |
| `rtos.context.is-irq` | Available | Available |
| `rtos.event.clear-keep` | Available | Available |
| `rtos.event.clear-keep-irq` | Available | Available |
| `rtos.event.create` | Available | Available |
| `rtos.event.delete` | Available | Available |
| `rtos.event.set` | Available | Available |
| `rtos.event.set-irq` | Available | Available |
| `rtos.event.wait` | Available | Available |
| `rtos.irq.affinity.get` | Available | Available |
| `rtos.irq.affinity.set` | Available | Available |
| `rtos.irq.disable` | Available | Available |
| `rtos.irq.enable` | Available | Available |
| `rtos.irq.limit` | Available | Available |
| `rtos.irq.pending.clear` | Available | Available |
| `rtos.irq.pending.is-set` | Available | Available |
| `rtos.irq.pending.set` | Available | Available |
| `rtos.irq.priority-levels` | Available | Available |
| `rtos.irq.priority.get` | Available | Available |
| `rtos.irq.priority.set` | Available | Available |
| `rtos.irq.register` | Available | Available |
| `rtos.irq.register-affinity` | Available | Available |
| `rtos.irq.status` | Available | Available |
| `rtos.irq.trigger.get` | Available | Available |
| `rtos.irq.trigger.set` | Available | Available |
| `rtos.irq.unregister` | Available | Available |
| `rtos.memory.allocate` | Available | Available |
| `rtos.memory.release` | Available | Available |
| `rtos.mmu.attributes` | Available | Available |
| `rtos.mmu.context.attributes` | Available | Available |
| `rtos.mmu.context.map` | Available | Available |
| `rtos.mmu.context.protect` | Available | Available |
| `rtos.mmu.context.translate` | Available | Available |
| `rtos.mmu.context.unmap` | Available | Available |
| `rtos.mmu.context.walk` | Available | Available |
| `rtos.mmu.invalidate` | Available | Available |
| `rtos.mmu.invalidate-all` | Available | Available |
| `rtos.mmu.map` | Available | Available |
| `rtos.mmu.mapped` | Available | Available |
| `rtos.mmu.protect` | Available | Available |
| `rtos.mmu.translate` | Available | Available |
| `rtos.mmu.unmap` | Available | Available |
| `rtos.mutex.create` | Available | Available |
| `rtos.mutex.delete` | Available | Available |
| `rtos.mutex.lock` | Available | Available |
| `rtos.mutex.lock-timeout` | Available | Available |
| `rtos.mutex.try-lock` | Available | Available |
| `rtos.mutex.unlock` | Available | Available |
| `rtos.semaphore.create` | Available | Available |
| `rtos.semaphore.delete` | Available | Available |
| `rtos.semaphore.poll` | Available | Available |
| `rtos.semaphore.signal` | Available | Available |
| `rtos.semaphore.signal-irq` | Available | Available |
| `rtos.semaphore.wait` | Available | Available |
| `rtos.semaphore.wait-timeout` | Available | Available |
| `rtos.spinlock.init` | Available | Available |
| `rtos.spinlock.try-lock` | Available | Available |
| `rtos.spinlock.unlock` | Available | Available |
| `rtos.task.create` | Available | Available |
| `rtos.task.delay` | Available | Available |
| `rtos.task.resume` | Available | Available |

## Native firmware inputs

This index does not include original firmware instructions, table contents, or keys. Build-time original bytes come only from user-supplied firmware verified against its complete SHA-256. When adding a binding, update the profile, independent checks, and porting documentation together.
