#include "fenestra_native_call.h"
#include <stddef.h>
_Static_assert(sizeof(FxNativeCallFrame)==560,"copied AAPCS64 frame");
_Static_assert(offsetof(FxNativeCallFrame,integer)==16 && offsetof(FxNativeCallFrame,vector)==96 &&
               offsetof(FxNativeCallFrame,stack)==224 && offsetof(FxNativeCallFrame,result)==480 &&
               offsetof(FxNativeCallFrame,vector_result)==496,"AAPCS64 assembly layout");
FxResult fx_native_call_frame_check(const FxNativeCallFrame *frame) {
    if(!frame || frame->abi!=1u || frame->bytes!=sizeof(*frame) || frame->flags&~1u || frame->reserved)return FX_ARGUMENT;
    if(frame->stack_bytes>FX_NATIVE_STACK_BYTES || (frame->stack_bytes&15u))return FX_BOUNDS;
    return FX_OK;
}
