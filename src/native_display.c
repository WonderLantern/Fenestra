/* Shared Nikon native GUI/render service binding. No MMIO, physical address
 * arithmetic, emulator register layout or owned scanout buffer is used.
 * The base-object/list/paint ABI is derived from this firmware's own menu
 * callers (403bc830..403bc8c4, 40331468..40331574, 40400358..403e8).
 * This is an internal, version-pinned ABI; it is not a public Nikon SDK. */
#include "fenestra_native_display.h"
#include "fenestra_memory.h"
#define FN(name,t) ((t)(uintptr_t)fx_native_display_profile.name)
#define GUI_LOCK fx_native_display_profile.gui_mutex
static void lock(void) { FN(native_mutex_acquire,void (*)(void *))((void *)GUI_LOCK); }
static void unlock(void) { FN(native_mutex_release,void (*)(void *))((void *)GUI_LOCK); }
FxResult fx_native_backdrop_open(FxNativePage *page) {
    if(!fx_native_page_modal(page))return FX_EMPTY;
    FxNativeBackdrop *b=&((FxNativeMenu *)page)->backdrop;
    if(b->image)return b->surface==page->surface?FX_OK:FX_STATE;
    if(!page->surface || !fx_native_display_profile.vgGetPixels ||
       !fx_native_display_profile.vgSetPixels)return FX_STATE;
    lock();FN(gui_bind_target,void (*)(void *))((void *)page->surface);
    uint32_t size=FN(gui_target_dimensions,uint32_t (*)(void *))((void *)page->surface);
    uint32_t width=size&65535u,height=size>>16;
    if(!width || !height || width>4096u || height>4096u){unlock();return FX_BOUNDS;}
    FN(native_graphics_acquire,void (*)(void))();
    b->image=FN(vgCreateImage,uint32_t (*)(uint32_t,int32_t,int32_t,uint32_t))(193u,(int32_t)width,(int32_t)height,1u);
    b->error=FN(vgGetError,uint32_t (*)(void))();
    if(b->image && !b->error) {
        FN(vgGetPixels,void (*)(uint32_t,int32_t,int32_t,int32_t,int32_t,int32_t,int32_t))
            (b->image,0,0,0,0,(int32_t)width,(int32_t)height);
        b->error=FN(vgGetError,uint32_t (*)(void))();
    }
    if(!b->image || b->error) {
        if(b->image)FN(vgDestroyImage,void (*)(uint32_t))(b->image);
        b->image=0;
    } else {b->width=width;b->height=height;b->surface=page->surface;}
    FN(native_graphics_release,void (*)(void))();unlock();
    return b->image?FX_OK:FX_IO;
}
FxResult fx_native_backdrop_restore(const FxNativePage *page) {
    if(!fx_native_page_modal(page))return FX_EMPTY;
    FxNativeBackdrop *b=&((FxNativeMenu *)(uintptr_t)page)->backdrop;
    if(!b->image)return FX_EMPTY;
    if(b->surface!=page->surface)return FX_STATE;
    /* Caller already owns GUI exclusion for the complete restore/draw/submit. */
    FN(gui_bind_target,void (*)(void *))((void *)page->surface);
    uint32_t size=FN(gui_target_dimensions,uint32_t (*)(void *))((void *)page->surface);
    if((size&65535u)!=b->width || (size>>16)!=b->height)return FX_STATE;
    FN(native_graphics_acquire,void (*)(void))();
    uint32_t scissor=FN(vgGeti,uint32_t (*)(uint32_t))(0x1131u);
    FN(vgSeti,void (*)(uint32_t,int32_t))(0x1131u,0);
    FN(vgSetPixels,void (*)(int32_t,int32_t,uint32_t,int32_t,int32_t,int32_t,int32_t))
        (0,0,b->image,0,0,(int32_t)b->width,(int32_t)b->height);
    b->error=FN(vgGetError,uint32_t (*)(void))();
    FN(vgSeti,void (*)(uint32_t,int32_t))(0x1131u,(int32_t)scissor);
    FN(native_graphics_release,void (*)(void))();
    return b->error?FX_IO:FX_OK;
}
void fx_native_backdrop_close(FxNativePage *page) {
    if(!fx_native_page_modal(page))return;
    FxNativeBackdrop *b=&((FxNativeMenu *)page)->backdrop;
    if(!b->image)return;
    lock();FN(native_graphics_acquire,void (*)(void))();
    FN(vgDestroyImage,void (*)(uint32_t))(b->image);b->image=0;b->surface=0;
    FN(native_graphics_release,void (*)(void))();unlock();
}
static int native_ram(const void *p,uint32_t bytes) {
    uintptr_t a=(uintptr_t)p;
    return !(a&7u) && a>=fx_native_display_profile.ram_begin &&
        bytes<=fx_native_display_profile.ram_end-fx_native_display_profile.ram_begin &&
        a<=fx_native_display_profile.ram_end-bytes;
}
FxNativeView fx_native_current_view(void) {
    if(!fx_native_display_profile.current_view_pointer)return (FxNativeView){0,UINT32_MAX,UINT32_MAX};
    void *view=*(void *volatile *)(uintptr_t)fx_native_display_profile.current_view_pointer;
    if(!native_ram(view,0x110u)) return (FxNativeView){0,UINT32_MAX,UINT32_MAX};
    uint32_t id=FN(native_view_id,uint32_t (*)(void *))(view);
    uint32_t state=FN(native_view_state,uint32_t (*)(void *))(view);
    return (FxNativeView){(uintptr_t)view,id,state};
}
uint32_t fx_native_display_capabilities(void) {
    return FX_SCENE_CAP_PAGE |
        (fx_native_display_profile.vgCreateImage && fx_native_display_profile.vgDestroyImage &&
         fx_native_display_profile.vgImageSubData && fx_native_display_profile.vgDrawImage &&
         fx_native_display_profile.vgLoadMatrix && fx_native_display_profile.vgGetMatrix?FX_SCENE_CAP_IMAGE:0u) |
        (fx_native_display_profile.current_view_pointer && fx_native_display_profile.native_global_surface &&
         fx_native_display_profile.native_graphics_backing_rectangle &&
         fx_native_display_profile.native_global_target && fx_native_display_profile.gui_surface_initialize &&
         fx_native_display_profile.native_graphics_current_device && fx_native_display_profile.native_graphics_target_enabled &&
         fx_native_display_profile.native_graphics_target_enable?FX_SCENE_CAP_GLOBAL:0u);
}
uint32_t fx_native_display_global_ready(void) {
    FxNativeView view=fx_native_current_view();
    return view.address && view.id==fx_native_display_profile.shooting_view_id && view.state==1u;
}
/* A native application transition transfers the target as well as visibility.
 * The old module object may still need unlinking, but clearing that target now
 * would erase pixels the new application has already drawn (notably headers). */
