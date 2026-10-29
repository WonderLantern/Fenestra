#ifndef FENESTRA_VIDEO_CONFIGURATION_H
#define FENESTRA_VIDEO_CONFIGURATION_H
#include "fenestra_capture.h"
#include "fenestra_records.h"
/* camera.video.configuration/v3. Capacity is an adapter limit, not a list of
 * approved modules. Modules own every replacement value, label and template.
 * BEGIN(total bytes), APPEND(exact next offset), COMMIT are one owner upload.
 * No original state is changed before a complete validated COMMIT. */
#define FX_VIDEO_CONFIGURATION_ABI 4u
#define FX_VIDEO_BINDING_VERSION 3u
#define FX_VIDEO_LEGACY_ABI 3u
/* Budget of the current adapter, not an asserted sensor/hardware limit. */
#define FX_VIDEO_MAX_MODES 16u
#define FX_VIDEO_MODES 2u /* Legacy/pair convenience container only. */
#define FX_VIDEO_RECORDS 12u
enum { FX_VIDEO_QUERY,FX_VIDEO_BEGIN,FX_VIDEO_APPEND,FX_VIDEO_COMMIT,FX_VIDEO_STATUS };
typedef struct {
    FxCaptureProfile profile;
    uint32_t native_codec,native_family,native_rate,file_rate;
    uint32_t row_template,stock_row,quick_template,native_layout; /* low16=image enum; high16=picture-copy area enum */
    char label[48];
    FxRecordProgram records[FX_VIDEO_RECORDS];
} FxVideoConfigurationMode;
typedef struct {
    uint32_t abi,bytes,count,reserved;
    FxVideoConfigurationMode modes[FX_VIDEO_MODES];
} FxVideoConfiguration;
typedef struct {
    uint32_t abi,bytes,count,reserved;
    FxVideoConfigurationMode modes[];
} FxVideoConfigurationData;
typedef struct {
    uint32_t abi,bytes,supported_abis,header_bytes,mode_bytes,max_modes,codecs,reserved;
} FxVideoConfigurationInfo;
static inline uint32_t fx_video_configuration_bytes(uint32_t count) {
    return count && count<=FX_VIDEO_MAX_MODES?
        (uint32_t)sizeof(FxVideoConfigurationData)+count*(uint32_t)sizeof(FxVideoConfigurationMode):0u;
}
FxResult fx_video_configuration_check(const void *,uint32_t bytes);
#endif
