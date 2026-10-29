#include "fenestra_native_text.h"
#include "fenestra_text_fit.h"
#include "fenestra_memory.h"
static int ram(const void *p, uint32_t n) {
    uintptr_t a = (uintptr_t)p;
    return p && !(a & 7u) && a >= UINT64_C(0xa00000000) && a <= UINT64_C(0xa40000000) - n;
}
typedef struct { const FxNativeTextProfile *profile;void *objects[2]; } TextMeasure;
static int32_t measure(void *context,uint32_t column) {
    TextMeasure *m=context;void *object=m->objects[column];
    if(!object)return -1;
    (void)((uint32_t (*)(void *))m->profile->release_text)(object);
    int16_t dimensions[2]={0,0};
    return ((uint32_t (*)(void *,int16_t *))m->profile->measure)(object,dimensions)?dimensions[0]:-1;
}
FxResult fx_native_text_prepare(const FxNativeTextProfile *p, FxNativePage *page, uint32_t id, uint32_t label,
                                uint32_t value, int enabled,FxNativeUi *ui) {
    if (!p || !ram(page, sizeof(*page)) || !p->current_page || !p->row_lookup || !p->value_lookup ||
        !p->set_disabled || !p->set_text || !p->release_text || !p->set_alignment || !p->row_draw || p->row_bytes < 0x70u ||
        p->value_bytes < 0x70u)
        return FX_ARGUMENT;
    if (((void *(*)(void))p->current_page)() != page || !ram(fx_native_page_rows(page), 0xd8u))
        return FX_STATE;
    void *row = ((void *(*)(uint32_t, void *))p->row_lookup)(id, fx_native_page_rows(page));
    if (!ram(row, p->row_bytes) || *(uintptr_t *)((uint8_t *)row + 0x48u) != p->row_draw)
        return FX_STATE;
    ((void (*)(void *, uint32_t))p->set_disabled)(row, (uint32_t)!enabled);
    void *object=0;
    uint32_t index=ui?id-ui->row_base:FX_UI_CONTROLS;
    int fitting=ui && index<FX_UI_CONTROLS && label==ui->text_base+FX_NATIVE_UI_ROW_LABELS+index;
    if(fitting) {
        memcpy(ui->row_labels[index],ui->labels[index],sizeof(ui->labels[index]));
        memcpy(ui->row_values[index],ui->values[index],sizeof(ui->values[index]));
    }
    if (value) {
        object = ((void *(*)(void *, uint32_t))p->value_lookup)(row, p->value_child);
        if (!ram(object, p->value_bytes))
            return FX_STATE;
        ((void (*)(void *, uint32_t, uint32_t))p->set_text)(object, value, 2u);
        /* Native source-2 alignment 2 anchors values at the right edge.
         * The copied stock template centers its short firmware value. */
        ((void (*)(void *, uint32_t))p->set_alignment)(object, 2u);
        (void)((uint32_t (*)(void *))p->release_text)(object);
        ((void (*)(void *, uint32_t))p->set_disabled)(object, (uint32_t)!enabled);
    }
    uint32_t *words = row;
    if (words[2] == label && words[3] == 2u)
        (void)((uint32_t (*)(void *))p->release_text)(row);
    if(fitting && p->measure && p->bounds && words[2]==label && words[3]==2u) {
        int16_t row_bounds[4],value_bounds[4];
        ((void (*)(void *,int16_t *))p->bounds)(row,row_bounds);
        ((void (*)(void *,int16_t *))p->bounds)(object?object:row,value_bounds);
        /* Original design coordinates, with room for native label inset and
         * the arrow beyond the value's right edge. No framebuffer pixels. */
        int32_t width=(int32_t)value_bounds[2]-row_bounds[0]-16;
        if(width>16) {
            TextMeasure m={p,{row,object}};uint16_t empty[FX_UI_TEXT]={0};
            FxResult result=fx_text_fit_pair(ui->row_labels[index],object?ui->row_values[index]:empty,
                                            width,16,measure,&m);
            (void)((uint32_t (*)(void *))p->release_text)(row);
            if(object)(void)((uint32_t (*)(void *))p->release_text)(object);
            if(result!=FX_OK)return result;
        }
    }
    return FX_OK;
}
