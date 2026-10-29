/* Ordinary native IRQ callback, independent of camera/module identities.
 * Descriptor and mailbox belong to explicitly exported native allocations. */
int64_t fx_native_irq_mailbox(uintptr_t cookie,uintptr_t saved_context) {
    FxNativeIrqMailbox *mailbox=(FxNativeIrqMailbox *)cookie;
    if(!mailbox || mailbox->abi!=1u || mailbox->bytes!=sizeof(*mailbox) || !mailbox->disable)return -17;
    int32_t result=((int32_t (*)(int32_t))(uintptr_t)mailbox->disable)(mailbox->interrupt);
    atomic_store_explicit(&mailbox->mask_result,result,memory_order_relaxed);
    atomic_store_explicit(&mailbox->last_context,saved_context,memory_order_relaxed);
    atomic_fetch_add_explicit(&mailbox->notifications,1u,memory_order_release);
    return result;
}
