#ifndef FENESTRA_NATIVE_PTP_H
#define FENESTRA_NATIVE_PTP_H
#include "fenestra_ptp.h"
#include <stddef.h>

/* Nikon's 64-byte dispatcher ABI, verified separately for each profile.
 * Reserved words remain zero; the original dispatcher owns transfer state. */
typedef struct FxNativePtpDescriptor FxNativePtpDescriptor;
struct FxNativePtpDescriptor {
    uint32_t opcode,direction;
    uint64_t flags,reserved16,reserved24;
    uint32_t (*buffer)(const FxNativePtpDescriptor *,const uint8_t *,uint8_t **,uint32_t *);
    uint32_t (*preflight)(const FxNativePtpDescriptor *,const uint8_t *);
    uint32_t (*execute)(const FxNativePtpDescriptor *,const uint8_t *);
    void (*finish)(void);
};
_Static_assert(sizeof(FxNativePtpDescriptor)==64,"native PTP descriptor size");
_Static_assert(offsetof(FxNativePtpDescriptor,execute)==48,"native PTP callback offset");
typedef struct {
    FxPtp *(*state)(void);
    int (*active)(void);
    uint8_t *(*payload)(void);
    uint32_t (*capacity)(void);
    const uint8_t *(*incoming)(void);
    void (*response)(const uint8_t *,uint32_t,uint32_t,const void *,uint32_t);
    uint32_t (*send)(const uint8_t *,const uint8_t *,uint32_t,uint32_t);
    const FxNativePtpDescriptor *(*original_lookup)(uint32_t);
    void (*original_session)(uint32_t);
    uint32_t (*original_operations)(uint8_t *,uint32_t,const uint8_t *);
    void (*disconnect)(void);
    const volatile uint32_t *session;
    const uint8_t *operations;
    uint32_t operations_capacity;
} FxNativePtpProfile;

/* One immutable target profile per firmware image, no global writable state. */
extern const FxNativePtpProfile fx_native_ptp_profile;
const FxNativePtpDescriptor *fx_native_ptp_lookup(uint32_t);
void fx_native_ptp_session(uint32_t);
uint32_t fx_native_ptp_operations(uint8_t *,uint32_t,const uint8_t *);
#endif
