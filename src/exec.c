#include "fenestra_exec.h"
#include "fenestra_memory.h"

static int geometry(uint64_t va, uint64_t pa, uint32_t capacity) {
    return capacity != 0u && capacity <= FX_EXEC_PAGES * FX_EXEC_PAGE &&
           capacity % FX_EXEC_PAGE == 0u && (va | pa) % FX_EXEC_PAGE == 0u &&
           va < (UINT64_C(1) << 39) && capacity <= (UINT64_C(1) << 39) - va &&
           pa < (UINT64_C(1) << 48) && capacity <= (UINT64_C(1) << 48) - pa;
}
FxResult fx_exec_plan(const FxModule *m,uint64_t va,uint64_t pa,uint32_t capacity,
                      uint64_t *out,uint32_t available) {
    if(!m || !out || !geometry(va,pa,capacity) || available<capacity/FX_EXEC_PAGE)return FX_ARGUMENT;
    if(!m->prepared || m->abi!=FX_ABI || m->isa!=FX_ISA_AARCH64 || !m->segment_count ||
       m->segment_count>FX_MODULE_SEGMENTS || !m->bytes || m->bytes>capacity ||
       m->entry_offset>=m->bytes || m->entry_offset%4u)return FX_STATE;
    uint32_t seen_entry=0u;
    /* Validate completely before changing output. Comparing segment page ranges
     * avoids a maximum-size automatic plan on the original RTOS task stack. */
    for(uint32_t i=0;i<m->segment_count;++i) {
        const FxSegment *a=&m->segments[i];
        if(!a->memory_bytes || a->image_offset>=m->bytes || a->memory_bytes>m->bytes-a->image_offset ||
           a->file_bytes>a->memory_bytes || (a->flags!=FX_READ && a->flags!=(FX_READ|FX_WRITE) &&
                                          a->flags!=(FX_READ|FX_EXECUTE)))return FX_BOUNDS;
        uint32_t first=a->image_offset/FX_EXEC_PAGE,last=(a->image_offset+a->memory_bytes-1u)/FX_EXEC_PAGE;
        for(uint32_t j=0;j<i;++j) {
            const FxSegment *b=&m->segments[j];
            if(a->flags!=b->flags && first<=(b->image_offset+b->memory_bytes-1u)/FX_EXEC_PAGE &&
               b->image_offset/FX_EXEC_PAGE<=last)return FX_INTEGRITY;
        }
        if((a->flags&FX_EXECUTE) && m->entry_offset>=a->image_offset &&
           m->entry_offset-a->image_offset<a->file_bytes)seen_entry=1u;
    }
    if(!seen_entry)return FX_INTEGRITY;
    memset(out,0,(size_t)(capacity/FX_EXEC_PAGE)*sizeof(*out));
    for(uint32_t i=0;i<m->segment_count;++i) {
        const FxSegment *a=&m->segments[i];uint32_t last=(a->image_offset+a->memory_bytes-1u)/FX_EXEC_PAGE;
        for(uint32_t page=a->image_offset/FX_EXEC_PAGE;page<=last;++page) {
            uint64_t pte=(pa+(uint64_t)page*FX_EXEC_PAGE)|FX_EXEC_RW_XN;
            if(!(a->flags&FX_WRITE))pte|=UINT64_C(2)<<6;
            if(a->flags&FX_EXECUTE)pte&=~FX_EXEC_PXN;
            out[page]=pte;
        }
    }
    return FX_OK;
}
static uint32_t page_up(uint32_t bytes){return (bytes+4095u)&~4095u;}
FxResult fx_exec_layout(uint32_t metadata,uint32_t package,uint32_t image,uint32_t exchange,
                        uint32_t extra,FxExecLayout *out) {
    return fx_exec_layout_stacks(metadata,package,image,exchange,extra,
        (FxModuleStacks){FX_MODULE_STACK_DEFAULT,FX_MODULE_STACK_DEFAULT},out);
}
FxResult fx_exec_layout_stacks(uint32_t metadata,uint32_t package,uint32_t image,uint32_t exchange,
                        uint32_t extra,FxModuleStacks stacks,FxExecLayout *out) {
    if(!out || !metadata || metadata>1048576u || package<=FX_MODULE_HEADER || package>FX_CARD_LIMIT ||
       !image || image>FX_MODULE_MEMORY_LIMIT || !exchange || exchange>65536u || (exchange&4095u) ||
       extra>FX_MODULE_MEMORY_LIMIT || (extra&4095u) || !fx_module_stacks_valid(stacks))return FX_ARGUMENT;
    FxExecLayout p={0};p.tables=7u;p.image_bytes=page_up(image);p.package_bytes=package;
    p.native_stack_bytes=stacks.native_bytes;p.user_stack_bytes=stacks.user_bytes;
    for(uint32_t attempt=0;attempt<16u;++attempt) {
        uint32_t at=page_up(metadata);
        p.private_tables=at;at+=p.tables*4096u;p.user_tables=at;at+=p.tables*4096u;
        p.native_stack=at;at+=stacks.native_bytes;p.api=at;at+=4096u;p.exchange=at;at+=exchange;
        p.extra=at;at+=extra+4096u;p.user_stack=at;at+=stacks.user_bytes+4096u;
        /* Package/file scratch is temporary and never part of either owned view. */
        p.package=p.file=0u;
        p.expected=at;at+=p.image_bytes/4096u*8u;p.staging=at;at+=p.image_bytes/4096u*8u;
        p.image=page_up(at);p.bytes=p.image+p.image_bytes;
        uint32_t tables=6u+(p.bytes+2097151u)/2097152u;
        if(tables>16u)return FX_BUDGET;
        if(tables==p.tables){*out=p;return FX_OK;}p.tables=tables;
    }
    return FX_BUDGET;
}

