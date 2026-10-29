#ifndef FENESTRA_CAPTURE_SERVICE_H
#define FENESTRA_CAPTURE_SERVICE_H
#include "fenestra_capture.h"
/* Legacy capture opcodes are retired. New clients open the versioned named
 * camera.video.configuration provider; see fenestra_video_configuration.h. */
enum { FX_CAPTURE_SERVICE_ABI=2, FX_CAPTURE_MAX_PROFILES=4 };
enum { FX_CAPTURE_QUERY=0x100u, FX_CAPTURE_REGISTER, FX_CAPTURE_SELECT,
       FX_CAPTURE_STATUS };
enum { FX_EVENT_CAPTURE_SELECT=16u };
/* An optional QUERY capability: selection is owned by the original resolution
 * menu task. Module SELECT returns BUSY without mutation. STATUS may advance
 * after a native menu selection; clients must poll it even without ACTION.
 * In this mode STATUS value[2] reports native owner busy (including preview),
 * NOT a recording indicator. STATUS success does not imply resumed capture. */
enum { FX_CAPTURE_NATIVE_MENU=1u<<11 };
#endif
