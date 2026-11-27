#define _CRT_SECURE_NO_WARNINGS
#include "fenestra_overlay_ranges.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *file(const char *path,uint32_t *size) {
    FILE *input=fopen(path,"rb");
    assert(input && !fseek(input,0,SEEK_END));
    long length=ftell(input);
    assert(length>0 && length<10000000 && !fseek(input,0,SEEK_SET));
    *size=(uint32_t)length;
    uint8_t *data=malloc(*size);
    assert(data && fread(data,1u,*size,input)==*size && !fclose(input));
    return data;
}
int main(int argc,char **argv) {
    assert(argc==5);
    uint32_t recipe_bytes,original_bytes;
    uint8_t *recipe=file(argv[1],&recipe_bytes),*original=file(argv[2],&original_bytes);
    FxOverlayRangeOriginal source={.data=original+4096u,.bytes=70000u,.offset=4096u};
    assert(original_bytes>source.bytes+4096u);
    memset(source.application_sha256,0x11u,32u);
    FxOverlayRangeView view;
    assert(fx_overlay_ranges_init(&view,recipe,recipe_bytes,&source)==FX_OK);
    uint32_t actual=0u;
    uint8_t buffer[777];
    assert(fx_overlay_ranges_read(&view,0u,buffer,sizeof(buffer),&actual)==FX_STATE);
    assert(fx_overlay_ranges_verify(&view)==FX_BUSY);
    assert(fx_overlay_ranges_verify(&view)==FX_OK && fx_overlay_ranges_verify(&view)==FX_OK);
    FILE *output=fopen(argv[3],"wb");
    assert(output);
    for(uint32_t at=0u;at<source.bytes;at+=actual) {
        assert(fx_overlay_ranges_read(&view,at,buffer,sizeof(buffer),&actual)==FX_OK && actual);
        assert(fwrite(buffer,1u,actual,output)==actual);
    }
    assert(!fclose(output));
    assert(fx_overlay_ranges_read(&view,source.bytes,buffer,sizeof(buffer),&actual)==FX_OK && !actual);
    assert(fx_overlay_ranges_read(&view,source.bytes+1u,buffer,1u,&actual)==FX_BOUNDS);
    assert(fx_overlay_ranges_read(&view,0u,original+4096u,1u,&actual)==FX_ARGUMENT);
    original[5000]^=1u;
    assert(fx_overlay_ranges_init(&view,recipe,recipe_bytes,&source)==FX_OK);
    FxResult result;
    do {result=fx_overlay_ranges_verify(&view);} while(result==FX_BUSY);
    assert(result==FX_INTEGRITY && fx_overlay_ranges_read(&view,0u,buffer,1u,&actual)==FX_STATE);
    original[5000]^=1u;

    memset(source.application_sha256,0x22u,32u);
    source.data=original+2048u;
    source.bytes=60000u;
    source.offset=2048u;
    assert(fx_overlay_ranges_init(&view,recipe,recipe_bytes,&source)==FX_OK);
    do {result=fx_overlay_ranges_verify(&view);} while(result==FX_BUSY);
    assert(result==FX_OK);
    output=fopen(argv[4],"wb");
    assert(output);
    for(uint32_t at=0u;at<source.bytes;at+=actual) {
        assert(fx_overlay_ranges_read(&view,at,buffer,sizeof(buffer),&actual)==FX_OK && actual);
        assert(fwrite(buffer,1u,actual,output)==actual);
    }
    assert(!fclose(output));
    memset(source.application_sha256,0x33u,32u);
    assert(fx_overlay_ranges_init(&view,recipe,recipe_bytes,&source)==FX_EMPTY);
    recipe[0]^=1u;
    assert(fx_overlay_ranges_init(&view,recipe,recipe_bytes,&source)==FX_INTEGRITY);
    free(recipe);
    free(original);
    return 0;
}