#if defined(__aarch64__)
static FxResult context(uint64_t *root, uint64_t *tcr, uint64_t *mair) {
    uint64_t el, control;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    if (el != 4u) return FX_CAPABILITY;
    __asm__ volatile("mrs %0, sctlr_el1; mrs %1, ttbr0_el1; mrs %2, tcr_el1; mrs %3, mair_el1"
                     : "=r"(control), "=r"(*root), "=r"(*tcr), "=r"(*mair));
    if (!(control & 1u) || (*tcr & 63u) != 25u || (*tcr & (UINT64_C(3) << 14)) ||
        (*tcr & (UINT64_C(1) << 7)) || (*tcr & (UINT64_C(1) << 37)) || (*mair & 255u) != 255u)
        return FX_CAPABILITY;
    return FX_OK;
}
/* Probe using the CPU, before dereferencing a physical table address. Device
 * memory and non-identity aliases are not admissible table access backends. */
static int translated(uint64_t va, uint64_t pa) {
    uint64_t par;
    __asm__ volatile("at s1e1r, %0; isb; mrs %1, par_el1" : "+r"(va), "=r"(par) : : "memory");
    return !(par & 1u) && (par & FX_EXEC_PA_MASK) == pa && (par >> 56) == 255u;
}
static int table_readable(uint64_t pa) { return translated(pa, pa); }
static FxResult leaf_for(uint64_t root, uint64_t va, volatile uint64_t **leaf) {
    uint64_t table = root & FX_EXEC_PA_MASK, entry; unsigned shift;
    for (shift = 30u; shift > 12u; shift -= 9u) {
        if (!table_readable(table)) return FX_CAPABILITY;
        entry = ((volatile uint64_t *)(uintptr_t)table)[(va >> shift) & 511u];
        if ((entry & ~FX_EXEC_PA_MASK) != 3u) return FX_CAPABILITY;
        table = entry & FX_EXEC_PA_MASK;
    }
    if (!table_readable(table)) return FX_CAPABILITY;
    *leaf = (volatile uint64_t *)(uintptr_t)(table + ((va >> 12) & 511u) * 8u); return FX_OK;
}
static int overlap(uint64_t a, uint64_t size, const FxExecArena *arena) {
    return a < arena->physical_base + arena->capacity && arena->physical_base < a + size;
}
/* A bounded walk proves there is no other writable/executable virtual alias
 * of the reserved backing range in this TTBR0. TTBR1 must be disabled below. */
