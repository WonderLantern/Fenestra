#include "fenestra_text_fit.h"
static uint32_t length(const uint16_t *s) {
    uint32_t n=0u;while(n<FX_UI_TEXT && s[n])++n;return n;
}
static FxResult fit(uint16_t *s,int32_t limit,uint32_t column,FxTextMeasure measure,void *context) {
    if(!s[0])return FX_OK;
    int32_t width=measure(context,column);
    if(width<0)return FX_STATE;
    if(width<=limit || !s[0])return FX_OK;
    uint32_t keep=length(s);
    /* Ellipsis uses the same native font. Remove whole UTF-16 code points,
     * never half a surrogate pair, and always make room for the terminator. */
    do {
        if(keep) {
            --keep;
            if(keep && s[keep]>=0xdc00u && s[keep]<=0xdfffu &&
               s[keep-1u]>=0xd800u && s[keep-1u]<=0xdbffu)--keep;
        }
        if(keep+3u>=FX_UI_TEXT)continue;
        s[keep]='.';s[keep+1u]='.';s[keep+2u]='.';s[keep+3u]=0u;
        width=measure(context,column);
        if(width<0)return FX_STATE;
        if(width<=limit)return FX_OK;
    } while(keep);
    /* Extremely narrow geometry cannot even contain an ellipsis. */
    s[0]=0u;return measure(context,column)<0?FX_STATE:FX_OK;
}
FxResult fx_text_fit_pair(uint16_t label[FX_UI_TEXT],uint16_t value[FX_UI_TEXT],
                          int32_t width,int32_t gap,FxTextMeasure measure,void *context) {
    if(!label || !value || !measure || width<=0 || gap<0 || gap>=width ||
       length(label)==FX_UI_TEXT || length(value)==FX_UI_TEXT)return FX_ARGUMENT;
    int32_t left=label[0]?measure(context,0u):0;
    int32_t right=value[0]?measure(context,1u):0;
    if(left<0 || right<0)return FX_STATE;
    int32_t space=width-((label[0] && value[0])?gap:0);
    if(left<=space && right<=space-left)return FX_OK;
    /* Short labels or values retain their full width. If both are long,
     * neither may consume more than the other's half of the available row. */
    int32_t right_limit=right<space/2?right:space/2;
    if(left<space-right_limit)right_limit=space-left;
    FxResult r=fit(label,space-right_limit,0u,measure,context);
    return r==FX_OK?fit(value,right_limit,1u,measure,context):r;
}
