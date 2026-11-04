/* Z8 address-space unit fixture: runs the production fx_z8_space_* code with
 * a fixture-owned identity probe and verifies the built PTE attributes,
 * duplicate/reject/full/sealed admission rules against the source contract. */
#include "address_space.h"

#define PAGE_SIZE 4096u

static _Alignas(4096) uint64_t pool[FX_Z8_TABLES * 512];
static _Alignas(4096) uint64_t pool2[FX_Z8_TABLES * 512];
static uintptr_t veto;

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
static int probe(void *context,uintptr_t va,uint32_t flags) {
    (void)flags;
    if(context && va==*(uintptr_t *)context)return 0;   /* the vetoed address */
    return va>=UINT64_C(0x40400000) && va<UINT64_C(0x48000000);
}

static uint64_t *walk(FxZ8AddressSpace *s,uintptr_t va) {
    uint64_t *table=s->tables[0];
    for(uint32_t shift=30u;shift>12u;shift-=9u) {
        uint64_t entry=table[(va>>shift)&511u];
        if((entry&3u)!=3u)return 0;
        table=(uint64_t *)(uintptr_t)(entry&UINT64_C(0x0000fffffffff000));
    }
    return &table[(va>>12)&511u];
}

void fixture_main(void) {
    FxZ8AddressSpace s;uintptr_t root;
    /* init: minimum capacity, full-pool RW probe, pool cleared. */
    require(fx_z8_space_init(&s,pool,3u,&veto,probe)==FX_OK,"init minimal capacity");
    require(s.used==1u && s.capacity==3u && !s.sealed,"init bookkeeping");
    /* init admission rejects: capacity below minimum, missing probe, re-init. */
    require(fx_z8_space_init(&s,pool,2u,0,probe)==FX_ARGUMENT,"init rejects capacity 2");
    require(fx_z8_space_init(&s,pool,3u,0,0)==FX_ARGUMENT,"init rejects null probe");
    require(fx_z8_space_init(&s,pool,3u,0,probe)==FX_BUSY,"init rejects re-init");
    /* map: one page, attributes exactly as the private gate expects (RW). */
    const uintptr_t page=UINT64_C(0x41000000);
    require(fx_z8_space_map(&s,page,PAGE_SIZE,FX_Z8_MAP_READ|FX_Z8_MAP_WRITE)==FX_OK,"map rw page");
    const uint64_t rw=UINT64_C(3)|(UINT64_C(3)<<8)|(UINT64_C(1)<<10)|(UINT64_C(1)<<54)|(UINT64_C(1)<<53);
    require(walk(&s,page) && *walk(&s,page)==(page|rw),"rw pte attributes");
    /* map admission: write+execute rejected; probe veto rejected. */
    require(fx_z8_space_map(&s,page+UINT64_C(0x200000),PAGE_SIZE,
        FX_Z8_MAP_READ|FX_Z8_MAP_WRITE|FX_Z8_MAP_EXECUTE)==FX_ARGUMENT,"map rejects w+x");
    veto=page+UINT64_C(0x201000);
    require(fx_z8_space_map(&s,veto,PAGE_SIZE,FX_Z8_MAP_READ|FX_Z8_MAP_WRITE)==FX_CAPABILITY,"map rejects probe veto");
    veto=0;
    /* duplicate mapping poisons the view and seals it. */
    require(fx_z8_space_map(&s,page,PAGE_SIZE,FX_Z8_MAP_READ|FX_Z8_MAP_WRITE)==FX_INTEGRITY,"duplicate map rejected");
    require(s.sealed==2u,"duplicate poisons view");
    require(fx_z8_space_map(&s,page+UINT64_C(0x300000),PAGE_SIZE,FX_Z8_MAP_READ)==FX_STATE,"poisoned view sealed");
    /* capacity exhaustion on a fresh space: two L1 branches drain the pool. */
    FxZ8AddressSpace f;
    require(fx_z8_space_init(&f,pool2,3u,0,probe)==FX_OK,"budget init");
    require(fx_z8_space_map(&f,UINT64_C(0x40400000),PAGE_SIZE,FX_Z8_MAP_READ)==FX_OK,"budget first branch");
    require(fx_z8_space_map(&f,UINT64_C(0x40600000),PAGE_SIZE,FX_Z8_MAP_READ)==FX_BUDGET,"pool exhaustion rejected");
    require(f.sealed==2u,"exhaustion poisons view");
    /* seal: root is the pool, further mapping rejected. */
    FxZ8AddressSpace t;
    require(fx_z8_space_init(&t,pool2+FX_Z8_TABLES*512/2,3u,0,probe)==FX_OK,"seal init");
    require(fx_z8_space_map(&t,UINT64_C(0x41000000),PAGE_SIZE,FX_Z8_MAP_READ|FX_Z8_MAP_USER)==FX_OK,"seal map");
    require(fx_z8_space_seal(&t,&root)==FX_OK && root==(uintptr_t)t.tables,"seal returns pool root");
    require(fx_z8_space_map(&t,UINT64_C(0x41100000),PAGE_SIZE,FX_Z8_MAP_READ)==FX_STATE,"sealed rejects map");
    output("PASS Z8 address_space init-map-pte-duplicate-veto-budget-seal\n");
    finish(0u);
}
