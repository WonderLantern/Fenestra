#ifndef FX_C110_FRAME_SAMPLE_NATIVE_H
#define FX_C110_FRAME_SAMPLE_NATIVE_H
#include "frame_sample.h"
#include "sensor_trace.h"
void fx_c110_frame_observe(FxSensorTraceFrame *,uint64_t);
void fx_c110_frame_step(const FxObservationIO *,int);
void fx_c110_frame_cancel(uint32_t);
int fx_c110_frame_quiesced(void);
uint32_t fx_c110_frame_request(const uint8_t *,uint32_t,uint8_t *,uint32_t *);
void fx_c110_menu_observer_fault(FxResult);
#endif
