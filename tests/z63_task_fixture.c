#include <assert.h>
#include <stdint.h>
static int32_t create(const void *);
static int32_t resume(int32_t);
#define FX_Z63_NATIVE_CALL(a,t) ((t)((a)==0x4018afb8u?(uintptr_t)create:(uintptr_t)resume))
#include "../src/platform/z63_c201/task.c"
static int32_t create_result=7,resume_result;
static unsigned creates,resumes;
static _Alignas(16) unsigned char stack[65536];
static void __attribute__((aligned(4))) entry(void *p){(void)p;}
static int32_t create(const void *p){
 const Task *q=p;++creates;
 assert(!q->attributes && q->entry==(uintptr_t)entry && q->context==123u);
 assert(q->priority==100u && q->stack_bytes==sizeof(stack) && q->stack==(uintptr_t)stack);
 assert(q->processor==1u && q->affinity==UINT32_MAX);
 return create_result;
}
static int32_t resume(int32_t id){assert(id==7);++resumes;return resume_result;}
int main(void){
 FxZ63TaskScope s={0};
 assert(fx_z63_task_start(&s,entry,(void *)123u,stack+1,sizeof(stack)-16u)==FX_ARGUMENT);
 assert(!creates);
 create_result=-33;
 assert(fx_z63_task_start(&s,entry,(void *)123u,stack,sizeof(stack))==FX_IO && s.phase==FX_Z63_TASK_FAILED);
 assert(!resumes && fx_z63_task_start(&s,entry,(void *)123u,stack,sizeof(stack))==FX_BUSY);
 s=(FxZ63TaskScope){0};create_result=7;resume_result=-25;
 assert(fx_z63_task_start(&s,entry,(void *)123u,stack,sizeof(stack))==FX_IO);
 assert(s.id==7 && s.stack==(uintptr_t)stack && s.phase==FX_Z63_TASK_FAILED);
 assert(fx_z63_task_start(&s,entry,(void *)123u,stack,sizeof(stack))==FX_BUSY);
 s=(FxZ63TaskScope){0};resume_result=0;
 assert(fx_z63_task_start(&s,entry,(void *)123u,stack,sizeof(stack))==FX_OK && s.phase==FX_Z63_TASK_RUNNING);
 return 0;
}
