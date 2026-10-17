#ifndef NDVIDEO_USB_AUDIO_FORMAT_H
#define NDVIDEO_USB_AUDIO_FORMAT_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef struct {
    uint8_t configuration, interface_number, alternate, endpoint, channels;
    uint8_t frequency_control;
    uint16_t max_packet;
    uint8_t protocol, control_interface, sample_bytes, sample_bits;
    uint8_t sync_type, data_interval, feedback_endpoint, feedback_interval;
    uint16_t feedback_max_packet, feedback_period_uframes, lock_delay;
    uint8_t lock_delay_units, terminal_link, clock_root, feature_unit;
    uint8_t mute_mask, volume_mask;
    uint32_t rate_mask, sample_rate;
} UsbAudioFormat;
#define USB_AUDIO_MAX_FORMATS 24U
#define USB_AUDIO_RATE_COUNT 4U
extern const uint32_t usb_audio_rates[USB_AUDIO_RATE_COUNT];
/* UAC1/UAC2, mono/stereo PCM16/24/32, sync/adaptive/explicit feedback.
 * Returns ranked alternatives; UAC2 rates are negotiated with its clock. */
unsigned usb_audio_formats(const uint8_t *config, size_t length, bool high_speed,
                           UsbAudioFormat *out, unsigned capacity);
bool usb_audio_find_format(const uint8_t *config, size_t length, UsbAudioFormat *out);
const uint8_t *usb_audio_control_unit(const uint8_t *config, size_t length,
                                      uint8_t control_interface, uint8_t id);
bool usb_audio_feedback_rate(const uint8_t *bytes, unsigned count, bool high_speed,
                             uint32_t nominal_rate, uint32_t *rate_q16);
#endif
