/* Status diagnostics for a pending runtime resume. */
#ifndef FX_Z8_POWER_DIAGNOSTIC_H
#define FX_Z8_POWER_DIAGNOSTIC_H
#include "fenestra.h"
/* A stage records the last loader boundary, not a progress/liveness promise.
 * Before a potentially blocking call E05 means no result has returned yet.
 * A single atomic word keeps the stage and complete FxResult inseparable. */
enum {
    FX_Z8_RESUME_QUEUED,FX_Z8_RESUME_STOP_DONE,FX_Z8_RESUME_STOP_ACK,
    FX_Z8_RESUME_GATE,FX_Z8_RESUME_TICKET,FX_Z8_RESUME_GUI_MUTEX,
    FX_Z8_RESUME_MANAGER,
    FX_Z8_RESUME_PUBLISH=9,FX_Z8_RESUME_DONE,FX_Z8_RESUME_BLOCKED
};
static inline uint64_t fx_z8_resume_diagnostic_word(uint32_t stage,uint32_t error) {
    return (uint64_t)stage<<32|error;
}
#define FX_Z8_RESUME_ROW(r) { \
 u"Resuming R" r " E00",u"Resuming R" r " E01",u"Resuming R" r " E02", \
 u"Resuming R" r " E03",u"Resuming R" r " E04",u"Resuming R" r " E05", \
 u"Resuming R" r " E06",u"Resuming R" r " E07",u"Resuming R" r " E08", \
 u"Resuming R" r " E09",u"Resuming R" r " E0A",u"Resuming R" r " E0B", \
 u"Resuming R" r " E0C",u"Resuming R" r " E0D" }
static inline const uint16_t *fx_z8_resume_diagnostic_label(uint64_t record,uint32_t *bytes) {
    static const uint16_t labels[12][14][16]={
        FX_Z8_RESUME_ROW("0"),FX_Z8_RESUME_ROW("1"),FX_Z8_RESUME_ROW("2"),
        FX_Z8_RESUME_ROW("3"),FX_Z8_RESUME_ROW("4"),FX_Z8_RESUME_ROW("5"),
        FX_Z8_RESUME_ROW("6"),FX_Z8_RESUME_ROW("7"),FX_Z8_RESUME_ROW("8"),
        FX_Z8_RESUME_ROW("9"),FX_Z8_RESUME_ROW("A"),FX_Z8_RESUME_ROW("B")};
    static const uint16_t unknown[]=u"Resuming R? E??";
    uint32_t stage=(uint32_t)(record>>32),error=(uint32_t)record;
    if(bytes)*bytes=30u;
    return stage<=FX_Z8_RESUME_BLOCKED && error<=13u?labels[stage][error]:unknown;
}
#undef FX_Z8_RESUME_ROW
#endif
