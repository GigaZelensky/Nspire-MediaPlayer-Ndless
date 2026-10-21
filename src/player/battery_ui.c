#include "player_internal.h"
#include "platform/battery_meter.h"

#if NDMEDIA_BATTERY_METER
static bool battery_arrived;
static uint32_t battery_arrived_at;
static uint8_t battery_presence_mix(void)
{
    uint32_t now=monotonic_clock_now_ms();
    if(!battery_arrived){battery_arrived=true;battery_arrived_at=now;}
    return ui_ease_smoothstep(now-battery_arrived_at,220);
}
#endif
bool battery_ui_animating(void)
{
#if NDMEDIA_BATTERY_METER
    uint8_t mix=battery_presence_mix();
    return battery_arrived && mix<255;
#else
    return false;
#endif
}

void draw_battery_meter(SDL_Surface *s,const Fonts *f,int x,int y,uint8_t mix)
{
#if NDMEDIA_BATTERY_METER
    BatteryMeterInfo battery;
    if(!mix)return;
    if(!battery_meter_info(&battery))battery=(BatteryMeterInfo){0};
    mix=(unsigned)mix*battery_presence_mix()/255;
    char text[12];int label=x+23;
    {
        SDL_Rect edges[]={{x+1,y-1,16,1},{x+1,y+8,16,1},{x,y,1,8},{x+17,y,1,8},{x+18,y+2,2,4}};
        for(unsigned i=0;i<sizeof(edges)/sizeof(*edges);++i) {
            SDL_Rect shadow=edges[i];++shadow.x;++shadow.y;
            fill_rect_rgb565_mix(s,&shadow,UI_COLOR_BLACK,mix);
        }
        for(unsigned i=0;i<sizeof(edges)/sizeof(*edges);++i)
            fill_rect_rgb565_mix(s,edges+i,UI_COLOR_WHITE,mix);
        if(battery.charging || battery.checking)
            draw_battery_power_glyph(s,x+5,y,mix,battery.checking);
        else if(battery.percent_valid) {
            unsigned width=(14*battery.percent_tenths+500)/1000;
            if(width) {SDL_Rect fill={x+2,y+1,(Uint16)width,6};
                fill_rect_rgb565_mix(s,&fill,battery.percent_tenths<=100?UI_COLOR_ACCENT_HOT:UI_COLOR_WHITE,mix);}
        }
        if(battery.percent_valid)snprintf(text,sizeof(text),"%u%%",(unsigned)(battery.percent_tenths+5)/10);
        else snprintf(text,sizeof(text),"--");
    }
    draw_ui_label_tint(s,f,label+1,y+1,text,UI_COLOR_BLACK,mix);
    draw_ui_label_tint(s,f,label,y,text,UI_COLOR_WHITE,mix);
#else
    (void)s;(void)f;(void)x;(void)y;(void)mix;
#endif
}

int battery_meter_width(const Fonts *f)
{
#if NDMEDIA_BATTERY_METER
    BatteryMeterInfo battery;char text[12];
    if(!battery_meter_info(&battery))battery=(BatteryMeterInfo){0};
    if(battery.percent_valid)snprintf(text,sizeof(text),"%u%%",(unsigned)(battery.percent_tenths+5)/10);
    else snprintf(text,sizeof(text),"--");
    return 23+nSDL_GetStringWidth(f->white,text);
#else
    (void)f;return 0;
#endif
}

int draw_video_battery_badge(SDL_Surface *s,const Fonts *f,int right,int y,uint8_t mix)
{
#if NDMEDIA_BATTERY_METER
    BatteryMeterInfo battery;
    if(!battery_meter_info(&battery))battery=(BatteryMeterInfo){0};
    char label[12];
    if(battery.percent_valid)snprintf(label,sizeof(label),"%u%%",(unsigned)(battery.percent_tenths+5)/10);
    else snprintf(label,sizeof(label),"--");
    int width=23+nSDL_GetStringWidth(f->white,label)+8,left=right-width;
    /* Status, not a button: flat body, two-pixel corner cuts, no raised face,
     * reflection, hover or pressed treatment. Same 16 px height as Fit/Speed. */
    Uint16 top=blend_rgb565(ui_theme()->gunmetal,UI_COLOR_WHITE,38);
    Uint16 bottom=blend_rgb565(ui_theme()->gunmetal,UI_COLOR_BLACK,45);
    uint8_t body_mix=(unsigned)mix*battery_presence_mix()/255;
    if(body_mix)for(int row=0;row<16;++row) {
        int inset=row==0 || row==15?2:row==1 || row==14?1:0;
        SDL_Rect span={left+inset,y+row,width-2*inset,1};
        fill_rect_rgb565_mix(s,&span,row==0 || row==15?ui_theme()->row_divider:rgb565_lerp(top,bottom,row,15),body_mix);
    }
    draw_battery_meter(s,f,left+4,y+4,mix);
    return left-6;
#else
    (void)s;(void)f;(void)y;(void)mix;return right;
#endif
}
