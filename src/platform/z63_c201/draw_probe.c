/* Native renderer bring-up on the current owned Fenestra page, after EL0. */
#include "fenestra_native_display.h"
#include "fenestra_memory.h"
#include "task.h"
extern void fx_z63_probe_module_result(uint32_t,uint32_t,uint64_t);
void fx_z63_draw_probe(void) {
    typedef struct {FxNativeDisplay display;FxScene scene;FxSceneCommand commands[5];} Draw;
    Draw *s=((void *(*)(uint32_t,uint32_t))(uintptr_t)0x40d32d00u)((uint32_t)sizeof(*s),0u);
    if(!s){fx_z63_probe_module_result(80u,FX_BUDGET,0u);return;}
    memset(s,0,sizeof(*s));
    s->commands[0]=(FxSceneCommand){FX_SCENE_CLEAR,0,0,0,0,0x202030};
    s->commands[1]=(FxSceneCommand){FX_SCENE_RECT,30,180,220,90,0x00ff00};
    s->commands[2]=(FxSceneCommand){FX_SCENE_GLYPH,320,180,'Z',4,0xffffff};
    s->commands[3]=(FxSceneCommand){FX_SCENE_GLYPH,350,180,'6',4,0xffffff};
    s->commands[4]=(FxSceneCommand){FX_SCENE_GLYPH,380,180,'3',4,0xffffff};
    s->scene=(FxScene){720u,480u,5u,5u,s->commands,0,0,0};s->display.exact_colors=1u;
    void *page=fx_native_display_current_page(0x7000u);
    FxResult r=fx_native_display_attach(&s->display,page,&s->scene);
    fx_z63_probe_module_result(80u,(uint32_t)r,((uint64_t)s->display.width<<32)|s->display.height);
    if(r!=FX_OK)return;
    for(uint32_t frame=0;frame<3u;++frame) {
        s->commands[1].glyph=(uint16_t)(220u+frame*30u);
        r=fx_native_display_present(&s->display,&s->scene);
        fx_z63_probe_module_result(81u,(uint32_t)r,((uint64_t)s->display.callbacks<<32)|s->display.error);
        if(r!=FX_OK)break;
        (void)fx_z63_task_delay_ticks(100u);
    }
    for(uint32_t i=0;i<8u;++i)(void)fx_z63_task_delay_ticks(1000u);
    fx_native_display_detach(&s->display);
    fx_z63_probe_module_result(82u,s->display.error,s->display.frames);
}