static uint32_t owns_global_target(const FxNativeDisplay *d) {
    FxNativeView view=fx_native_current_view();
    const uint32_t *target=(const uint32_t *)((const uint8_t *)d->surface+0x108u);
    return view.address==d->application && view.id==fx_native_display_profile.shooting_view_id &&
        !memcmp(target,d->target,sizeof(d->target)) &&
        target[0]==FN(native_graphics_current_device,uint32_t (*)(void))() &&
        target[2]==FN(native_global_target,uint32_t (*)(uint32_t))
            ((uint32_t)fx_native_display_profile.global_surface_index);
}
static uint32_t is_current(const FxNativeDisplay *d) {
    if(!d || !atomic_load_explicit(&d->attached,memory_order_acquire))return 0u;
    if(d->global_overlay) {
        FxNativeView view=fx_native_current_view();
        const uint32_t *target=(const uint32_t *)((const uint8_t *)d->surface+0x108u);
        uint16_t viewport[4];
        return owns_global_target(d) && view.state==1u &&
            (FN(native_graphics_backing_rectangle,uint32_t (*)(uint32_t,uint32_t,uint32_t,void *))
                (target[0],target[1],target[2],viewport)&1u) && !memcmp(viewport,d->viewport,sizeof(viewport));
    }
    return FN(native_current_page,void *(*)(void))()==d->page &&
        *(void **)((uint8_t *)d->page+32u)==d->surface;
}
uint32_t fx_native_display_is_current(const FxNativeDisplay *d) {
    lock();uint32_t current=is_current(d);unlock();return current;
}
static void bind(FxNativeDisplay *d) {
    FN(gui_bind_target,void (*)(void *))(d->object);
}
extern uint32_t fx_native_vg_create_path(uintptr_t);
static uint32_t coordinate(uint32_t value) {
    if(!value) return 0u;
    uint32_t exponent=31u-(uint32_t)__builtin_clz(value);
    return (127u+exponent)<<23 | (value<<(23u-exponent)&0x7fffffu);
}
static uint32_t palette(uint32_t rgb) {
    /* Panic page colors share the native renderer, without rain quantization. */
    if(rgb==0xff6868u || rgb==0xf0f0f0u || rgb==0xb8b8b8u) return rgb;
    if(rgb==0xd5ffe6u || rgb==0xc1ffe5u || rgb==0x54af88u || rgb==0x3e8665u) return rgb;
    /* Sixteen intensity levels keep the rain smooth while allowing glyphs of
     * one color to share a single native path submission. */
    uint32_t level=(rgb>>8)&255u;
    level=(level+7u)&~15u;
    if(level>240u) level=240u;
    return (level/14u)<<16 | level<<8 | level/3u;
}
static void flush_path(FxNativeDisplay *d,uint32_t path,uint32_t rectangles) {
    if(!rectangles) return;
    FN(vgAppendPathData,void (*)(uint32_t,int32_t,const uint8_t *,const void *))
        (path,(int32_t)(rectangles*5u),d->segments,d->coordinates); /* vgAppendPathData */
    FN(vgDrawPath,void (*)(uint32_t,uint32_t))(path,2u); /* vgDrawPath(FILL) */
    FN(vgClearPath,void (*)(uint32_t,uint32_t))(path,0xfffu); /* vgClearPath */
    d->rectangles+=rectangles;
}
static void append_rectangle(FxNativeDisplay *d,uint32_t path,uint32_t *count,uint32_t x,uint32_t y,uint32_t w,uint32_t h) {
    uint32_t vw=d->viewport[2]-d->viewport[0],vh=d->viewport[3]-d->viewport[1];
    uint32_t left=d->viewport[0]+x*vw/d->scene->width;
    uint32_t right=d->viewport[0]+(x+w)*vw/d->scene->width;
    uint32_t top=d->height-d->viewport[1]-y*vh/d->scene->height;
    uint32_t bottom=d->height-d->viewport[1]-(y+h)*vh/d->scene->height;
    if(left==right || top==bottom)return;
    const uint32_t vertices[8]={coordinate(left),coordinate(bottom),coordinate(right),coordinate(bottom),
        coordinate(right),coordinate(top),coordinate(left),coordinate(top)};
    const uint8_t commands[5]={2u,4u,4u,4u,0u};
    memcpy(d->coordinates+*count*8u,vertices,sizeof(vertices));memcpy(d->segments+*count*5u,commands,sizeof(commands));
    if(++*count==128u){flush_path(d,path,*count);*count=0u;}
}
static uint32_t paths(FxNativeDisplay *d,uint32_t rgb,uint32_t begin,uint32_t end) {
    uint32_t previous_paint=FN(vgGetPaint,uint32_t (*)(uint32_t))(2u);
    uint32_t path=fx_native_vg_create_path(fx_native_display_profile.vgCreatePath);
    uint32_t paint=FN(vgCreatePaint,uint32_t (*)(void))();
    if(!path || !paint) {
        if(path) FN(vgDestroyPath,void (*)(uint32_t))(path);
        if(paint) FN(vgDestroyPaint,void (*)(uint32_t))(paint);
        return 1u;
    }
    uint32_t rgba[4];
    FN(native_rgba_float_conversion,void (*)(uint32_t,void *))((rgb<<8)|255u,rgba);
    FN(vgSetParameteri,void (*)(uint32_t,uint32_t,int32_t))(paint,0x1a00u,0x1b00);
    FN(vgSetParameterfv,void (*)(uint32_t,uint32_t,int32_t,const void *))(paint,0x1a01u,4,rgba);
    FN(vgSetPaint,void (*)(uint32_t,uint32_t))(paint,2u);
    uint32_t count=0u;
    for(uint32_t i=begin;i<end;++i) {
        const FxDemoDraw *c=&d->scene->commands[i];
        if((d->exact_colors?c->rgb:palette(c->rgb))!=rgb) continue;
        if(c->kind==FX_SCENE_RECT) {
            append_rectangle(d,path,&count,c->x,c->y,c->glyph,c->scale);continue;
        }
        for(uint32_t row=0;row<7u;++row) {
            uint32_t bits=fx_glyph((char)c->glyph,row),col=0u;
            while(col<5u) {
                if(!(bits&(1u<<(4u-col)))) { ++col;continue; }
                uint32_t first=col++;
                while(col<5u && (bits&(1u<<(4u-col)))) ++col;
                append_rectangle(d,path,&count,c->x+first*c->scale,c->y+row*c->scale,
                    (col-first)*c->scale,c->scale);
            }
        }
    }
    flush_path(d,path,count);
    FN(vgSetPaint,void (*)(uint32_t,uint32_t))(previous_paint,2u);
    FN(vgDestroyPath,void (*)(uint32_t))(path);
    FN(vgDestroyPaint,void (*)(uint32_t))(paint);
    return FN(vgGetError,uint32_t (*)(void))(); /* vgGetError */
}
static void rectangle(FxNativeDisplay *d,uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t rgb) {
    /* Native half-open rectangle, native output size and constant RGBA paint.
     * The native service performs clipping, target selection, color conversion,
     * graphics queue submission and completion. */
    uint32_t left=x*d->width/d->scene->width,top=y*d->height/d->scene->height;
    uint32_t right=(x+w)*d->width/d->scene->width,bottom=(y+h)*d->height/d->scene->height;
    if(right<=left || bottom<=top) return;
    const uint16_t rect[4]={(uint16_t)left,(uint16_t)top,(uint16_t)right,(uint16_t)bottom};
    const uint32_t size=d->width|(d->height<<16);
    const struct { uint32_t type,reserved;uint64_t color; } brush={2u,0u,(rgb<<8)|255u};
    FN(native_constant_paint_rectangle,void (*)(const uint16_t *,const uint32_t *,const void *,void *))
        (rect,&size,&brush,0);
    ++d->rectangles;
}
/* Positive bounded integer ratio -> IEEE754, round to nearest/even. Core
 * tasks need no FP register ownership; OpenVG reads the float-bit matrix. */
