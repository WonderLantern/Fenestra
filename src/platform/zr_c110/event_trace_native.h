#ifndef FX_C110_EVENT_TRACE_NATIVE_H
#define FX_C110_EVENT_TRACE_NATIVE_H
#include "event_trace.h"
#include "sensor_trace.h"
FxEventTrace *fx_c110_menu_event_trace(void);
FxEventTrace *fx_c110_menu_event_trace_create(void);
void fx_c110_event_trace_observe(FxSensorTraceFrame *,uint32_t site);
void fx_c110_event_trace_sensor(FxSensorTraceFrame *,uint32_t event,uint64_t capture,uint64_t token);
void fx_c110_event_trace_cancel(uint32_t);
void fx_c110_event_trace_step(const FxObservationIO *,int);
int fx_c110_event_trace_quiesced(void);
uint32_t fx_c110_event_trace_request(const uint8_t *,uint32_t,uint8_t *,uint32_t *);
#endif
