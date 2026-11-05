/* Z8 menu-construct fixture: runs the production fx_z8_menu_construct
 * wrapper against a fixture navigation table with the pinned 13-record
 * shape (placed at FX_Z8_MENU_NAVIGATION) and verifies the handler
 * rewriting: activation trio -> entry action, back route -> parent,
 * cursor routes untouched, terminator scan bound. */
#include "menu_data.h"
#include "fenestra_native_menu.h"

void *memset(void *,int,unsigned long);

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

/* Fixture navigation table: the pinned 0x43bf0680 record set (values from
 * the C311 image; callbacks/conditions are never called by the fixture,
 * only rewritten or compared). 12 routes + the 0x3bb terminator. */
__attribute__((section(".navtbl")))
const FxZ8MenuEvent fixture_navigation[13]={
    {0x27e,0x4054eae4,0x403b1c5c,0x68c},
    {0x27f,0x403b1bb0,0x403b1c5c,0x68c},
    {0x283,0x403b1b74,0x403b1c5c,0x68c},
    {0x284,0x403b1b8c,0x403b1c5c,0x68c},
    {0x285,0x403b1ba4,0x403b1c5c,0x68c},
    {0x286,0x4054eae4,0x403b1c5c,0x68c},
    {0x28b,0x4054eae4,0x403b1c7c,0x68c},
    {0x29d,0x403b1b74,0x403b1c5c,0x68c},
    {0x29e,0x403b1b8c,0x403b1c5c,0x68c},
    {0x29f,0x403b1ba8,0x403b1c5c,0x68c},
    {0x2a0,0x403b1ba4,0x403b1c5c,0x68c},
    {0x2ae,0x4054ea5c,0x403b1c7c,0x68c},
    {0x3bb,0,0,0x68c},
};

/* A second table without the terminator: the old 12-record shape must be
 * rejected (the pre-fix code could never terminate its scan). */
__attribute__((section(".navbad")))
const FxZ8MenuEvent fixture_bad_navigation[12]={
    {0x27e,0x4054eae4,0x403b1c5c,0x68c},
    {0x27f,0x403b1bb0,0x403b1c5c,0x68c},
    {0x283,0x403b1b74,0x403b1c5c,0x68c},
    {0x284,0x403b1b8c,0x403b1c5c,0x68c},
    {0x285,0x403b1ba4,0x403b1c5c,0x68c},
    {0x286,0x4054eae4,0x403b1c5c,0x68c},
    {0x28b,0x4054eae4,0x403b1c7c,0x68c},
    {0x29d,0x403b1b74,0x403b1c5c,0x68c},
    {0x29e,0x403b1b8c,0x403b1c5c,0x68c},
    {0x29f,0x403b1ba8,0x403b1c5c,0x68c},
    {0x2a0,0x403b1ba4,0x403b1c5c,0x68c},
    {0x2ae,0x4054ea5c,0x403b1c7c,0x68c},
};

static FxNativeRow stock_rows[45];
static FxZ8MenuPage setup_page;
static FxZ8MenuDescriptor setup_descriptor;
static uintptr_t stock_events[45];
static uintptr_t page_callbacks[5];
static FxZ8OwnedMenu menu;

static uint32_t action_one(void *p){(void)p;return 1u;}
static uint32_t action_two(void *p){(void)p;return 2u;}

static const FxZ8MenuEntry entries[5]={
    {3206u,0x7ff1u,0u,0u,(uintptr_t)action_one,0x7000u},
    {3207u,0x7ff2u,0u,1u,(uintptr_t)action_two,0x7000u},
    {3208u,0x7ff3u,1u,0u,(uintptr_t)action_one,0x705cu},
    {3209u,0x7ff4u,1u,1u,(uintptr_t)action_two,0x705du},
    {3210u,0x7ff5u,0u,0u,(uintptr_t)action_one,0x7001u},
};

