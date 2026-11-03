# Native resident extensions

Execution and purpose are independent: managed/native selects the entry ABI;
APP/EXTENSION/SERVICE selects behaviour. Both use card-discovered `.wlm` files
and ordinary module enable settings. Native initialization starts after
Activate, on the original RTOS mappings, with a versioned bootstrap.

The Loader resolves named profile bindings and retains executable code and
bootstrap state. Extensions own tasks, callbacks, hooks, interrupts, DMA,
synchronization and device protocols. Closing a menu or disabling a module
does not unload referenced code. Shutdown notification is distinct from
acknowledged quiescence. Recovery is camera restart.

Ordinary modules can use native providers through named, versioned service
contracts. Shared stream buffers carry bulk data; copied messages carry
control and completion information. Audio policy and hardware adaptation
belong to providers, with explicit supported formats and operations.

Native code shares the stock firmware fault domain. Avoiding internal Flash
writes is the extension author's implementation and review obligation;
arbitrary privileged execution cannot provide an enforced no-Flash guarantee.
