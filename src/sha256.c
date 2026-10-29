#include "fenestra.h"
#include "fenestra_sha256.h"
#include "fenestra_memory.h"

static uint32_t rotate(uint32_t value, unsigned count) {
    return (value >> count) | (value << (32u - count));
}

static void block(uint32_t state[8], const uint8_t data[64]) {
    static const uint32_t constants[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    };
    uint32_t words[64], a,b,c,d,e,f,g,h;
    unsigned i;
    for (i = 0u; i < 16u; ++i) {
        const uint8_t *p = data + i * 4u;
        words[i] = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
                   (uint32_t)p[2] << 8 | p[3];
    }
    for (i = 16u; i < 64u; ++i) {
        uint32_t x = words[i-15u], y = words[i-2u];
        words[i] = words[i-16u] + (rotate(x,7u)^rotate(x,18u)^(x>>3)) +
                   words[i-7u] + (rotate(y,17u)^rotate(y,19u)^(y>>10));
    }
    a=state[0]; b=state[1]; c=state[2]; d=state[3];
    e=state[4]; f=state[5]; g=state[6]; h=state[7];
    for (i = 0u; i < 64u; ++i) {
        uint32_t t1 = h + (rotate(e,6u)^rotate(e,11u)^rotate(e,25u)) +
                      ((e&f)^((~e)&g)) + constants[i] + words[i];
        uint32_t t2 = (rotate(a,2u)^rotate(a,13u)^rotate(a,22u)) + ((a&b)^(a&c)^(b&c));
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

void fx_sha256_init(FxSha256 *s) {
    static const uint32_t initial[8]={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    memset(s,0,sizeof(*s));memcpy(s->state,initial,sizeof(initial));
}
FxResult fx_sha256_update(FxSha256 *s,const void *input,size_t bytes) {
    if(!s || (!input && bytes) || s->used>=64u ||
       s->bytes>UINT64_MAX/8u || bytes>UINT64_MAX/8u-s->bytes)return FX_ARGUMENT;
    const uint8_t *data=input;s->bytes+=(uint64_t)bytes;
    while(bytes) {
        if(!s->used && bytes>=64u) {block(s->state,data);data+=64u;bytes-=64u;continue;}
        size_t n=64u-s->used;if(n>bytes)n=bytes;
        memcpy(s->tail+s->used,data,n);s->used+=(uint32_t)n;data+=n;bytes-=n;
        if(s->used==64u) {block(s->state,s->tail);s->used=0u;}
    }
    return FX_OK;
}
void fx_sha256_final(const FxSha256 *s,uint8_t out[32]) {
    uint32_t state[8];uint8_t tail[128]={0};
    memcpy(state,s->state,sizeof(state));memcpy(tail,s->tail,s->used);
    tail[s->used]=0x80u;size_t padded=s->used<56u?64u:128u;
    uint64_t bits=s->bytes*8u;
    for(unsigned i=0u;i<8u;++i)tail[padded-1u-i]=(uint8_t)(bits>>(i*8u));
    block(state,tail);if(padded==128u)block(state,tail+64u);
    for(unsigned i=0u;i<8u;++i) {
        out[i*4u]=(uint8_t)(state[i]>>24);out[i*4u+1u]=(uint8_t)(state[i]>>16);
        out[i*4u+2u]=(uint8_t)(state[i]>>8);out[i*4u+3u]=(uint8_t)state[i];
    }
}
void fx_sha256(const void *input,size_t bytes,uint8_t out[32]) {
    FxSha256 s;fx_sha256_init(&s);(void)fx_sha256_update(&s,input,bytes);fx_sha256_final(&s,out);
}
