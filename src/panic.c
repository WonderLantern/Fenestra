/* Loader-owned error page. Rendered through the Nikon GUI/OpenVG backend,
 * after native system registers have been restored; no faulty module call. */
#include "fenestra_panic.h"
#define WHITE 0xf0f0f0u
#define GREY 0xb8b8b8u
#define RED 0xff6868u
static int text(FxScene *s,uint32_t x,uint32_t y,uint32_t scale,uint32_t rgb,const char *v) {
    while(*v) {
        uint32_t glyph=(uint8_t)*v++;
        if(glyph>='a' && glyph<='z') glyph-='a'-'A';
        if(s->count>=s->capacity || x+5u*scale>s->width || y+7u*scale>s->height) return 0;
        s->commands[s->count++]=(FxSceneCommand){FX_SCENE_GLYPH,(uint16_t)x,(uint16_t)y,
            (uint16_t)glyph,scale,rgb};
        x+=6u*scale;
    }
    return 1;
}
static int number(FxScene *s,uint32_t y,const char *label,uint64_t value,uint32_t digits) {
    char hex[19]={'0','X'};
    for(uint32_t i=0u;i<digits;++i) hex[2u+i]="0123456789ABCDEF"[(value>>((digits-1u-i)*4u))&15u];
    hex[2u+digits]=0;
    return text(s,48u,y,3u,GREY,label) && text(s,300u,y,3u,WHITE,hex);
}
FxResult fx_panic_scene(FxScene *s,const FxPanicReport *p) {
    if(!s || !p || !p->module || !s->commands || s->width!=1280u || s->height!=800u ||
       s->capacity<FX_PANIC_COMMANDS) return FX_BOUNDS;
    s->count=0u;
    s->commands[s->count++]=(FxSceneCommand){FX_SCENE_CLEAR,0u,0u,0u,0u,0x101014u};
    const char *reason=p->error==FX_TIMEOUT?"MODULE DEADLINE EXCEEDED":p->request_sequence?"MODULE REQUESTED PANIC":p->software_interrupts?"SOFTWARE PANIC INTERRUPT":!p->cpu_faults?
        (p->return_valid && p->module_result?"MODULE RETURNED AN ERROR":"LOADER OR MODULE OUTPUT FAILURE"):
        (p->esr>>26==0x24u || p->esr>>26==0x25u)?"MODULE DATA ABORT":p->esr>>26==0u?"UNDEFINED INSTRUCTION":"MODULE CPU EXCEPTION";
    int ok=text(s,48u,28u,4u,RED,"FENESTRA PANIC") &&
        text(s,48u,98u,3u,GREY,"MODULE:") && text(s,210u,98u,3u,WHITE,p->module) &&
        text(s,48u,148u,3u,RED,reason) &&
        number(s,210u,"ERROR",(uint32_t)p->error,8u) &&
        ((!p->return_valid)?(text(s,48u,252u,3u,GREY,"RETURN") && text(s,300u,252u,3u,WHITE,"NO RETURN")):
            number(s,252u,"RETURN",(uint32_t)p->module_result,8u)) &&
        number(s,294u,"ESR",p->esr,16u) && number(s,336u,"ELR",p->elr,16u) &&
        number(s,378u,"FAR",p->far,16u) &&
        number(s,420u,p->request_sequence?"SEQUENCE":"REQUEST",p->request_sequence?p->request_sequence:p->interrupt_reason,8u) &&
        (!p->request_sequence || (number(s,454u,"ARG0",p->request_args[0],16u) &&
            number(s,488u,"ARG1",p->request_args[1],16u) && number(s,522u,"ARG2",p->request_args[2],16u) &&
            number(s,556u,"ARG3",p->request_args[3],16u))) &&
        text(s,48u,606u,3u,WHITE,"EXTENSION STOPPED. MODULE WILL NOT RUN AGAIN.") &&
        text(s,48u,650u,3u,GREY,"RESOURCES RETAINED UNTIL RESTART.") &&
        text(s,48u,706u,3u,WHITE,"POWER OFF THE CAMERA TO RECOVER.") &&
        text(s,48u,750u,3u,GREY,"RESTART CAMERA TO USE FENESTRA AGAIN.");
    if(!ok)s->count=0u;
    return ok?FX_OK:FX_BOUNDS;
}

void fx_panic_service_request(FxPanicReport *report,const FxServiceOwner *owner) {
    if(!report || !owner || !owner->fault || owner->active || !owner->last_sequence ||
       owner->last_request.operation!=FX_S_PANIC || owner->last_request.sequence!=owner->last_sequence ||
       owner->last_reply.sequence!=owner->last_sequence || owner->last_reply.result!=FX_STATE)return;
    report->request_sequence=owner->last_sequence;
    for(uint32_t i=0u;i<4u;++i)report->request_args[i]=owner->last_request.arg[i];
}
