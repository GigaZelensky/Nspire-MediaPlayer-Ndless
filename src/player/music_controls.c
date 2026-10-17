#include "music_controls.h"

bool music_hud_should_show(unsigned view,bool paused,bool modal,bool gesture,bool activity,
                           uint32_t now,uint32_t *until)
{
    if(activity)*until=now+2400;
    return !view || paused || modal || gesture || (int32_t)(now-*until)<0;
}

static bool rect(int x, int y, int left, int top, int width, int height)
{
    return x >= left && y >= top && x < left + width && y < top + height;
}
int music_control_at(int x, int y, bool duration_known)
{
    if (rect(x, y, 8, 8, 44, 22))
        return MUSIC_BACK;
    if (rect(x, y, 268, 8, 44, 22))
        return MUSIC_INFO;
    if (rect(x, y, 14, 40, 92, 92))
        return MUSIC_ART;
    if (rect(x, y, 80, 151, 40, 34))
        return MUSIC_PREVIOUS;
    if (rect(x, y, 140, 148, 40, 40))
        return MUSIC_TOGGLE;
    if (rect(x, y, 200, 151, 40, 34))
        return MUSIC_NEXT;
    if (rect(x, y, 16, 155, 42, 26))
        return MUSIC_MODE;
    if (rect(x, y, 260, 155, 44, 26))
        return MUSIC_VISUAL;
    if (duration_known && rect(x, y, MUSIC_SEEK_X, MUSIC_SEEK_Y, MUSIC_SEEK_W, MUSIC_SEEK_H))
        return MUSIC_SEEK;
    return MUSIC_NONE;
}
static uint32_t pointer_sample(int x, uint32_t frames)
{
    int offset = x - MUSIC_SEEK_X;
    if (offset < 0)
        offset = 0;
    if (offset >= MUSIC_SEEK_W)
        offset = MUSIC_SEEK_W - 1;
    return (uint32_t)((uint64_t)(unsigned)offset * frames / (MUSIC_SEEK_W - 1));
}
MusicAction music_controls_update(MusicControls *c, const MusicControlInput *in)
{
    MusicAction out = {MUSIC_NONE, 0};
    int hot = in->pointer_visible ? music_control_at(in->x, in->y, in->duration_known) : MUSIC_NONE;
    if (hot == MUSIC_ART && !in->art_enabled)
        hot = MUSIC_NONE;
    if (c->art_open)
    {
        hot = MUSIC_NONE;
        if (in->back || in->pressed)
        {
            c->art_open = false;
            c->capture = 0;
            return out;
        }
    }
    if (in->back)
    {
        if (c->info_open)
        {
            c->info_open = false;
            return out;
        }
        if (c->capture)
        {
            c->capture = 0;
            c->scrubbing = false;
            return out;
        }
        if (c->pending_seek)
        {
            c->pending_seek = false;
            c->seek_delta_ms = 0;
            return out;
        }
        return (MusicAction){MUSIC_BACK, 0};
    }
    if (in->pressed && !c->capture)
    {
        if (c->info_open && hot != MUSIC_INFO && hot != MUSIC_BACK && hot != MUSIC_SEEK)
            hot = 0;
        c->capture = hot;
        c->capture_keyboard = in->keyboard_pressed;
        c->capture_background = !hot && !c->info_open && !c->art_open;
        c->capture_x = in->x;c->capture_y = in->y;
        if(c->capture_background)c->capture=MUSIC_TOGGLE;
        c->scrubbing = hot == MUSIC_SEEK;
        if (c->scrubbing)
        {
            c->pending_seek = false;
            c->seek_delta_ms = 0;
            c->drag_offset = 0;
            if (in->frames)
            {
                int center =
                    MUSIC_SEEK_X + (int)((uint64_t)in->position * (MUSIC_SEEK_W - 1) / in->frames);
                if (in->x >= center - 5 && in->x <= center + 5)
                    c->drag_offset = in->x - center;
            }
        }
    }
    bool held=c->capture_keyboard?in->keyboard_held:in->held;
    bool released=c->capture_keyboard?in->keyboard_released:in->released;
    if (c->scrubbing && (held || released))
        c->preview = pointer_sample(in->x - c->drag_offset, in->frames);
    if (c->capture && (released || !held))
    {
        if (c->capture == MUSIC_SEEK)
            out = (MusicAction){MUSIC_SEEK, c->preview};
        else if(c->capture_background) {
            int dx=in->x-c->capture_x,dy=in->y-c->capture_y;
            if(dx>=-6 && dx<=6 && dy>=-6 && dy<=6)out.kind=MUSIC_TOGGLE;
        }
        else if (c->capture == hot)
            out.kind = hot;
        c->capture = 0;
        c->capture_keyboard=c->capture_background=false;
        c->scrubbing = false;
    }
    if (!c->capture)
    {
        if (in->toggle)
            out.kind = MUSIC_TOGGLE;
        if (in->next)
            out.kind = MUSIC_NEXT;
        if (in->previous)
            out.kind = MUSIC_PREVIOUS;
        if (in->mode)
            out.kind = MUSIC_MODE;
        if (in->visual)
            out.kind = MUSIC_VISUAL;
        if (in->info)
            out.kind = MUSIC_INFO;
        int direction = in->left_held == in->right_held ? 0 : in->right_held ? 1 : -1;
        bool pressed = direction > 0 ? in->right_pressed : in->left_pressed;
        if (direction && pressed)
        {
            c->repeat_direction = direction;
            c->repeat_at = in->now + 250;
        }
        else if (!direction || direction != c->repeat_direction)
            c->repeat_direction = 0;
        else if ((int32_t)(in->now - c->repeat_at) >= 0)
        {
            pressed = true;
            c->repeat_at = in->now + 80;
        }
        if (direction && pressed)
        {
            if (!c->pending_seek || (c->seek_delta_ms < 0) != (direction < 0))
                c->badge_started = in->now;
            int64_t delta = (int64_t)c->seek_delta_ms + direction * 5000;
            int64_t max_ms = in->rate ? (int64_t)in->frames * 1000 / in->rate : INT32_MAX;
            if (max_ms > INT32_MAX)
                max_ms = INT32_MAX;
            if (delta > max_ms)
                delta = max_ms;
            if (delta < -max_ms)
                delta = -max_ms;
            c->seek_delta_ms = c->badge_delta_ms = (int32_t)delta;
            c->pending_seek = true;
            c->seek_at = in->now + 450;
            c->badge_hide_at = c->seek_at + 300;
        }
        if (c->pending_seek)
        {
            int64_t sample = (int64_t)in->position + (int64_t)c->seek_delta_ms * in->rate / 1000;
            uint32_t limit = in->duration_known ? in->frames : UINT32_MAX;
            c->preview = sample < 0 ? 0 : sample > limit ? limit : (uint32_t)sample;
            if ((int32_t)(in->now - c->seek_at) >= 0)
            {
                out = (MusicAction){MUSIC_SEEK, c->preview};
                c->pending_seek = false;
                c->seek_delta_ms = 0;
            }
        }
    }
    if (out.kind == MUSIC_TOGGLE)
    {
        c->paused = !c->paused;
        if (in->finished)
        {
            c->paused = false;
            out.sample = 0;
        }
    }
    if (out.kind == MUSIC_INFO)
        c->info_open = !c->info_open;
    if (out.kind == MUSIC_ART)
        c->art_open = true;
    return out;
}