static uint32_t ratio(uint32_t n,uint32_t denominator) {
    int32_t e=(int32_t)__builtin_clz(denominator)-(int32_t)__builtin_clz(n);
    if(e>=0?(uint64_t)n<((uint64_t)denominator<<e):((uint64_t)n<<-e)<denominator)--e;
    uint64_t value=(uint64_t)n<<(23-e),q=value/denominator,r=value%denominator;
    if(r*2u>denominator || (r*2u==denominator && (q&1u)))++q;
    if(q==(UINT64_C(1)<<24)){q>>=1;++e;}
    return (uint32_t)(e+127)<<23 | ((uint32_t)q&0x7fffffu);
}
static uint32_t image(FxNativeDisplay *d,const FxSceneCommand *c) {
    FxSceneImage source;memcpy(&source,d->scene->data+c->rgb,sizeof(source));
    uint32_t vw=d->viewport[2]-d->viewport[0],vh=d->viewport[3]-d->viewport[1];
    uint32_t left=d->viewport[0]+c->x*vw/d->scene->width;
    uint32_t right=d->viewport[0]+(c->x+c->glyph)*vw/d->scene->width;
    uint32_t top=d->height-d->viewport[1]-c->y*vh/d->scene->height;
    uint32_t bottom=d->height-d->viewport[1]-(c->y+c->scale)*vh/d->scene->height;
    if(left==right || top==bottom)return 0u;
    uint32_t handle=FN(vgCreateImage,uint32_t (*)(uint32_t,int32_t,int32_t,uint32_t))
        (193u,(int32_t)source.width,(int32_t)source.height,1u); /* sABGR8888: RGBA bytes on LE */
    if(!handle){uint32_t error=FN(vgGetError,uint32_t (*)(void))();return error?error:0x1002u;}
    /* A negative source stride converts top-down module pixels to OpenVG's
     * bottom-up image coordinates without rewriting the retained snapshot. */
    FN(vgImageSubData,void (*)(uint32_t,const void *,int32_t,uint32_t,int32_t,int32_t,int32_t,int32_t))
        (handle,d->scene->data+source.pixels+(source.height-1u)*source.stride,-(int32_t)source.stride,
         193u,0,0,(int32_t)source.width,(int32_t)source.height);
    uint32_t error=FN(vgGetError,uint32_t (*)(void))();
    if(!error) {
        uint32_t previous[9],matrix[9]={ratio(right-left,source.width),0u,0u,0u,
            ratio(top-bottom,source.height),0u,coordinate(left),coordinate(bottom),0x3f800000u};
        FN(vgSeti,void (*)(uint32_t,int32_t))(0x1100u,0x1401);
        FN(vgGetMatrix,void (*)(void *))(previous);
        FN(vgLoadMatrix,void (*)(const void *))(matrix);
        FN(vgDrawImage,void (*)(uint32_t))(handle);
        error=FN(vgGetError,uint32_t (*)(void))();
        FN(vgLoadMatrix,void (*)(const void *))(previous);
        FN(vgSeti,void (*)(uint32_t,int32_t))(0x1100u,0x1400);
    }
    FN(vgDestroyImage,void (*)(uint32_t))(handle);
    uint32_t cleanup=FN(vgGetError,uint32_t (*)(void))();return error?error:cleanup;
}
static uint32_t draw_body(void *object,void *progress) {
    (void)progress;
    FxNativeDisplay *d=object; /* Owned base object is the first field. */
    if(!d->scene->count) return 1u;
    bind(d);
    /* The Nikon paint service also establishes the graphics-service target;
     * GUI object binding alone only updates the GUI's selection metadata. */
    rectangle(d,0u,0u,d->scene->width,d->scene->height,d->scene->commands[0].rgb);
    uint32_t colors[20],color_count=0u;
    for(uint32_t i=1u;!d->exact_colors && i<d->scene->count;++i) {
        uint32_t color=palette(d->scene->commands[i].rgb),j;
        for(j=0u;j<color_count && colors[j]!=color;++j) {}
        if(j==color_count) {
            if(color_count>=20u) { d->error=2u;return 0u; }
            colors[color_count++]=color;
        }
    }
    FN(native_graphics_acquire,void (*)(void))(); /* Nikon graphics transaction lock */
    uint32_t scissor=FN(vgGeti,uint32_t (*)(uint32_t))(0x1131u);
    uint32_t matrix_mode=FN(vgGeti,uint32_t (*)(uint32_t))(0x1100u);
    uint32_t state_keys[5]={0x1102u,0x1104u,0x1105u,0x1130u,0x1170u},state_values[5];
    uint32_t state_defaults[5]={1u,0x2001u,0x1f00u,0u,0u};
    for(uint32_t i=0u;i<5u;++i)state_values[i]=FN(vgGeti,uint32_t (*)(uint32_t))(state_keys[i]);
    uint32_t old_clear[4],clear[4];
    FN(vgGetfv,void (*)(uint32_t,int32_t,void *))(0x1121u,4,old_clear);
    FN(native_rgba_float_conversion,void (*)(uint32_t,void *))
        ((d->scene->commands[0].rgb<<8)|255u,clear);
    /* The full-surface scene must not inherit a menu text box's clip. All
     * changed OpenVG state is restored within the original graphics lock. */
    FN(vgSeti,void (*)(uint32_t,int32_t))(0x1131u,0);
    FN(vgSeti,void (*)(uint32_t,int32_t))(0x1100u,0x1400);
    uint32_t old_path_matrix[9],identity[9]={0x3f800000u,0u,0u,0u,0x3f800000u,0u,0u,0u,0x3f800000u};
    FN(vgGetMatrix,void (*)(void *))(old_path_matrix);
    FN(vgLoadMatrix,void (*)(const void *))(identity);
    for(uint32_t i=0u;i<5u;++i)FN(vgSeti,void (*)(uint32_t,int32_t))(state_keys[i],(int32_t)state_defaults[i]);
    FN(vgSetfv,void (*)(uint32_t,int32_t,const void *))(0x1121u,4,clear);
    FN(vgClear,void (*)(int32_t,int32_t,int32_t,int32_t))
        (0,0,(int32_t)d->width,(int32_t)d->height);
    if(d->exact_colors) {
        /* Only adjacent equal colors merge. Overlapping commands retain the
         * module's order; arbitrary RGB is never quantized by this service. */
        for(uint32_t begin=1u;begin<d->scene->count && !d->error;) {
            if(d->scene->commands[begin].kind==FX_SCENE_IMAGE) {
                d->error=image(d,&d->scene->commands[begin++]);continue;
            }
            uint32_t end=begin+1u,rgb=d->scene->commands[begin].rgb;
            while(end<d->scene->count && d->scene->commands[end].kind!=FX_SCENE_IMAGE && d->scene->commands[end].rgb==rgb)++end;
            d->error=paths(d,rgb,begin,end);begin=end;
        }
    } else for(uint32_t i=0u;i<color_count && !d->error;++i)d->error=paths(d,colors[i],1u,d->scene->count);
    FN(vgSetfv,void (*)(uint32_t,int32_t,const void *))(0x1121u,4,old_clear);
    FN(vgLoadMatrix,void (*)(const void *))(old_path_matrix);
    for(uint32_t i=0u;i<5u;++i)FN(vgSeti,void (*)(uint32_t,int32_t))(state_keys[i],(int32_t)state_values[i]);
    FN(vgSeti,void (*)(uint32_t,int32_t))(0x1100u,(int32_t)matrix_mode);
    FN(vgSeti,void (*)(uint32_t,int32_t))(0x1131u,(int32_t)scissor);
    FN(native_graphics_release,void (*)(void))();
    if(d->error) return 0u;
    ++d->callbacks;
    return 1u;
}
static uint32_t draw(void *object,void *progress) {
    FxNativeDisplay *d=object;if(!d->gate)return draw_body(object,progress);
    uint64_t ticket=atomic_load_explicit(d->gate,memory_order_acquire);
    if(!(ticket&1u))return 1u;
    atomic_fetch_add_explicit(&d->readers,1u,memory_order_acquire);
    uint32_t result=1u;
    if(atomic_load_explicit(d->gate,memory_order_acquire)==ticket &&
       is_current(d))result=draw_body(object,progress);
    atomic_fetch_sub_explicit(&d->readers,1u,memory_order_release);return result;
}
static FxResult validate(const FxDemoScene *s) {
    if(!s || !s->commands || s->width!=1280u || s->height!=800u ||
       s->capacity!=FX_DEMO_DRAW_CAPACITY || !s->count || s->count>s->capacity) return FX_BOUNDS;
    uint32_t colors[20],color_count=0u;
    for(uint32_t i=0;i<s->count;++i) {
        const FxDemoDraw *c=&s->commands[i];
        if(c->rgb>0xffffffu) return FX_BOUNDS;
        if(c->kind==FX_DEMO_CLEAR) {
            if(i || c->x || c->y || c->glyph || c->scale) return FX_INTEGRITY;
        } else if(c->kind!=FX_DEMO_GLYPH || c->glyph<32u || c->glyph>126u ||
                  c->scale<1u || c->scale>4u || c->x+5u*c->scale>s->width ||
                  c->y+7u*c->scale>s->height) return FX_BOUNDS;
        if(i) {
            uint32_t rgb=palette(c->rgb),j=0u;
            while(j<color_count && colors[j]!=rgb) ++j;
            if(j==color_count) {
                if(color_count==20u) return FX_BOUNDS;
                colors[color_count++]=rgb;
            }
        }
    }
    return s->commands[0].kind==FX_DEMO_CLEAR?FX_OK:FX_INTEGRITY;
}
static void invalidate(void *object,uint32_t depth,uint32_t *budget) {
    if(!*budget || depth>8u || !native_ram(object,0xc0u)) return;
    --*budget;
    /* The profile's gui_mark_object_dirty is Nikon's CONTAINER child-dirty
     * operation (C1.10 403360d4 / C5.32 4032a030), not a base setter. Calling
     * it on a text/image leaf treats unrelated fields as a child list. Mark
     * this actual base object; recurse only through recognized native classes. */
    ((uint8_t *)object)[0x3cu]=1u;
    uintptr_t callback=*(uintptr_t *)((uint8_t *)object+0x48u);
    uint32_t offset,wrapped;
    if(callback==fx_native_display_profile.gui_draw_body ||
       (!callback && !*(uintptr_t *)((uint8_t *)object+0xb0u))) { offset=0xc8u;wrapped=0u; }
    else if(callback==fx_native_display_profile.compound_draw || callback==fx_native_display_profile.second_compound_draw) {
        offset=0xd8u;wrapped=1u;
    } else if(callback==fx_native_display_profile.row_draw) {
        /* Native row widgets contain cached image/text children, including
         * sidebar icons. Marking only the row cannot repaint those children. */
        offset=(uint32_t)fx_native_display_profile.row_children_offset;wrapped=0u;
    } else return;
    if(!native_ram(object,offset+32u)) return;
    void *list=(uint8_t *)object+offset;
    uint32_t count=FN(native_list_count,uint32_t (*)(void *))(list);
    if(count>256u) return;
    void *cursor=FN(native_list_begin,void *(*)(void *))(list);
    for(uint32_t i=0u;i<count && *budget;++i) {
        void *child=FN(native_list_next,void *(*)(void *,void **))(list,&cursor);
        if(wrapped) {
            if(!native_ram(child,8u)) return;
            child=*(void **)child;
        }
        invalidate(child,depth+1u,budget);
    }
}
/* Native pages share menu surfaces. The category icons belong to an ancestor
 * page, outside the current Fenestra page's object lists. C1.10's own redraw
 * walks the parent at+8 from oldest to newest (403e8828..403e8888). */
