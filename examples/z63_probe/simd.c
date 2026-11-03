/* FP/NEON control: all 32 Q registers and FPCR/FPSR survive an integer
 * interval long enough to admit timer preemption, before the copied SDK test. */
#define fenestra_module_entry copied_sdk_entry
#include "sdk.c"
#undef fenestra_module_entry
static uint32_t checked;
static uint8_t observed[512] __attribute__((aligned(16)));
static int vectors(void) {
    uint64_t fpcr,fpsr;
    __asm__ volatile(
        "movi v0.16b,#1\n"
        "movi v1.16b,#2\n"
        "movi v2.16b,#3\n"
        "movi v3.16b,#4\n"
        "movi v4.16b,#5\n"
        "movi v5.16b,#6\n"
        "movi v6.16b,#7\n"
        "movi v7.16b,#8\n"
        "movi v8.16b,#9\n"
        "movi v9.16b,#10\n"
        "movi v10.16b,#11\n"
        "movi v11.16b,#12\n"
        "movi v12.16b,#13\n"
        "movi v13.16b,#14\n"
        "movi v14.16b,#15\n"
        "movi v15.16b,#16\n"
        "movi v16.16b,#17\n"
        "movi v17.16b,#18\n"
        "movi v18.16b,#19\n"
        "movi v19.16b,#20\n"
        "movi v20.16b,#21\n"
        "movi v21.16b,#22\n"
        "movi v22.16b,#23\n"
        "movi v23.16b,#24\n"
        "movi v24.16b,#25\n"
        "movi v25.16b,#26\n"
        "movi v26.16b,#27\n"
        "movi v27.16b,#28\n"
        "movi v28.16b,#29\n"
        "movi v29.16b,#30\n"
        "movi v30.16b,#31\n"
        "movi v31.16b,#32\n"
        "mov x9,#0x400000\nmsr fpcr,x9\n"
        "mov x9,#0x8000000\nmsr fpsr,x9\n"
        "mov x9,#0x4b40\nmovk x9,#0x4c,lsl #16\n"
        "1: subs x9,x9,#1\nb.ne 1b\n"
        "stp q0,q1,[%2,#0]\n"
        "stp q2,q3,[%2,#32]\n"
        "stp q4,q5,[%2,#64]\n"
        "stp q6,q7,[%2,#96]\n"
        "stp q8,q9,[%2,#128]\n"
        "stp q10,q11,[%2,#160]\n"
        "stp q12,q13,[%2,#192]\n"
        "stp q14,q15,[%2,#224]\n"
        "stp q16,q17,[%2,#256]\n"
        "stp q18,q19,[%2,#288]\n"
        "stp q20,q21,[%2,#320]\n"
        "stp q22,q23,[%2,#352]\n"
        "stp q24,q25,[%2,#384]\n"
        "stp q26,q27,[%2,#416]\n"
        "stp q28,q29,[%2,#448]\n"
        "stp q30,q31,[%2,#480]\n"
        "mrs %0,fpcr\nmrs %1,fpsr\nmsr fpcr,xzr\nmsr fpsr,xzr\n"
        : "=&r"(fpcr),"=&r"(fpsr) : "r"(observed)
        : "x9","cc","memory","v0","v1","v2","v3","v4","v5","v6","v7","v8","v9","v10","v11","v12","v13","v14","v15","v16","v17","v18","v19","v20","v21","v22","v23","v24","v25","v26","v27","v28","v29","v30","v31");
    if(fpcr!=0x400000u || fpsr!=0x8000000u)return 1;
    for(uint32_t i=0;i<512u;++i)if(observed[i]!=(uint8_t)(i/16u+1u))return 2;
    return 0;
}
int fenestra_module_entry(const FxModuleServices *base) {
    if(!checked){int r=vectors();if(r)return 200+r;checked=1u;}
    return copied_sdk_entry(base);
}
