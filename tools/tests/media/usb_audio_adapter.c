/* Unmodified configuration captured read-only through a Windows USB hub on
 * 2026-10-16: 12d1:3a06 USB audio adapter, with headphone, mic and HID functions.
 * Device/serial descriptors are deliberately excluded. No hardware is used. */
#include "audio/usb_audio_format.h"
#include <assert.h>
#include <stdio.h>

static const uint8_t config[] = {
    9,2,218,0,4,1,0,0x80,50,
    9,4,0,0,0,1,1,0,0,
    10,0x24,1,0,1,71,0,2,1,2,
    12,0x24,2,1,1,1,0,2,3,0,0,0,
    10,0x24,6,9,1,1,3,0,0,0,
    9,0x24,3,3,1,3,0,9,0,
    12,0x24,2,2,1,2,0,1,1,0,0,0,
    9,0x24,6,10,2,1,3,0,0,
    9,0x24,3,4,1,1,0,10,0,
    9,4,1,0,0,1,2,0,0,
    9,4,1,1,1,1,2,0,0,
    7,0x24,1,1,1,1,0,
    11,0x24,2,1,2,2,16,1,0x80,0xbb,0,
    9,5,2,9,192,0,1,0,0,
    7,0x25,1,0,0,0,0,
    9,4,2,0,0,1,2,0,0,
    9,4,2,1,1,1,2,0,0,
    7,0x24,1,4,1,1,0,
    11,0x24,2,1,1,2,16,1,0x80,0xbb,0,
    9,5,0x82,5,96,0,1,0,0,
    7,0x25,1,0,0,0,0,
    9,4,3,0,1,3,0,0,0,
    9,0x21,1,2,0,1,0x22,54,0,
    7,5,0x81,3,8,0,1
};

int main(void)
{
    UsbAudioFormat formats[USB_AUDIO_MAX_FORMATS];
    assert(sizeof(config) == 218);
    assert(usb_audio_formats(config, sizeof(config), false, formats, USB_AUDIO_MAX_FORMATS) == 1);
    const UsbAudioFormat *f = formats;
    assert(f->configuration == 1 && f->protocol == 0 && f->control_interface == 0);
    assert(f->interface_number == 1 && f->alternate == 1 && f->endpoint == 2);
    assert(f->channels == 2 && f->sample_bytes == 2 && f->sample_bits == 16);
    assert(f->sample_rate == 48000 && f->rate_mask == 1 && !f->frequency_control);
    assert(f->sync_type == 2 && f->data_interval == 1 && f->max_packet == 192);
    assert(!f->feedback_endpoint && f->feature_unit == 9);
    assert(f->mute_mask == 1 && f->volume_mask == 1); /* Playback master, never mic unit 10. */
    for (size_t cut = 0; cut < sizeof(config); ++cut)
        assert(!usb_audio_formats(config, cut, false, formats, USB_AUDIO_MAX_FORMATS));
    puts("PASS captured 12d1:3a06: UAC1 PCM16 stereo 48k, adaptive output and playback mixer routing");
    return 0;
}