static FxResult restore_pages(void *page) {
    void *pages[8];uint32_t count=0u,budget=1024u;
    while(page && count<8u && native_ram(page,96u)) {
        for(uint32_t i=0u;i<count;++i) if(pages[i]==page) return FX_INTEGRITY;
        pages[count++]=page;
        page=*(void **)((uint8_t *)page+8u);
    }
    if(page) return FX_INTEGRITY; /* Reject a malformed/truncated parent chain. */
    while(count) {
        page=pages[--count];
        for(uint32_t offset=24u;offset<=32u;offset+=8u) {
            void *surface=*(void **)((uint8_t *)page+offset);
            if(!native_ram(surface,0x118u)) continue;
            invalidate(surface,0u,&budget);
            if(!*(uintptr_t *)((uint8_t *)surface+0x48u) && !*(uintptr_t *)((uint8_t *)surface+0xb0u)) {
                /* The stock menu root owns children without a draw callback.
                 * The general object dispatcher deliberately skips that root. */
                FN(gui_bind_target,void (*)(void *))(surface);
                (void)FN(gui_draw_body,uint32_t (*)(void *,void *))(surface,0);
            } else (void)FN(native_object_dispatch,uint32_t (*)(void *,void *,void *))(surface,0,0);
        }
    }
    return budget?FX_OK:FX_BUDGET;
}
FxResult fx_native_menu_restore(const FxNativePage *page) {
    if(!page || !native_ram(page,sizeof(*page)))return FX_ARGUMENT;
    if(fx_native_page_modal(page))return fx_native_backdrop_restore(page);
    if(!native_ram((void *)page->surface,0x118u))return FX_STATE;
    FN(gui_bind_target,void (*)(void *))((void *)page->surface);
    uint32_t size=FN(gui_target_dimensions,uint32_t (*)(void *))((void *)page->surface);
    uint32_t width=size&65535u,height=size>>16;
    if(!width || !height || width>4096u || height>4096u)return FX_BOUNDS;
    /* Nikon rows do not paint their spacing or the area vacated by a keyboard.
     * Clear the native menu target, then restore stock chrome and widget trees.
     * The original graphics service owns target selection, clipping and paint. */
    const uint16_t rect[4]={0u,0u,(uint16_t)width,(uint16_t)height};
    const struct {uint32_t type,reserved;uint64_t color;} brush={2u,0u,255u};
    FN(native_constant_paint_rectangle,void (*)(const uint16_t *,const uint32_t *,const void *,void *))
        (rect,&size,&brush,0);
    return restore_pages((void *)(uintptr_t)page);
}
static void describe(FxNativeDisplay *d,void *object,uint32_t depth) {
    if(d->original_object_count>=32u || depth>3u || !native_ram(object,0xc0u)) return;
    for(uint32_t i=0u;i<d->original_object_count;++i)
        if(d->original_objects[i][0]==(uintptr_t)object) return;
    uint64_t *row=d->original_objects[d->original_object_count++];
    row[0]=(uintptr_t)object;
    row[1]=*(uint64_t *)object;
    row[2]=*(uintptr_t *)((uint8_t *)object+0x48u);
    row[3]=*(uintptr_t *)((uint8_t *)object+0xb0u);
    uint32_t offset,wrapped;
    if(row[2]==fx_native_display_profile.gui_draw_body) { offset=0xc8u;wrapped=0u; }
    else if(row[2]==fx_native_display_profile.compound_draw || row[2]==fx_native_display_profile.second_compound_draw) { offset=0xd8u;wrapped=1u; }
    else if(row[2]==fx_native_display_profile.row_draw) { offset=(uint32_t)fx_native_display_profile.row_children_offset;wrapped=0u; }
    else return;
    void *list=(uint8_t *)object+offset;
    uint32_t count=FN(native_list_count,uint32_t (*)(void *))(list);
    if(count>256u) return;
    void *cursor=FN(native_list_begin,void *(*)(void *))(list);
    for(uint32_t i=0u;i<count && d->original_object_count<32u;++i) {
        void *child=FN(native_list_next,void *(*)(void *,void **))(list,&cursor);
        if(wrapped) { if(!native_ram(child,8u)) return;child=*(void **)child; }
        describe(d,child,depth+1u);
    }
}
/* Nikon lazily constructs this reserved foreground root. Its original callers
 * pass a copied 0x80-byte descriptor, including the native graphics tuple.
 * Use that constructor only for a wholly uninitialized root. No camera setting
 * service or ROM write is involved. An initialized foreign object is retained. */
