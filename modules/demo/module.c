#include "fenestra_demo.h"

typedef struct {
    uint8_t glyph, level, target, locked;
    uint16_t reveal_at, release_at;
} Character;
/* The ONLY character container: rain and the word use these same cells.
 * No text overlay, logo sprite, second particle list or allocation. */
static Character cells[FX_DEMO_CELLS];
static uint32_t seed, initialized, target_count, cycles;
static const char alphabet[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-";
static const char word[]="wonderlantern";
static int draw(const FxDemoServices *s,uint16_t kind,uint32_t x,uint32_t y,
                char glyph,uint32_t scale,uint32_t rgb) {
    if(s->scene) {
        FxDemoScene *scene=s->scene;
        if(scene->count>=scene->capacity || x>=scene->width || y>=scene->height) return -3;
        scene->commands[scene->count++]=(FxDemoDraw){kind,(uint16_t)x,(uint16_t)y,
            (uint16_t)(uint8_t)glyph,scale,rgb};
    } else if(kind==FX_DEMO_CLEAR) fx_clear(s->canvas,rgb);
    else fx_char(s->canvas,x,y,glyph,scale,rgb);
    return 0;
}
static int text(const FxDemoServices *s,uint32_t x,uint32_t y,const char *value,uint32_t rgb) {
    while(*value) {
        if(draw(s,FX_DEMO_GLYPH,x,y,*value++,2u,rgb)) return -3;
        x+=12u;
    }
    return 0;
}
static uint32_t random_word(void) {
    seed^=seed<<13; seed^=seed>>17; seed^=seed<<5; return seed;
}
static void initialize(void) {
    uint32_t i, letter, row, col, dy;
    seed=0x574c4658u;
    for (i=0; i<FX_DEMO_CELLS; ++i) {
        cells[i].glyph=(uint8_t)alphabet[random_word()%(sizeof(alphabet)-1u)];
        cells[i].level=(uint8_t)(random_word()%24u);
    }
    for (letter=0; letter<sizeof(word)-1u; ++letter)
        for (row=0; row<7u; ++row)
            for (col=0; col<5u; ++col)
                if (fx_glyph(word[letter],row)&(1u<<(4u-col)))
                  for (dy=0u; dy<2u; ++dy) {
                    i=(11u+row*2u+dy)*FX_DEMO_COLUMNS+11u+letter*6u+col;
                    cells[i].target=1u;
                    cells[i].reveal_at=(uint16_t)(100u+random_word()%110u);
                    cells[i].release_at=(uint16_t)(355u+random_word()%65u);
                    ++target_count;
                }
    initialized=1u;
}
__attribute__((visibility("default")))
int fenestra_module_entry(const FxModuleServices *base) {
    const FxDemoServices *s;
    uint32_t phase,i,col,row,head,locked=0u;
    if (!base || base->abi!=1u || base->bytes<sizeof(FxDemoServices)) return -1;
    s=(const FxDemoServices *)(const void *)base;
    if (!s->stats || s->reserved) return -2;
    if(s->scene) {
        if(!s->scene->commands || s->scene->capacity<FX_DEMO_DRAW_CAPACITY ||
           s->scene->width!=1280u || s->scene->height!=800u) return -2;
        s->scene->count=0u;
    } else if (!fx_canvas_valid(s->canvas) || s->canvas->width!=1280u ||
        s->canvas->height!=800u) return -2;
    if (!initialized) initialize();
    phase=s->frame%FX_DEMO_CYCLE;
    if (phase==0u) ++cycles;
    for (i=0; i<FX_DEMO_CELLS; ++i) {
        Character *c=&cells[i];
        c->locked=(uint8_t)(c->target && phase>=c->reveal_at && phase<c->release_at);
        if (c->locked) { c->level=230u; ++locked; }
        else c->level=(uint8_t)(c->level>7u ? c->level-7u : 0u);
    }
    for (col=0; col<FX_DEMO_COLUMNS; ++col) {
        uint32_t speed=2u+col%4u;
        head=(s->frame/speed+(col*17u+col*col)%FX_DEMO_ROWS)%FX_DEMO_ROWS;
        i=head*FX_DEMO_COLUMNS+col;
        if (!cells[i].locked) {
            cells[i].glyph=(uint8_t)alphabet[random_word()%(sizeof(alphabet)-1u)];
            cells[i].level=255u;
        }
        i=((head+FX_DEMO_ROWS-1u)%FX_DEMO_ROWS)*FX_DEMO_COLUMNS+col;
        if (!cells[i].locked) cells[i].level=180u;
    }
    if(draw(s,FX_DEMO_CLEAR,0u,0u,0,0u,0x010705u)) return -3;
    for (row=0; row<FX_DEMO_ROWS; ++row)
        for (col=0; col<FX_DEMO_COLUMNS; ++col) {
            Character *c=&cells[row*FX_DEMO_COLUMNS+col];
            uint32_t level=c->level,color;
            if (!level) continue;
            if (c->locked) {
                /* The unchanged rain glyph becomes a bright logo cell. */
                color=0xd5ffe6u;
            } else {
                /* Negative space is still made of the same rain cells.
                 * Dim it as the word appears, then restore the moving rain. */
                uint32_t emphasis=phase<100u ? 0u : phase<140u ? phase-100u :
                    phase<355u ? 40u : phase<420u ? (420u-phase)*40u/65u : 0u;
                if (row>=10u && row<=25u && col>=8u && col<=90u)
                    level=level*(80u-emphasis)/80u;
                if (emphasis) level=level*3u/4u;
                if (level>=245u) color=0xc1ffe5u;
                else color=(level/14u)<<16 | level<<8 | level/3u;
            }
            if(draw(s,FX_DEMO_GLYPH,40u+col*12u,110u+row*16u,(char)c->glyph,2u,color)) return -3;
        }
    if(text(s,40u,32u,"FENESTRA / WONDERLANTERN",0x54af88u) ||
       text(s,904u,762u,"BACK: MODULES",0x3e8665u)) return -3;
    s->stats->frame=s->frame; s->stats->cell_count=FX_DEMO_CELLS;
    s->stats->target_count=target_count; s->stats->locked_count=locked;
    s->stats->cycles=cycles; s->stats->cell_pool_address=(uintptr_t)cells;
    if (base->log) {
        const char *message=phase==0u ? "DEMO_STAGE_RAIN" :
            phase==100u ? "DEMO_STAGE_ASSEMBLING" :
            phase==210u ? "DEMO_STAGE_WORD_HELD" :
            phase==420u ? "DEMO_STAGE_RELEASED" : 0;
        if (message) return base->log(base->context,message);
    }
    return 0;
}