static FxResult aliases(const FxExecArena *a) {
    uint64_t root = a->ttbr0 & FX_EXEC_PA_MASK, e1, e2, e3, p1, p2, p3, va;
    uint32_t i, j, k, tables = 1;
    if (!table_readable(root) || !(a->tcr & (UINT64_C(1) << 23))) return FX_CAPABILITY;
    for (i = 0; i < 512u; ++i) {
        e1 = ((volatile uint64_t *)(uintptr_t)root)[i]; if (!(e1 & 1u)) continue;
        p1 = e1 & FX_EXEC_PA_MASK;
        if ((e1 & 3u) == 1u) { if (overlap(p1, UINT64_C(1) << 30, a)) return FX_INTEGRITY; continue; }
        if ((e1 & ~FX_EXEC_PA_MASK) != 3u || ++tables > 32u || !table_readable(p1)) return FX_CAPABILITY;
        for (j = 0; j < 512u; ++j) {
            e2 = ((volatile uint64_t *)(uintptr_t)p1)[j]; if (!(e2 & 1u)) continue;
            p2 = e2 & FX_EXEC_PA_MASK;
            if ((e2 & 3u) == 1u) { if (overlap(p2, UINT64_C(1) << 21, a)) return FX_INTEGRITY; continue; }
            if ((e2 & ~FX_EXEC_PA_MASK) != 3u || ++tables > 32u || !table_readable(p2)) return FX_CAPABILITY;
            for (k = 0; k < 512u; ++k) {
                e3 = ((volatile uint64_t *)(uintptr_t)p2)[k]; if (!(e3 & 1u)) continue;
                if ((e3 & 3u) != 3u) return FX_INTEGRITY;
                p3 = e3 & FX_EXEC_PA_MASK;
                if (!overlap(p3, FX_EXEC_PAGE, a)) continue;
                va = (uint64_t)i << 30 | (uint64_t)j << 21 | (uint64_t)k << 12;
                if (va != a->virtual_base + p3 - a->physical_base) return FX_INTEGRITY;
            }
        }
    }
    return FX_OK;
}
static volatile uint64_t *entry(const FxExecArena *a,uint32_t page) {
    uint32_t n=(uint32_t)((a->virtual_base>>12)&511u)+page;
    return a->leaf_tables[n/512u]+n%512u;
}
static void invalidate(FxExecArena *a) {
    uint32_t i; uint64_t va;
    for (i = 0; i < a->pages; ++i) *entry(a,i) = 0u;
    __asm__ volatile("dsb ishst" : : : "memory");
    for (i = 0; i < a->pages; ++i) {
        va = (a->virtual_base + (uint64_t)i * FX_EXEC_PAGE) >> 12;
        __asm__ volatile("tlbi vaae1is, %0" : : "r"(va) : "memory");
    }
    __asm__ volatile("dsb ish; isb" : : : "memory");
}
static void visible(uint64_t va, uint32_t bytes) {
    uint64_t ctr, line, address, end = va + bytes;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    line = 4u << ((ctr >> 16) & 15u);
    for (address = va; address < end; address += line) __asm__ volatile("dc cvau, %0" : : "r"(address) : "memory");
    __asm__ volatile("dsb ish" : : : "memory");
    line = 4u << (ctr & 15u);
    for (address = va; address < end; address += line) __asm__ volatile("ic ivau, %0" : : "r"(address) : "memory");
    __asm__ volatile("dsb ish; isb" : : : "memory");
}
#endif