static FxResult initialize_global(void *surface) {
    const uint8_t *bytes=surface;
    for(uint32_t i=0u;i<0x118u;++i)if(bytes[i])return FX_OK;
    uint32_t device=FN(native_graphics_current_device,uint32_t (*)(void))();
    uint32_t target=FN(native_global_target,uint32_t (*)(uint32_t))
        ((uint32_t)fx_native_display_profile.global_surface_index);
    uint16_t viewport[4];
    if(!(FN(native_graphics_backing_rectangle,uint32_t (*)(uint32_t,uint32_t,uint32_t,void *))
        (device,1u,target,viewport)&1u) || viewport[0]>=viewport[2] || viewport[1]>=viewport[3] ||
        viewport[2]>4096u || viewport[3]>4096u)return FX_CAPABILITY;
    _Alignas(8) uint8_t descriptor[0x80]={0};
    *(uint32_t *)(void *)descriptor=0x7fdfu;
    uint16_t *bounds=(uint16_t *)(void *)(descriptor+0x10u);
    bounds[2]=viewport[2];bounds[3]=viewport[3];descriptor[0x20u]=1u;
    uint32_t *binding=(uint32_t *)(void *)(descriptor+0x70u);
    binding[0]=device;binding[1]=1u;binding[2]=target;
    FN(gui_surface_initialize,void (*)(void *,const void *))(surface,descriptor);
    return FX_OK;
}
FxResult fx_native_display_attach(FxNativeDisplay *d,void *page,const FxDemoScene *scene) {
    if(!d || d->attached || (page && !native_ram(page,96u)) || !scene) return FX_STATE;
    if(!page && !(fx_native_display_capabilities()&FX_SCENE_CAP_GLOBAL))return FX_CAPABILITY;
    lock();
    void *current=FN(native_current_page,void *(*)(void))();
    /* Own a last foreground object. Painting the shared background destroys
     * permanent chrome whose objects are outside the active subpage tree. */
    void *surface;
    d->global_overlay=page?0u:1u;
    if(page) surface=*(void **)((uint8_t *)page+32u);
    else {
        FxNativeView view=fx_native_current_view();
        if(view.id!=fx_native_display_profile.shooting_view_id || view.state!=1u) { unlock();return FX_STATE; }
        d->application=view.address;
        /* The profile names the initialized native foreground root. Its
         * backing allocation can be square: obtain the visible source rect
         * from Nikon's graphics service instead of treating it as LCD size. */
        surface=FN(native_global_surface,void *(*)(void *,uint32_t))
            (0,(uint32_t)fx_native_display_profile.global_surface_index);
    }
    if((page && current!=page) || !native_ram(surface,0x118u)) { unlock();return FX_STATE; }
    if(d->global_overlay) {
        FxResult initialized=initialize_global(surface);
        if(initialized!=FX_OK){unlock();return initialized;}
    }
    if(d->global_overlay &&
       (*(uintptr_t *)((uint8_t *)surface+0xb0u)!=fx_native_display_profile.gui_extended_draw_body ||
        *(uint32_t *)((uint8_t *)surface+0x10cu)!=1u ||
        *(uint32_t *)((uint8_t *)surface+0x108u)!=FN(native_graphics_current_device,uint32_t (*)(void))() ||
        *(uint32_t *)((uint8_t *)surface+0x110u)!=FN(native_global_target,uint32_t (*)(uint32_t))
            ((uint32_t)fx_native_display_profile.global_surface_index))) { unlock();return FX_CAPABILITY; }
    if(d->gate && FN(native_menu_find_row_by_id,void *(*)(uint32_t,void *))(0x7fe0u,surface)) {
        unlock();return FX_BUSY; /* Never insert a duplicate native object. */
    }
    d->page=page;d->surface=surface;d->scene=scene;
    void *ancestors[8],*ancestor=page;uint32_t ancestor_count=0u;
    while(ancestor_count<8u && native_ram(ancestor,96u)) {
        ancestors[ancestor_count++]=ancestor;
        ancestor=*(void **)((uint8_t *)ancestor+8u);
    }
    while(ancestor_count) {
        ancestor=ancestors[--ancestor_count];
        describe(d,*(void **)((uint8_t *)ancestor+24u),0u);
        describe(d,*(void **)((uint8_t *)ancestor+32u),0u);
    }
    describe(d,surface,0u);
    memset(d->object,0,sizeof(d->object));
    *(uint32_t *)(void *)d->object=0x7fe0u; /* Separate from every native menu row. */
    *(uintptr_t *)(void *)(d->object+0x40u)=(uintptr_t)surface;
    FN(gui_base_initialize,void (*)(void *))(d->object);
    *(uintptr_t *)(void *)(d->object+0x48u)=(uintptr_t)draw;
    d->object[0x28u]=d->object[0x3cu]=1u;
    bind(d);
    uint32_t size=FN(gui_target_dimensions,uint32_t (*)(void))( );
    d->width=size&65535u;d->height=size>>16;
    if(!d->width || !d->height || d->width>4096u || d->height>4096u) {
        d->error=1u;unlock();return FX_CAPABILITY;
    }
    d->viewport[0]=d->viewport[1]=0u;
    d->viewport[2]=(uint16_t)d->width;d->viewport[3]=(uint16_t)d->height;
    if(d->global_overlay) {
        const uint32_t *target=(const uint32_t *)((const uint8_t *)surface+0x108u);
        memcpy(d->target,target,sizeof(d->target));
        /* Native composition rectangle within the backing allocation.
         * The paired getter at4034d770 describes logical output coordinates;
         * native callers obtain both (40338e70..40338e94). */
        uint32_t valid=FN(native_graphics_backing_rectangle,uint32_t (*)(uint32_t,uint32_t,uint32_t,void *))
            (target[0],target[1],target[2],d->viewport);
        if(!(valid&1u) || d->viewport[0]>=d->viewport[2] || d->viewport[1]>=d->viewport[3] ||
           d->viewport[2]>d->width || d->viewport[3]>d->height) {
            d->error=3u;unlock();return FX_CAPABILITY;
        }
    }
    uint16_t *rect=(uint16_t *)(void *)(d->object+0x10u);
    rect[0]=rect[1]=0u;rect[2]=(uint16_t)d->width;rect[3]=(uint16_t)d->height;
    /* Same list insertion API used by the original menu. No fixed RAM slot. */
    uint32_t added=FN(gui_object_insert,uint32_t (*)(void *,void *))(surface,d->object);
    d->attached=added&1u;
    if(d->attached && d->global_overlay) {
        d->original_visible=FN(gui_get_visible,uint32_t (*)(void *))(surface)&1u;
        d->target_was_enabled=FN(native_graphics_target_enabled,uint32_t (*)(uint32_t,uint32_t,uint32_t))
            (d->target[0],d->target[1],d->target[2]);
        FN(native_graphics_target_enable,void (*)(uint32_t,uint32_t,uint32_t,uint32_t))
            (d->target[0],d->target[1],d->target[2],1u);
        FN(gui_set_visible,void (*)(void *,uint32_t))(surface,1u);
    }
    unlock();
    return d->attached?FX_OK:FX_BUDGET;
}
FxResult fx_native_display_attach_global(FxNativeDisplay *d,const FxDemoScene *scene) {
    return fx_native_display_attach(d,0,scene);
}
void *fx_native_display_current_page(uint32_t expected_id) {
    lock();void *page=FN(native_current_page,void *(*)(void))();
    if(!native_ram(page,96u) || (*(uint32_t *)page>>16)!=expected_id)page=0;
    unlock();return page;
}
FxResult fx_native_display_present(FxNativeDisplay *d,const FxDemoScene *scene) {
    if(!d || !d->attached) return FX_STATE;
    FxResult r=d->exact_colors?fx_scene_validate(scene):validate(scene);
    if(r!=FX_OK) return r;
    lock();
    if(!is_current(d)) { unlock();return FX_STATE; }
    d->scene=scene;
    d->object[0x3cu]=1u;
    /* Original extended GUI renderer, under the native GUI mutex.
     * Native callbacks run only after private EL1 execution has returned. */
    uint32_t before=d->callbacks;
    uint32_t rendered=FN(gui_extended_draw_body,uint32_t (*)(void *,void *,void *))(d->surface,0,0);
    if(d->gate && !(atomic_load_explicit(d->gate,memory_order_acquire)&1u))r=FX_STATE;
    else if(!(rendered&1u) || d->callbacks!=before+1u || d->error) r=FX_IO;
    else ++d->frames;
    unlock();
    if(r==FX_OK) {
        /* Native menu callers follow object drawing with this compositor
         * submission (403b6ef0..403b6f20, 4060ad6c..4060ad98). Drawing into a
         * Nikon surface alone does not publish it to the physical LCD path. */
        FN(native_menu_compositor_submit,void (*)(void *,uint32_t,uint32_t))
            ((void *)(uintptr_t)fx_native_display_profile.native_menu_progress,3u,7u);
    }
    return r;
}
void fx_native_display_detach(FxNativeDisplay *d) {
    if(!d || !d->attached) return;
    lock();
    if(d->attached) {
        uint32_t owns_target=!d->global_overlay || owns_global_target(d);
        if(owns_target)bind(d);
        const uint16_t rect[4]={0u,0u,(uint16_t)d->width,(uint16_t)d->height};
        const uint32_t size=d->width|(d->height<<16);
        if(d->global_overlay && owns_target) {
            /* Original shooting UI can leave a restricted graphics clip.
             * Select this native target and clear it with scissoring disabled,
             * then restore graphics state before native objects redraw. */
            rectangle(d,0u,0u,d->scene->width,d->scene->height,0u);
            FN(native_graphics_acquire,void (*)(void))();
            uint32_t scissor=FN(vgGeti,uint32_t (*)(uint32_t))(0x1131u);
            uint32_t old_clear[4],transparent[4]={0u,0u,0u,0u};
            FN(vgGetfv,void (*)(uint32_t,int32_t,void *))(0x1121u,4,old_clear);
            FN(vgSeti,void (*)(uint32_t,int32_t))(0x1131u,0);
            FN(vgSetfv,void (*)(uint32_t,int32_t,const void *))(0x1121u,4,transparent);
            FN(vgClear,void (*)(int32_t,int32_t,int32_t,int32_t))
                (0,0,(int32_t)d->width,(int32_t)d->height);
            FN(vgSetfv,void (*)(uint32_t,int32_t,const void *))(0x1121u,4,old_clear);
            FN(vgSeti,void (*)(uint32_t,int32_t))(0x1131u,(int32_t)scissor);
            if(FN(vgGetError,uint32_t (*)(void))()) d->error=4u;
            FN(native_graphics_release,void (*)(void))();
        } else if(!d->global_overlay)
            FN(native_transparent_clear_rectangle,void (*)(const uint16_t *,const uint32_t *,void *))(rect,&size,0);
        (void)FN(gui_object_remove,uint32_t (*)(void *,void *))(d->surface,d->object);
        if(d->gate && FN(native_menu_find_row_by_id,void *(*)(uint32_t,void *))(0x7fe0u,d->surface)==d->object) {
            unlock();return; /* Keep attached/pinned until removal is observed. */
        }
        d->attached=0u;
        if(d->global_overlay && owns_target) {
            uint32_t budget=1024u;
            invalidate(d->surface,0u,&budget);
            FN(gui_set_visible,void (*)(void *,uint32_t))(d->surface,d->original_visible);
            FN(native_graphics_target_enable,void (*)(uint32_t,uint32_t,uint32_t,uint32_t))
                (d->target[0],d->target[1],d->target[2],d->target_was_enabled);
            (void)FN(gui_extended_draw_body,uint32_t (*)(void *,void *,void *))(d->surface,0,0);
        }
        void *page=FN(native_current_page,void *(*)(void))();
        restore_pages(page);
        ++d->restores;
    }
    unlock();
    FN(native_menu_compositor_submit,void (*)(void *,uint32_t,uint32_t))
        ((void *)(uintptr_t)fx_native_display_profile.native_menu_progress,3u,7u);
}
