/* Z8 routed-id allocation fixture (T5): runs the production route table
 * and its validators in the isolated virt machine. Checks: the shipped
 * fx_z8_route validates; parameterized minimal allocations exhaust with
 * FX_CAPABILITY (never a fallback id); overlapping or out-of-window
 * tables are rejected. */
#include "menu_data.h"
#include "menu_route.h"

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

void fixture_main(void) {
    /* The shipped table must validate and serve both dynamic slots. */
    require(fx_z8_route_validate(&fx_z8_route)==FX_OK,"production table invalid");
    uint32_t row=0u;
    require(fx_z8_route_module_row_base(&fx_z8_route,0u,&row)==FX_OK,"slot 0 missing");
    require(row==fx_z8_route.module_row_base[0],"slot 0 id mismatch");
    require(fx_z8_route_module_row_base(&fx_z8_route,1u,&row)==FX_OK,"slot 1 missing");
    require(row==fx_z8_route.module_row_base[1],"slot 1 id mismatch");
    /* Parameterized minimal allocation: the shipped two-slot bound and a
     * zero-slot table both exhaust with FX_CAPABILITY past the count --
     * explicit failure, never a silent fallback id. */
    require(fx_z8_route_module_row_base(&fx_z8_route,2u,&row)==FX_CAPABILITY,"slot 2 not exhausted");
    FxZ8RouteTable empty=fx_z8_route;empty.module_row_count=0u;
    require(fx_z8_route_module_row_base(&empty,0u,&row)==FX_CAPABILITY,"zero-slot not exhausted");
    require(fx_z8_route_validate(&empty)==FX_ARGUMENT,"zero-slot table validates");
    /* A third module slot exceeds the packed row-family budget. */
    FxZ8RouteTable three=fx_z8_route;three.module_row_count=3u;
    require(fx_z8_route_validate(&three)==FX_ARGUMENT,"three-slot table validates");
    /* Gate/window/disjointness rejections: every violation must fail. */
    FxZ8RouteTable above=fx_z8_route;above.apps_list_base=0x68cu;
    require(fx_z8_route_validate(&above)==FX_CAPABILITY,"gate id accepted");
    FxZ8RouteTable overlap=fx_z8_route;overlap.manager_base=fx_z8_route.fenestra_page;
    require(fx_z8_route_validate(&overlap)==FX_CAPABILITY,"page overlap accepted");
    FxZ8RouteTable row_overlap=fx_z8_route;row_overlap.apps_row_base=fx_z8_route.manager_row_base;
    require(fx_z8_route_validate(&row_overlap)==FX_CAPABILITY,"row overlap accepted");
    FxZ8RouteTable row_out=fx_z8_route;row_out.module_row_base[0]=0x46e8u;
    require(fx_z8_route_validate(&row_out)==FX_CAPABILITY,"row pool escape accepted");
    /* Packed module slots collide when the second base invades the first
     * slot's control rows. */
    FxZ8RouteTable slot_clash=fx_z8_route;
    slot_clash.module_row_base[1]=slot_clash.module_row_base[0]+1u;
    require(fx_z8_route_validate(&slot_clash)==FX_CAPABILITY,"module slot overlap accepted");
    /* The apps list span must stay inside the window and clear of the
     * own pages (seven pages = root + six list pages). */
    FxZ8RouteTable apps_wide=fx_z8_route;
    apps_wide.apps_list_base=fx_z8_route.fenestra_page-FX_Z8_APPS_PAGES+2u;
    require(fx_z8_route_validate(&apps_wide)==FX_CAPABILITY,"apps span overlap accepted");
    FxZ8RouteTable window=fx_z8_route;window.module_page_limit=window.module_page_first+1u;
    require(fx_z8_route_validate(&window)==FX_CAPABILITY,"tiny module window accepted");
    output("ROUTE OK\n");
    finish(0u);
}
