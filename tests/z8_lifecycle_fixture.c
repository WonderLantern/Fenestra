/* Z8 sysctl-lifecycle unit fixture: runs the production fx_z8_lifecycle_*
 * code and verifies the MOUNT_EVER sticky witness plus the boot-cleanup
 * predicate truth table against the source contract (lifecycle.h). */
#include "lifecycle.h"
#include "fenestra_startup.h"

static void output(const char *p) {
    while(*p)*(volatile uint32_t *)(uintptr_t)0x09000000u=(uint8_t)*p++;
}
static void finish(unsigned status) {
    uint64_t args[2]={0x20026u,status};
    register uint64_t op __asm__("x0")=0x20u;
    register void *arg __asm__("x1")=args;
    __asm__ volatile("hlt #0xf000":"+r"(op),"+r"(arg)::"memory");
    for(;;)__asm__ volatile("wfi");
}
static void require(int ok,const char *message) {
    if(!ok){output("FAIL ");output(message);output("\n");finish(1u);}
}

void fixture_main(void) {
    FxZ8Lifecycle s;
    atomic_store(&s.word,0u);
    /* Boot shape (E2c absent-card leg): startup-complete, then cleanup
     * card-removes with no mount ever. */
    fx_z8_lifecycle_record(&s,FX_Z8_LIFE_EVENT_STARTUP_COMPLETE);
    fx_z8_lifecycle_record(&s,FX_Z8_LIFE_EVENT_CARD_REMOVE);
    fx_z8_lifecycle_record(&s,FX_Z8_LIFE_EVENT_CARD_REMOVE);
    require(atomic_load(&s.word)==(FX_Z8_LIFE_STARTED|FX_Z8_LIFE_START_PENDING|
        FX_Z8_LIFE_CARD_PENDING),"absent-boot word shape");
    require(fx_z8_lifecycle_boot_cleanup_window(atomic_load(&s.word),
        FX_STARTUP_IO_ERROR)==1,"cleanup 23 exempt before any mount");
    /* First mount sets the sticky MOUNT_EVER witness beside CARD. */
    fx_z8_lifecycle_record(&s,FX_Z8_LIFE_EVENT_MOUNT_COMPLETE);
    require(atomic_load(&s.word)==(FX_Z8_LIFE_STARTED|FX_Z8_LIFE_CARD|
        FX_Z8_LIFE_START_PENDING|FX_Z8_LIFE_CARD_PENDING|FX_Z8_LIFE_MOUNT_EVER),
        "first mount sets mount_ever");
    /* Stickiness: card-remove clears CARD but never MOUNT_EVER, and taking
     * the pending notices cannot clear it either. */
    fx_z8_lifecycle_record(&s,FX_Z8_LIFE_EVENT_CARD_REMOVE);
    require(!(atomic_load(&s.word)&FX_Z8_LIFE_CARD),"post-mount 23 clears card");
    require(atomic_load(&s.word)&FX_Z8_LIFE_MOUNT_EVER,"23 retains mount_ever");
    require(atomic_load(&s.word)&FX_Z8_LIFE_CARD_PENDING,"23 sets card_pending");
    fx_z8_lifecycle_record(&s,FX_Z8_LIFE_EVENT_MOUNT_COMPLETE);
    (void)fx_z8_lifecycle_take(&s);
    require(atomic_load(&s.word)==(FX_Z8_LIFE_STARTED|FX_Z8_LIFE_CARD|
        FX_Z8_LIFE_MOUNT_EVER),"take clears notices not mount_ever");
    /* Truth table: never-mounted words are exempt exactly in the pre-ARMED
     * phases; a mounted-ever word is never exempt, in any phase. */
    static const uint32_t phases[8]={FX_STARTUP_UNCHECKED,FX_STARTUP_READY,
        FX_STARTUP_ABNORMAL,FX_STARTUP_IO_ERROR,FX_STARTUP_ARMED,
        FX_STARTUP_RUNNING,FX_STARTUP_FAULT,FX_STARTUP_STOPPED};
    static const int never_mounted[8]={1,1,1,1,0,0,0,0};
    const uint32_t never=FX_Z8_LIFE_STARTED|FX_Z8_LIFE_CARD_PENDING;
    const uint32_t ever=never|FX_Z8_LIFE_MOUNT_EVER;
    for(uint32_t i=0;i<8u;++i) {
        require(fx_z8_lifecycle_boot_cleanup_window(never,phases[i])==
            never_mounted[i],"never-mounted truth table");
        require(fx_z8_lifecycle_boot_cleanup_window(ever,phases[i])==0,
            "mounted-ever truth table");
    }
    require(FX_Z8_LIFE_MOUNT_EVER==0x8u && !(FX_Z8_LIFE_MOUNT_EVER&FX_Z8_LIFE_PENDING),
        "mount_ever disjoint from pending mask");
    output("PASS Z8 lifecycle mount-ever-sticky boot-cleanup-window-truth-table\n");
    finish(0u);
}
