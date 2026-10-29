#ifndef FENESTRA_NATIVE_TRANSLATION_IMPL_H
#define FENESTRA_NATIVE_TRANSLATION_IMPL_H
#include "fenestra_translation.h"
/* Shared by the native module binding and RTOS debugger. No policy gate,
 * caller-buffer writes, table edits or task switch occurs in this interval. */
static inline uint64_t fx_translation_probe(uint64_t address,uint64_t access) {
    if(access>FX_TRANSLATE_EL0_WRITE)return UINT64_MAX;
    uint64_t saved_daif,saved_par,result;
    __asm__ volatile("mrs %0,daif; msr daifset,#15; mrs %1,par_el1"
        :"=&r"(saved_daif),"=&r"(saved_par)::"memory");
    switch(access) {
    case FX_TRANSLATE_EL1_READ:__asm__ volatile("at s1e1r,%0"::"r"(address):"memory");break;
    case FX_TRANSLATE_EL1_WRITE:__asm__ volatile("at s1e1w,%0"::"r"(address):"memory");break;
    case FX_TRANSLATE_EL0_READ:__asm__ volatile("at s1e0r,%0"::"r"(address):"memory");break;
    default:__asm__ volatile("at s1e0w,%0"::"r"(address):"memory");break;
    }
    __asm__ volatile("isb; mrs %0,par_el1; msr par_el1,%1; msr daif,%2"
        :"=&r"(result):"r"(saved_par),"r"(saved_daif):"memory");
    return result;
}
#endif