static int storage_ok(uint64_t va,uint32_t capacity,const FxExecArena *arena,FxExecStorage storage) {
    uint32_t pages=capacity/FX_EXEC_PAGE;uintptr_t a=(uintptr_t)storage.expected,b=(uintptr_t)storage.staging;
    size_t n=(size_t)pages*8u;uintptr_t owner=(uintptr_t)arena;
    if(!a || !b || ((a|b)&7u) || storage.pages<pages || a>UINTPTR_MAX-n || b>UINTPTR_MAX-n)return 0;
    if((a<b+n && b<a+n) || (a<va+capacity && va<a+n) || (b<va+capacity && va<b+n) ||
       (a<owner+sizeof(*arena) && owner<a+n) || (b<owner+sizeof(*arena) && owner<b+n))return 0;
    return 1;
}
FxResult fx_exec_bind(FxExecArena *out,uint64_t va,uint64_t pa,uint32_t capacity,
                      volatile uint64_t *leaf,FxExecStorage storage) {
    if(!out || !leaf || !geometry(va,pa,capacity) || ((uintptr_t)leaf&7u) ||
       !storage_ok(va,capacity,out,storage))return FX_ARGUMENT;
    if(out->bound || out->published)return FX_STATE;
#if defined(__aarch64__)
    FxExecArena a={0};volatile uint64_t *actual;FxResult result;
    a.virtual_base=va;a.physical_base=pa;a.capacity=capacity;a.pages=capacity/FX_EXEC_PAGE;a.leaf=leaf;
    a.expected=storage.expected;a.staging=storage.staging;a.storage_pages=storage.pages;
    result=context(&a.ttbr0,&a.tcr,&a.mair);if(result!=FX_OK)return result;
    for(uint32_t page=0;page<a.pages;) {
        uint64_t address=va+(uint64_t)page*FX_EXEC_PAGE;
        result=leaf_for(a.ttbr0,address,&actual);if(result!=FX_OK)return result;
        if(!page && actual!=leaf)return FX_INTEGRITY;
        uint32_t offset=(uint32_t)((address>>12)&511u);
        a.leaf_tables[a.leaf_table_count++]=actual-offset;
        /* Plan buffers are ordinary private RAM, never table descriptors. */
        uintptr_t table=(uintptr_t)(actual-offset),ea=(uintptr_t)a.expected,sa=(uintptr_t)a.staging;
        size_t n=(size_t)a.pages*8u;
        if((ea<table+4096u && table<ea+n) || (sa<table+4096u && table<sa+n))return FX_INTEGRITY;
        page+=512u-offset;
    }
    result=aliases(&a);if(result!=FX_OK)return result;
    for(uint32_t i=0;i<a.pages;++i) {
        uint64_t wanted=(pa+(uint64_t)i*FX_EXEC_PAGE)|FX_EXEC_RW_XN;
        if(*entry(&a,i)!=wanted || !translated(va+(uint64_t)i*FX_EXEC_PAGE,pa+(uint64_t)i*FX_EXEC_PAGE))return FX_INTEGRITY;
    }
    for(uint32_t i=0;i<a.pages;++i)a.expected[i]=(pa+(uint64_t)i*FX_EXEC_PAGE)|FX_EXEC_RW_XN;
    a.bound=1u;*out=a;return FX_OK;
#else
    return FX_CAPABILITY;
#endif
}
FxResult fx_exec_live(const FxExecArena *a) {
    if (a == NULL || a->bound != 1u || a->published > 1u || a->leaf == NULL || ((uintptr_t)a->leaf & 7u) ||
        a->pages != a->capacity / FX_EXEC_PAGE || !geometry(a->virtual_base, a->physical_base, a->capacity) ||
        !storage_ok(a->virtual_base,a->capacity,a,(FxExecStorage){a->expected,a->staging,a->storage_pages}) ||
        a->leaf_table_count!=(((uint32_t)((a->virtual_base>>12)&511u)+a->pages-1u)/512u+1u)) return FX_STATE;
#if defined(__aarch64__)
    uint64_t root, tcr, mair; volatile uint64_t *actual; FxResult result; uint32_t i;
    result = context(&root, &tcr, &mair); if (result != FX_OK) return result;
    if (root != a->ttbr0 || tcr != a->tcr || mair != a->mair) return FX_STALE;
    for(i=0;i<a->pages;) {
        uint64_t va=a->virtual_base+(uint64_t)i*FX_EXEC_PAGE;uint32_t slot=(uint32_t)((va>>12)&511u);
        result=leaf_for(root,va,&actual);if(result!=FX_OK)return result;
        if(actual!=entry(a,i))return FX_STALE;
        i+=512u-slot;
    }
    for(i=0;i<a->pages;++i)if(*entry(a,i)!=a->expected[i])return FX_INTEGRITY;
    return FX_OK;
#else
    return FX_CAPABILITY;
#endif
}
FxResult fx_exec_publish(FxExecArena *a, const FxModule *module) {
    FxResult result;
    if (a == NULL || !a->bound || a->published) return FX_STATE;
    result = fx_exec_live(a); if (result != FX_OK) return result;
    result = fx_exec_plan(module,a->virtual_base,a->physical_base,a->capacity,a->staging,a->storage_pages);
    if(result!=FX_OK)return result;
#if defined(__aarch64__)
    uint32_t i;
    result = aliases(a); if (result != FX_OK) return result;
    visible(a->virtual_base, module->bytes); invalidate(a);
    for (i = 0; i < a->pages; ++i) *entry(a,i) = a->staging[i];
    __asm__ volatile("dsb ishst; isb" : : : "memory");
    memcpy(a->expected,a->staging,(size_t)a->pages*8u); a->published = 1; return FX_OK;
#else
    return FX_CAPABILITY;
#endif
}
FxResult fx_exec_revoke(FxExecArena *a) {
    FxResult result = fx_exec_live(a);
    if (result != FX_OK) return result;
#if defined(__aarch64__)
    result = aliases(a); if (result != FX_OK) return result;
    invalidate(a); memset(a->expected,0,(size_t)a->pages*8u); a->published = a->bound = 0;
    return FX_OK;
#else
    return FX_CAPABILITY;
#endif
}
