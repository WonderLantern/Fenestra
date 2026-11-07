/* Z8 routed-page descriptor tail fixture (T12-①): the C311 open-path
 * staged visible-row builder (FUN_403b01f0) dereferences *(desc+0x40) as
 * {count, content}, so shared-built routed pages must publish a valid
 * 0x50-byte descriptor tail instead of leaving the copied header bytes
 * there (x23=0x600000000 = header words {0,6}). Verifies the shared
 * builder publishes the profile tail verbatim at descriptor+0x40/+0x48
 * and zeroes it when the profile supplies none. */
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

static const FxNativeEvent navigation[13]={
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

/* The empty staging vector shape FUN_403b01f0 consumes: a 16-bit count
 * at +0 and the content array pointer at +8 (zero entries). */
static const struct { uint16_t count; uint16_t pad[3]; uintptr_t content; }
    empty_staging = {0u, {0u, 0u, 0u}, 0u};
static const uint64_t tail_pair[2]={(uint64_t)(uintptr_t)&empty_staging, 0x48u};

static FxNativeRow stock_rows[4];
static FxNativePage stock_page;
static FxNativeDescriptor stock_descriptor;
static uintptr_t stock_events[4];
static uintptr_t callbacks[5];

static uint32_t action(void *p){(void)p;return 1u;}

static const FxNativeEntry entries[2]={
    {3206u,0x45cfu,0u,0u,(uintptr_t)action,0x636u},
    {3207u,0x45e7u,0u,0u,(uintptr_t)action,0x636u},
};

void fixture_main(void) {
    require(sizeof(FxNativeDescriptor)==64u,"descriptor stays 64 bytes");
    require(offsetof(FxNativeMenu,descriptor_tail)==0xa0u,"tail follows descriptor");
    for(uint32_t i=0u;i<4u;++i){stock_rows[i].word[0]=600u+i;stock_rows[i].word[6]=100u+i;}
    stock_descriptor.header=(uintptr_t)stock_rows;
    stock_descriptor.rows=(uintptr_t)stock_rows;
    stock_descriptor.events=(uintptr_t)stock_events;
    stock_descriptor.count=4u;
    stock_page.identity=(0x624u<<16)|2u;
    stock_page.descriptor=(uintptr_t)&stock_descriptor;
    for(uint32_t i=0u;i<5u;++i)callbacks[i]=(uintptr_t)action;

    FxNativeMenu menu;memset(&menu,0,sizeof(menu));
    FxNativeMenuProfile profile={&stock_page,stock_rows,navigation,
        0x3bbu,0x27fu,{0x286u,0x28bu,0x2aeu,0u},0x68cu,0x624u,13u,tail_pair};
    require(fx_native_menu_build(&menu,&profile,0x624u,3204u,entries,2u,callbacks)==FX_OK,"build ok");
    require(menu.page.descriptor==(uintptr_t)&menu.descriptor,"descriptor published");
    /* The tail must land exactly at descriptor+0x40/+0x48 (the stock
     * 0x50-byte descriptor layout), readable as the staging vector. */
    require(menu.descriptor_tail[0]==tail_pair[0],"tail vector copied");
    require(menu.descriptor_tail[1]==tail_pair[1],"tail word copied");
    require(*(const uint64_t *)(const void *)((uintptr_t)&menu.descriptor+0x40u)==tail_pair[0],
            "tail at +0x40");
    require(*(const uint64_t *)(const void *)((uintptr_t)&menu.descriptor+0x48u)==tail_pair[1],
            "tail at +0x48");
    const struct { uint16_t count; uint16_t pad[3]; uintptr_t content; } *vector=
        (const void *)(uintptr_t)tail_pair[0];
    require(vector->count==0u,"empty staging vector");

    /* Profiles without a tail (z9/zr literals) publish zeros. */
    FxNativeMenu plain;memset(&plain,0,sizeof(plain));
    FxNativeMenuProfile none=profile;none.descriptor_tail=0;
    require(fx_native_menu_build(&plain,&none,0x625u,3204u,entries,2u,callbacks)==FX_OK,"plain build ok");
    require(plain.descriptor_tail[0]==0u && plain.descriptor_tail[1]==0u,"no-tail zeros");
    output("TAIL OK\n");
    finish(0u);
}
