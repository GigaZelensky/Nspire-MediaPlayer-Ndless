#include "player/music_controls.h"
#include "player/media_files.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    MusicControls c = {0};
    MusicControlInput in = {.rate = 48000, .frames = 480000, .duration_known = true};
    in.toggle = true;
    assert(music_controls_update(&c, &in).kind == MUSIC_TOGGLE && c.paused);
    in.finished = true;
    assert(music_controls_update(&c, &in).kind == MUSIC_TOGGLE && !c.paused);
    in.toggle = in.finished = false;
    in.pointer_visible = true;
    in.pressed = in.held = true;
    in.x = 150;
    in.y = 195;
    assert(music_controls_update(&c, &in).kind == MUSIC_NONE && c.scrubbing);
    in.pressed = false;
    in.x = 400;
    assert(music_controls_update(&c, &in).kind == MUSIC_NONE && c.preview == in.frames);
    in.x = -100;
    in.released = true;
    in.held = false;
    MusicAction a = music_controls_update(&c, &in);
    assert(a.kind == MUSIC_SEEK && !a.sample && !c.scrubbing);
    in.released = false;
    in.pressed = in.held = true;
    in.x = 303;
    in.y = 195;
    music_controls_update(&c, &in);
    in.back = true;
    assert(music_controls_update(&c, &in).kind == MUSIC_NONE && !c.scrubbing);
    in.held = in.pressed = false;
    assert(music_controls_update(&c, &in).kind == MUSIC_BACK);
    c = (MusicControls){0};
    in = (MusicControlInput){.rate = 48000,
                             .frames = 2880000,
                             .duration_known = true,
                             .position = 480000,
                             .right_pressed = true,
                             .right_held = true,
                             .now = 1000};
    a = music_controls_update(&c, &in);
    assert(a.kind == MUSIC_NONE && c.pending_seek && c.preview == 720000);
    in.right_pressed = false;
    in.now = 1250;
    music_controls_update(&c, &in);
    assert(c.seek_delta_ms == 10000 && c.preview == 960000);
    in.right_held = false;
    in.now = 1699;
    assert(music_controls_update(&c, &in).kind == MUSIC_NONE);
    in.now = 1700;
    a = music_controls_update(&c, &in);
    assert(a.kind == MUSIC_SEEK && a.sample == 960000 && !c.pending_seek);
    // Timer wrap, upper/lower limits and cancellation before a disk seek.
    in.position = 1;
    in.left_pressed = in.left_held = true;
    in.now = UINT32_MAX - 100;
    a = music_controls_update(&c, &in);
    assert(a.kind == MUSIC_NONE && !c.preview);
    in.left_pressed = in.left_held = false;
    in.now = 350;
    a = music_controls_update(&c, &in);
    assert(a.kind == MUSIC_SEEK && !a.sample);
    in.right_pressed = in.right_held = true;
    in.position = in.frames - 1;
    in.now = 1000;
    music_controls_update(&c, &in);
    assert(c.preview == in.frames);
    in.back = true;
    in.right_pressed = in.right_held = false;
    assert(music_controls_update(&c, &in).kind == MUSIC_NONE && !c.pending_seek);
    in.back = false;
    // Grabbing the thumb off centre keeps the current position; never jumps.
    in.position = in.frames / 2;
    in.pressed = in.held = in.pointer_visible = true;
    in.x = 163;
    in.y = 202;
    music_controls_update(&c, &in);
    assert(c.scrubbing && c.drag_offset == 4);
    assert(c.preview >= in.position - in.frames / 287 && c.preview <= in.position);
    in.x = 200;
    in.pressed = false;
    music_controls_update(&c, &in);
    assert(c.preview > in.position);
    in.released = true;
    in.held = false;
    a = music_controls_update(&c, &in);
    assert(a.kind == MUSIC_SEEK);
    // Info consumes Esc without stopping playback, blocks covered buttons,
    // and leaves the timeline and keyboard transport available.
    c = (MusicControls){0};
    in = (MusicControlInput){.info = true};
    assert(music_controls_update(&c, &in).kind == MUSIC_INFO && c.info_open);
    in.info = false;
    in.pressed = in.held = in.pointer_visible = true;
    in.x = 160;
    in.y = 164;
    music_controls_update(&c, &in);
    assert(!c.capture);
    in.pressed = in.held = false;
    in.back = true;
    assert(music_controls_update(&c, &in).kind == MUSIC_NONE && !c.info_open);
    in.back = false;
    in.mode = true;
    assert(music_controls_update(&c, &in).kind == MUSIC_MODE);
    in.mode = false;
    in.visual = true;
    assert(music_controls_update(&c, &in).kind == MUSIC_VISUAL);
    in.visual = false;
    in.duration_known = false;
    in.pressed = in.held = true;
    in.x = 160;
    in.y = 202;
    assert(music_controls_update(&c, &in).kind == MUSIC_NONE && !c.scrubbing);
    // Artwork expands without pausing; modal clicks/Esc cannot activate controls below it.
    c = (MusicControls){0};
    in = (MusicControlInput){.pointer_visible = true,
                             .pressed = true,
                             .held = true,
                             .x = 50,
                             .y = 70,
                             .art_enabled = true};
    assert(music_controls_update(&c, &in).kind == MUSIC_NONE && c.capture == MUSIC_ART);
    in.pressed = in.held = false;
    in.released = true;
    assert(music_controls_update(&c, &in).kind == MUSIC_ART && c.art_open && !c.paused);
    in.released = false;
    in.pressed = in.held = true;
    in.x = 160;
    in.y = 164;
    assert(music_controls_update(&c, &in).kind == MUSIC_NONE && !c.art_open && !c.paused);
    c.art_open = true;
    in.pressed = in.held = false;
    in.back = true;
    assert(music_controls_update(&c, &in).kind == MUSIC_NONE && !c.art_open && !c.paused);
    in.back = false;
    in.art_enabled = false;
    in.pressed = in.held = true;
    in.x = 50;
    in.y = 70;
    assert(music_controls_update(&c, &in).kind == MUSIC_NONE && c.capture_background);
    in.pressed=in.held=false;in.released=true;
    assert(music_controls_update(&c,&in).kind==MUSIC_TOGGLE && c.paused);
    /* Enter/5 owns its own release, independently of an idle touchpad. */
    c=(MusicControls){0};
    in=(MusicControlInput){.pointer_visible=true,.x=160,.y=202,.duration_known=true,
        .frames=480000,.rate=48000,.position=240000,.pressed=true,
        .keyboard_pressed=true,.keyboard_held=true,.released=true};
    assert(music_controls_update(&c,&in).kind==MUSIC_NONE && c.scrubbing && c.capture_keyboard);
    in.pressed=in.keyboard_pressed=false;in.x=220;
    assert(music_controls_update(&c,&in).kind==MUSIC_NONE && c.scrubbing);
    in.keyboard_held=false;in.keyboard_released=true;
    assert(music_controls_update(&c,&in).kind==MUSIC_SEEK && !c.capture);
    /* A keyboard release from an earlier loading screen cannot end a physical drag. */
    c=(MusicControls){0};in=(MusicControlInput){.pointer_visible=true,.x=160,.y=202,
        .duration_known=true,.frames=480000,.rate=48000,.position=240000,
        .pressed=true,.held=true,.keyboard_released=true};
    assert(music_controls_update(&c,&in).kind==MUSIC_NONE && c.scrubbing && !c.capture_keyboard);
    in.pressed=false;in.x=200;
    assert(music_controls_update(&c,&in).kind==MUSIC_NONE && c.scrubbing);
    in.held=false;in.released=true;
    assert(music_controls_update(&c,&in).kind==MUSIC_SEEK);
    /* A blank-space click keeps ownership when revealing a hidden HUD under it. */
    c=(MusicControls){0};in=(MusicControlInput){.x=160,.y=164,.pressed=true,.held=true};
    assert(music_controls_update(&c,&in).kind==MUSIC_NONE && c.capture_background);
    in.pointer_visible=true;in.pressed=in.held=false;in.released=true;
    assert(music_controls_update(&c,&in).kind==MUSIC_TOGGLE && c.paused);
    c=(MusicControls){0};in=(MusicControlInput){.x=170,.y=120,.pressed=true,.held=true};
    music_controls_update(&c,&in);in.x+=20;in.pressed=in.held=false;in.released=true;
    assert(music_controls_update(&c,&in).kind==MUSIC_NONE && !c.paused);
    uint32_t until=0;
    assert(music_hud_should_show(1,false,false,false,true,1000,&until) && until==3400);
    assert(music_hud_should_show(1,false,false,false,false,3399,&until));
    assert(!music_hud_should_show(1,false,false,false,false,3400,&until));
    assert(music_hud_should_show(1,true,false,false,false,5000,&until));
    assert(music_hud_should_show(1,false,true,false,false,5000,&until));
    assert(music_hud_should_show(1,false,false,true,false,5000,&until));
    assert(music_hud_should_show(0,false,false,false,false,5000,&until));
    assert(music_hud_should_show(1,false,false,false,true,UINT32_MAX-100,&until));
    assert(music_hud_should_show(1,false,false,false,false,2298,&until));
    assert(!music_hud_should_show(1,false,false,false,false,2299,&until));
    const char *good[] = {"x.MP3",      "dir/y.flac.tns", "a.WAV", "a.ogg",
                          "a.opus.tns", "a.m4a",          "a.aac", "a.nvp.tns"};
    for (unsigned i = 0; i < sizeof(good) / sizeof(*good); ++i)
        assert(media_is_playable(good[i]));
    const char *bad[] = {"a.mp3.bak", "a.txt", "a.tns", "a.mp4", "dir.mp3/a.txt", ""};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); ++i)
        assert(!media_is_playable(bad[i]));
    char title[80];
    media_audio_title("/Music/01 - Artist - Song.MP3.tns", title, sizeof(title));
    assert(!strcmp(title, "01 - Artist - Song"));
    char tiny[2];
    media_audio_title("long song.flac", tiny, sizeof(tiny));
    assert(!strcmp(tiny, "l"));
    puts("PASS native file classification, music pause/EOF, captured scrub/cancel, grab offset, "
         "stacked seek timing/wraparound, nonblocking info and view/mode controls");
}
