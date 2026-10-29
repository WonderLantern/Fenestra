#ifndef FX_C110_SENSOR_TRACE_NATIVE_H
#define FX_C110_SENSOR_TRACE_NATIVE_H
#include "sensor_trace.h"
/* Native menu owns one lazy allocation per boot. Creation is permitted only
 * by an admitted explicit ARM; it is never called by a trace callback. */
FxSensorTrace *fx_c110_menu_sensor_trace(void);
FxSensorTrace *fx_c110_menu_sensor_trace_create(void);
uint64_t fx_c110_sensor_trace_enter(FxSensorTraceFrame *);
void fx_c110_sensor_trace_leave(FxSensorTraceFrame *);
void fx_c110_sensor_trace_cancel(uint32_t reason);
void fx_c110_sensor_trace_step(const FxObservationIO *,int admitted);
int fx_c110_sensor_trace_quiesced(void);
uint32_t fx_c110_sensor_trace_request(const uint8_t *,uint32_t,uint8_t *,uint32_t *);
#endif
