/* Host fixture for dynamically sized native locale banks; no camera code. */
#include "fenestra_native_font.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {void *blocks[16];uint32_t count;} Memory;
static void *allocate(void *context,uint32_t bytes){
    Memory *m=context;assert(m->count<16u);void *block=malloc(bytes);assert(block);
    m->blocks[m->count++]=block;return block;
}
static void release(void *context,void *block){(void)context;free(block);}
static void discard(Memory *m){for(uint32_t i=0;i<m->count;++i)free(m->blocks[i]);}
static void scenario(uint32_t languages){
    uint8_t fonts[17],rows[65][17];uintptr_t roots[65];
    for(uint32_t col=0;col<17u;++col)fonts[col]=(uint8_t)(col+3u);
    for(uint32_t lang=0;lang<65u;++lang){
        roots[lang]=(uintptr_t)rows[lang];
        for(uint32_t col=0;col<17u;++col)rows[lang][col]=(uint8_t)(lang*3u+col);
    }
    Memory m={0};FxIndexMemory memory={&m,allocate,release};FxNativeTextBank *first=0,*grown=0;
    assert(fx_native_text_bank_create(memory,fonts,roots,17u,languages,18u,7u,9u,&first)==FX_OK);
    assert(first && first->languages==languages && first->capacity>=18u && first->previous==0);
    assert(!memcmp(first->fonts,fonts,17u));
    for(uint32_t lang=0;lang<languages;++lang){
        const uint8_t *row=(const uint8_t *)first->attributes[lang];
        assert(!memcmp(row,rows[lang],17u));assert(row[17]==9u);
    }
    first->fonts[18]=(uint8_t)123u;
    for(uint32_t lang=0;lang<languages;++lang)((uint8_t *)first->attributes[lang])[18]=(uint8_t)(lang+40u);
    uint32_t first_capacity=first->capacity,first_bytes=first->bytes;
    assert(fx_native_text_bank_grow(memory,first,first_capacity+100u,&grown)==FX_OK);
    assert(grown!=first && grown->previous==first && grown->capacity>=first_capacity+100u);
    assert(grown->retained_bytes==first_bytes+grown->bytes && first->capacity==first_capacity);
    assert(!memcmp(grown->fonts,first->fonts,first_capacity) && grown->fonts[18]==123u);
    for(uint32_t col=first_capacity;col<grown->capacity;++col)assert(grown->fonts[col]==7u);
    for(uint32_t lang=0;lang<languages;++lang){
        const uint8_t *old=(const uint8_t *)first->attributes[lang],*row=(const uint8_t *)grown->attributes[lang];
        assert(!memcmp(row,old,first_capacity) && row[18]==(uint8_t)(lang+40u));
        for(uint32_t col=first_capacity;col<grown->capacity;++col)assert(row[col]==9u);
    }
    FxNativeTextBank *same=0;uint32_t allocations=m.count;
    assert(fx_native_text_bank_grow(memory,grown,grown->capacity,&same)==FX_OK && same==grown && m.count==allocations);
    discard(&m);
}
int main(void){
    scenario(29u);scenario(36u);
    uint8_t fonts[17]={0},row[17]={0};uintptr_t roots[65];
    for(uint32_t i=0;i<65u;++i)roots[i]=(uintptr_t)row;
    Memory m={0};FxIndexMemory memory={&m,allocate,release};FxNativeTextBank *bank=0;
    assert(fx_native_text_bank_create(memory,fonts,roots,17u,65u,18u,0u,0u,&bank)==FX_ARGUMENT);
    assert(bank==0 && m.count==0u);
    assert(fx_native_text_bank_create(memory,fonts,roots,17u,64u,65536u,1u,2u,&bank)==FX_OK);
    assert(bank->capacity==65536u && bank->languages==64u && bank->bytes<FX_NATIVE_TEXT_BUDGET);
    assert(((uint8_t *)bank->attributes[63])[65535]==2u && bank->fonts[65535]==1u);
    discard(&m);
    puts("PASS: 29/36 locales create/grow preserve every row; 64 maximum bank bounded; 65 rejected before allocation");
    return 0;
}
