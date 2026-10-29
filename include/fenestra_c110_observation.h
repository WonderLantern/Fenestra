#ifndef FENESTRA_C110_OBSERVATION_H
#define FENESTRA_C110_OBSERVATION_H
#include "fenestra_observation.h"
typedef struct {
    FxObservation recorder;
    uint64_t boot;
    _Atomic uint64_t disconnect_epoch;
    _Atomic uint32_t created;
    _Atomic int32_t task;
    uint8_t stack[16384] __attribute__((aligned(16)));
} FxC110Observation;
FxC110Observation *fx_c110_menu_observation(void);
uint64_t fx_c110_observation_activation(void);
void fx_c110_observation_init(FxC110Observation *);
void fx_c110_observation_cancel(uint32_t);
void fx_c110_observation_disconnect(void);
int fx_c110_observation_quiesced(void);
uint32_t fx_c110_observation_request(const uint8_t *,uint32_t,uint8_t *,uint32_t *);
#endif
