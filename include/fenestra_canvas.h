#ifndef FENESTRA_CANVAS_H
#define FENESTRA_CANVAS_H
#include <stdint.h>

/* Owned software YYUV+A surface. This is not a physical panel ABI. */
typedef struct {
    uint32_t width, height;
    uint8_t *yyuv, *alpha;
    uint32_t color_stride, alpha_stride;
} FxCanvas;

static inline uint8_t fx_glyph(char c, uint32_t row) {
    static const uint8_t font[128][7] = {
        ['A']={14,17,17,31,17,17,17}, ['B']={30,17,17,30,17,17,30},
        ['C']={14,17,16,16,16,17,14}, ['D']={30,17,17,17,17,17,30},
        ['E']={31,16,16,30,16,16,31}, ['F']={31,16,16,30,16,16,16},
        ['G']={14,17,16,23,17,17,15}, ['H']={17,17,17,31,17,17,17},
        ['I']={14,4,4,4,4,4,14}, ['J']={7,2,2,2,18,18,12},
        ['K']={17,18,20,24,20,18,17}, ['L']={16,16,16,16,16,16,31},
        ['M']={17,27,21,21,17,17,17}, ['N']={17,25,21,19,17,17,17},
        ['O']={14,17,17,17,17,17,14}, ['P']={30,17,17,30,16,16,16},
        ['Q']={14,17,17,17,21,18,13}, ['R']={30,17,17,30,20,18,17},
        ['S']={15,16,16,14,1,1,30}, ['T']={31,4,4,4,4,4,4},
        ['U']={17,17,17,17,17,17,14}, ['V']={17,17,17,17,17,10,4},
        ['W']={17,17,17,21,21,21,10}, ['X']={17,17,10,4,10,17,17},
        ['Y']={17,17,10,4,4,4,4}, ['Z']={31,1,2,4,8,16,31},
        ['0']={14,17,19,21,25,17,14}, ['1']={4,12,4,4,4,4,14},
        ['2']={14,17,1,2,4,8,31}, ['3']={30,1,1,14,1,1,30},
        ['4']={2,6,10,18,31,2,2}, ['5']={31,16,16,30,1,1,30},
        ['6']={14,16,16,30,17,17,14}, ['7']={31,1,2,4,8,8,8},
        ['8']={14,17,17,14,17,17,14}, ['9']={14,17,17,15,1,1,14},
        ['a']={0,0,14,1,15,17,15}, ['b']={16,16,22,25,17,17,30},
        ['c']={0,0,14,16,16,17,14}, ['d']={1,1,13,19,17,17,15},
        ['e']={0,0,14,17,31,16,14}, ['f']={6,9,8,28,8,8,8},
        ['g']={0,15,17,17,15,1,14}, ['h']={16,16,22,25,17,17,17},
        ['i']={4,0,12,4,4,4,14}, ['j']={2,0,6,2,2,18,12},
        ['k']={16,16,18,20,24,20,18}, ['l']={12,4,4,4,4,4,14},
        ['m']={0,0,26,21,21,17,17}, ['n']={0,0,22,25,17,17,17},
        ['o']={0,0,14,17,17,17,14}, ['p']={0,0,30,17,30,16,16},
        ['q']={0,0,15,17,15,1,1}, ['r']={0,0,22,25,16,16,16},
        ['s']={0,0,15,16,14,1,30}, ['t']={8,8,28,8,8,9,6},
        ['u']={0,0,17,17,17,19,13}, ['v']={0,0,17,17,17,10,4},
        ['w']={0,0,17,17,21,21,10}, ['x']={0,0,17,10,4,10,17},
        ['y']={0,0,17,17,15,1,14}, ['z']={0,0,31,2,4,8,31},
        ['>']={16,8,4,2,4,8,16}, ['/']={1,2,2,4,8,8,16},
        ['-']={0,0,0,31,0,0,0}, ['_']={0,0,0,0,0,0,31},
        ['.']={0,0,0,0,0,12,12}, [':']={0,12,12,0,12,12,0},
        ['[']={14,8,8,8,8,8,14}, [']']={14,2,2,2,2,2,14},
        ['+']={0,4,4,31,4,4,0}, ['!']={4,4,4,4,4,0,4},
        ['?']={14,17,1,2,4,0,4}, ['=']={0,0,31,0,31,0,0}
    };
    return row < 7u ? font[(uint8_t)c & 127u][row] : 0u;
}

static inline uint8_t fx_byte(int32_t n) {
    return (uint8_t)(n < 0 ? 0 : n > 255 ? 255 : n);
}
static inline uint32_t fx_yyuv(uint32_t rgb) {
    int32_t r=(int32_t)((rgb>>16)&255u), g=(int32_t)((rgb>>8)&255u), b=(int32_t)(rgb&255u);
    uint32_t y=(uint32_t)fx_byte((77*r+150*g+29*b)/256);
    uint32_t u=(uint32_t)fx_byte((-43*r-85*g+128*b)/256+128);
    uint32_t v=(uint32_t)fx_byte((128*r-107*g-21*b)/256+128);
    return y | y<<8 | u<<16 | v<<24;
}
static inline int fx_canvas_valid(const FxCanvas *c) {
    return c && c->yyuv && c->alpha && c->width && c->height &&
        c->width <= 1280u && c->height <= 800u && !(c->width&1u) &&
        c->color_stride >= c->width*2u && c->alpha_stride >= c->width;
}
static inline void fx_rect(FxCanvas *c, uint32_t x, uint32_t y,
                           uint32_t w, uint32_t h, uint32_t rgb) {
    uint32_t row, col, packed=fx_yyuv(rgb);
    if (x>=c->width || y>=c->height) return;
    if (w>c->width-x) w=c->width-x;
    if (h>c->height-y) h=c->height-y;
    for (row=y; row<y+h; ++row) {
        uint8_t *p=c->yyuv+(uint64_t)row*c->color_stride;
        for (col=x; col<x+w; ++col) {
            uint32_t pair=(col&~1u)*2u;
            p[pair+(col&1u)]=(uint8_t)packed;
            p[pair+2u]=(uint8_t)(packed>>16);
            p[pair+3u]=(uint8_t)(packed>>24);
        }
    }
}
static inline void fx_clear(FxCanvas *c, uint32_t rgb) {
    uint32_t y,x,packed=fx_yyuv(rgb);
    for (y=0; y<c->height; ++y) {
        uint32_t *p=(uint32_t *)(void *)(c->yyuv+(uint64_t)y*c->color_stride);
        for (x=0; x<c->width/2u; ++x) p[x]=packed;
    }
}
static inline void fx_char(FxCanvas *c, uint32_t x, uint32_t y,
                           char ch, uint32_t scale, uint32_t rgb) {
    uint32_t row,col;
    for (row=0; row<7u; ++row)
        for (col=0; col<5u; ++col)
            if (fx_glyph(ch,row)&(1u<<(4u-col)))
                fx_rect(c,x+col*scale,y+row*scale,scale,scale,rgb);
}
static inline void fx_text(FxCanvas *c, uint32_t x, uint32_t y,
                           const char *s, uint32_t scale, uint32_t rgb) {
    while (*s && x<c->width) { fx_char(c,x,y,*s++,scale,rgb); x+=6u*scale; }
}
#endif
