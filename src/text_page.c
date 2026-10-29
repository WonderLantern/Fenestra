#include "fenestra_text_page.h"
static FxResult character(const char *text,uint32_t bytes,uint32_t *offset,uint32_t *value) {
    uint32_t at=*offset;if(at>=bytes)return FX_EMPTY;
    uint32_t c=(uint8_t)text[at++],extra=0,minimum=0;
    if(!c)return FX_INTEGRITY;
    if(c>=128u) {
        if(c>=0xc2u && c<=0xdfu){extra=1;minimum=0x80;c&=31u;}
        else if(c>=0xe0u && c<=0xefu){extra=2;minimum=0x800;c&=15u;}
        else if(c>=0xf0u && c<=0xf4u){extra=3;minimum=0x10000;c&=7u;}
        else return FX_INTEGRITY;
        if(extra>bytes-at)return FX_INTEGRITY;
        while(extra--){uint32_t b=(uint8_t)text[at++];if((b&0xc0u)!=0x80u)return FX_INTEGRITY;c=(c<<6)|(b&63u);}
        if(c<minimum || c>0x10ffffu || (c>=0xd800u && c<=0xdfffu))return FX_INTEGRITY;
    }
    if(c=='\r'){c='\n';if(at<bytes && text[at]=='\n')++at;}
    *offset=at;*value=c;return FX_OK;
}
static int append(FxTextPage *p,uint32_t value) {
    uint32_t n=value>=0x10000u?2u:1u;
    if(p->units+n>=FX_TEXT_PAGE_UNITS)return 0;
    if(n==2u){value-=0x10000u;p->text[p->units++]=(uint16_t)(0xd800u+(value>>10));value=0xdc00u+(value&1023u);}
    p->text[p->units++]=(uint16_t)value;p->text[p->units]=0;return 1;
}
static void truncate(FxTextPage *p,uint32_t units){p->units=units;p->text[units]=0;}
static FxResult fits(FxTextPage *p,int32_t width,int32_t height,FxPageMeasure measure,void *context,int *wide,int *high) {
    int32_t dimensions[2]={0,0};FxResult r=measure(context,p->text,p->units,dimensions);
    if(r!=FX_OK)return r;
    if(dimensions[0]<0 || dimensions[1]<0)return FX_INTEGRITY;
    *wide=dimensions[0]>width;*high=dimensions[1]>height;return FX_OK;
}
FxResult fx_text_page(FxTextPage *p,const char *text,uint32_t bytes,uint32_t offset,
                      int32_t width,int32_t height,FxPageMeasure measure,void *context) {
    if(!p || !text || !measure || offset>bytes || width<=0 || height<=0)return FX_ARGUMENT;
    p->units=0;p->text[0]=0;p->next=offset;
    while(p->next<bytes) {
        uint32_t start=p->next,at=start,old=p->units,c=0;int wide=0,high=0;
        FxResult r=character(text,bytes,&at,&c);if(r!=FX_OK)return r;
        /* Try a complete word first. Long words use the code-point path;
         * short words move together rather than breaking at every edge. */
        if(c!=' ' && c!='\n' && c!='\t') {
            uint32_t end=at;int complete=append(p,c);
            while(complete && end<bytes) {
                uint32_t next=end,v=0;r=character(text,bytes,&next,&v);if(r!=FX_OK)return r;
                if(v==' ' || v=='\n' || v=='\t')break;
                complete=append(p,v);if(complete)end=next;
            }
            if(complete) {
                r=fits(p,width,height,measure,context,&wide,&high);if(r!=FX_OK)return r;
                if(!wide && !high){p->next=end;continue;}
                if(wide && old && p->text[old-1u]!='\n' && p->units+1u<FX_TEXT_PAGE_UNITS) {
                    for(uint32_t j=p->units;j>old;--j)p->text[j]=p->text[j-1u];
                    p->text[old]='\n';p->text[++p->units]=0;
                    r=fits(p,width,height,measure,context,&wide,&high);if(r!=FX_OK)return r;
                    if(!wide && !high){p->next=end;continue;}
                }
                /* A whole word fits horizontally but belongs on the next
                 * page. Keep the input offset at the word's first byte. */
                if(!wide && high && old){truncate(p,old);break;}
            }
            truncate(p,old);
        }
        if(!append(p,c))break;
        r=fits(p,width,height,measure,context,&wide,&high);if(r!=FX_OK)return r;
        if(wide && c!='\n' && old && p->text[old-1u]!='\n') {
            truncate(p,old);
            if(!append(p,'\n') || !append(p,c)){truncate(p,old);break;}
            r=fits(p,width,height,measure,context,&wide,&high);if(r!=FX_OK)return r;
        }
        if(wide || high) {
            truncate(p,old);if(!old)return FX_BOUNDS;
            break;
        }
        p->next=at;
    }
    return p->next>offset || offset==bytes?FX_OK:FX_BOUNDS;
}
