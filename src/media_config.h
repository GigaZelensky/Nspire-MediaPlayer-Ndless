#ifndef NMEDIA_CONFIG_H
#define NMEDIA_CONFIG_H
#include <stdbool.h>
/* The public meter is independent of default-off diagnostic recording and
 * of the private experimental profile used during hardware development. */
#ifndef NDMEDIA_BATTERY_METER
#if (defined(NDMEDIA_BATTERY_GAUGE_TEST) && NDMEDIA_BATTERY_GAUGE_TEST) || \
    (defined(NDMEDIA_BATTERY_CALIBRATION) && NDMEDIA_BATTERY_CALIBRATION)
#define NDMEDIA_BATTERY_METER 0
#else
#define NDMEDIA_BATTERY_METER 1
#endif
#endif
#ifndef NDMEDIA_BATTERY_SERVICE
#define NDMEDIA_BATTERY_SERVICE NDMEDIA_BATTERY_METER
#endif
#ifndef NDVIDEO_WITH_H264
#define NDVIDEO_WITH_H264 1
#endif
#ifndef NDVIDEO_WITH_MPEG4
#define NDVIDEO_WITH_MPEG4 1
#endif
#ifndef NDVIDEO_WITH_HEVC
#define NDVIDEO_WITH_HEVC 1
#endif
#ifndef NDVIDEO_WITH_AV1
#define NDVIDEO_WITH_AV1 1
#endif

/* Public build switches. Defaults also keep standalone decoder tests useful. */
#ifndef NDMEDIA_AUDIO
#define NDMEDIA_AUDIO 1
#endif
#ifndef NDMEDIA_USB_STORAGE
#define NDMEDIA_USB_STORAGE 1
#endif
#ifndef NDMEDIA_ARTWORK
#define NDMEDIA_ARTWORK NDMEDIA_AUDIO
#endif
#ifndef NDMEDIA_VISUALIZATIONS
#define NDMEDIA_VISUALIZATIONS NDMEDIA_AUDIO
#endif
#ifndef NDMEDIA_WITH_PCM
#define NDMEDIA_WITH_PCM NDMEDIA_AUDIO
#endif
#ifndef NDMEDIA_WITH_IMA
#define NDMEDIA_WITH_IMA NDMEDIA_AUDIO
#endif
#ifndef NDMEDIA_WITH_MP3
#define NDMEDIA_WITH_MP3 NDMEDIA_AUDIO
#endif
#ifndef NDMEDIA_WITH_FLAC
#define NDMEDIA_WITH_FLAC NDMEDIA_AUDIO
#endif
#ifndef NDMEDIA_WITH_VORBIS
#define NDMEDIA_WITH_VORBIS NDMEDIA_AUDIO
#endif
#ifndef NDMEDIA_WITH_AAC
#define NDMEDIA_WITH_AAC NDMEDIA_AUDIO
#endif
#ifndef NDMEDIA_WITH_OPUS
#define NDMEDIA_WITH_OPUS NDMEDIA_AUDIO
#endif
#define NDMEDIA_WITH_WAV (NDMEDIA_WITH_PCM || NDMEDIA_WITH_IMA)
#define NDMEDIA_WITH_EXTRA (NDMEDIA_WITH_VORBIS || NDMEDIA_WITH_AAC || NDMEDIA_WITH_OPUS)

static inline bool media_video_codec_supported(unsigned codec)
{
    switch(codec) {
    case 0:return NDVIDEO_WITH_H264;
    case 1:return NDVIDEO_WITH_MPEG4;
    case 2:return NDVIDEO_WITH_HEVC;
    case 3:return NDVIDEO_WITH_AV1;
    default:return false;
    }
}

static inline bool media_audio_codec_supported(unsigned codec)
{
    switch(codec) {
    case 0:return NDMEDIA_AUDIO && NDMEDIA_WITH_PCM;
    case 1:return NDMEDIA_AUDIO && NDMEDIA_WITH_IMA;
    case 2:return NDMEDIA_AUDIO && NDMEDIA_WITH_MP3;
    case 3:return NDMEDIA_AUDIO && NDMEDIA_WITH_FLAC;
    case 4:return NDMEDIA_AUDIO && NDMEDIA_WITH_VORBIS;
    case 5:return NDMEDIA_AUDIO && NDMEDIA_WITH_AAC;
    case 6:return NDMEDIA_AUDIO && NDMEDIA_WITH_OPUS;
    default:return false;
    }
}
#endif