static void check_handlers(const FxZ8MenuEvent *handlers,const FxZ8MenuEntry *e) {
    int seen_back=0,seen_select[3]={0,0,0},seen_up=0;
    for(uint32_t j=0u;j<18u;++j) {
        uint64_t event=handlers[j].event&0xffffu;
        if(event==0x27fu) {
            seen_back=1;
            require(handlers[j].condition==0,"back condition cleared");
            require(handlers[j].next==638u,"back routes to parent");
        }
        for(uint32_t k=0u;k<3u;++k)
            if(event==(k==0u?0x286u:(k==1u?0x28bu:0x2aeu))) {
                seen_select[k]=1;
                require(handlers[j].callback==e->action,"select callback replaced");
                require(handlers[j].condition==0,"select condition cleared");
                require(handlers[j].next==e->next,"select next replaced");
            }
        if(event==0x283u) {
            seen_up=1;
            require(handlers[j].callback==0x403b1b74,"cursor up untouched");
        }
    }
    require(seen_back,"back record present");
    require(seen_select[0]&&seen_select[1]&&seen_select[2],"all three select records");
    require(seen_up,"cursor record present");
    require((handlers[12].event&0xffffu)==0x3bbu,"terminator copied");
}

void fixture_main(void) {
    /* Synthetic SETUP page: 45 stock rows, descriptor with rows/events. */
    for(uint32_t i=0u;i<45u;++i){stock_rows[i].word[0]=600u+i;stock_rows[i].word[6]=100u+i;}
    setup_descriptor.header=(uintptr_t)stock_rows; /* any non-null header */
    setup_descriptor.rows=(uintptr_t)stock_rows;
    setup_descriptor.events=(uintptr_t)stock_events;
    setup_descriptor.count=45u;
    setup_page.identity=(638u<<16)|0x1234u;   /* page id in the high half */
    setup_page.descriptor=(uintptr_t)&setup_descriptor;
    /* FxZ8MenuPage mirrors FxNativePage (ABI asserted in menu_construct.c). */
    for(uint32_t i=0u;i<5u;++i)page_callbacks[i]=(uintptr_t)action_one;
    page_callbacks[0]=(uintptr_t)0; page_callbacks[0]=(uintptr_t)action_one;

    /* The wrapper reads the pinned table address; the fixture ld places
     * fixture_navigation there. */
    require((uintptr_t)fixture_navigation==FX_Z8_MENU_NAVIGATION,"table placement");

    FxResult r=fx_z8_menu_construct(&menu,&setup_page,0x7100u,3204u,entries,5u,page_callbacks);
    require(r==FX_OK,"construct ok");
    require(menu.page.identity==((0x7100u<<16)|0x1234u),"page identity");
    require(menu.descriptor.count==5u,"row count");
    require(menu.descriptor.rows==(uintptr_t)menu.rows,"rows repointed");
    for(uint32_t i=0u;i<5u;++i) {
        require(menu.events[i]==(uintptr_t)menu.handlers[i],"events wiring");
        check_handlers(menu.handlers[i],&entries[i]);
        require(menu.rows[i].word[6]==entries[i].row_id,"row id copied");
        require(menu.rows[i].word[0]==entries[i].text_id,"text id copied");
    }
    /* submenu/value flag geometry from the template row */
    require((menu.rows[1].word[4]&1u)==1u,"value flag");
    require((menu.rows[2].word[4]&0x40u)==0x40u,"submenu flag");

    /* The terminator-less table must be rejected. */
    FxZ8OwnedMenu bad;memset(&bad,0,sizeof(bad));
    /* Point the profile at the bad table through a second construct call
     * that overrides the navigation pointer via the linker-provided alias. */
    extern FxResult fx_z8_menu_construct_navigation(FxZ8OwnedMenu *,const FxZ8MenuPage *,
        uint32_t,uint32_t,const FxZ8MenuEntry *,uint32_t,const uintptr_t *,const FxZ8MenuEvent *);
    r=fx_z8_menu_construct_navigation(&bad,&setup_page,0x7101u,3204u,entries,5u,page_callbacks,fixture_bad_navigation);
    require(r==FX_INTEGRITY,"terminator-less table rejected");

    output("CONSTRUCT OK\n");
    finish(0u);
}
