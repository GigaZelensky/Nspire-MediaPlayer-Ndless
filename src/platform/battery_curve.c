#include "battery_curve.h"
/* Default loaded-playback prior from a CX II USB music discharge run:
 * 396 MHz, FLAC44.1kHz stereo, volume80%, screen-off reference, about106.8min
 * to the3.50V reserve. Starting full charge was assumed, not independently
 * established. This is an approximate usable-playback curve, not chemical SOC.
 * Voltage at5% fractions was smoothed with a second-difference penalty4,
 * lower measured region retained. Monotone cubic derivatives use Q12 units.
 * This fixed table is a starting prior; no user calibration run is required. */
/* The measured run did not establish a fully charged upper endpoint.
 * Extend only its top 10% to a conservative 4.20 V prior; otherwise a fresh
 * 4.16 V battery remains saturated at 100 until it falls below 3.964 V.
 * This unmeasured upper tail remains an estimate, identified in Info. */
static const struct { uint16_t mv,percent,slope; } points[]={
    {3498,0,13653},
    {3513,50,13653},
    {3528,100,13653},
    {3543,150,14130},
    {3557,200,14130},
    {3572,250,13217},
    {3588,300,12800},
    {3604,350,11732},
    {3623,400,10505},
    {3643,450,9760},
    {3665,500,8910},
    {3689,550,8360},
    {3714,600,8032},
    {3740,650,7589},
    {3768,700,7314},
    {3796,750,7065},
    {3826,800,6609},
    {3858,850,6302},
    {3891,900,4000},
    {3927,925,2800},
    {3964,950,2200},
    {4040,970,1000},
    {4120,985,700},
    {4200,1000,768},
};
uint32_t battery_curve_percent(uint32_t mv)
{
    if(mv<=points[0].mv)return 0;
    unsigned last=sizeof(points)/sizeof(*points)-1;
    if(mv>=points[last].mv)return 1000;
    unsigned i=0;while(mv>=points[i+1].mv)++i;
    int32_t width=points[i+1].mv-points[i].mv;
    int32_t t=(mv-points[i].mv)*4096/width;
    int32_t t2=(t*t+2048)/4096,t3=(t2*t+2048)/4096;
    int64_t value=(int64_t)(2*t3-3*t2+4096)*points[i].percent+
        (int64_t)(-2*t3+3*t2)*points[i+1].percent+
        ((int64_t)(t3-2*t2+t)*width*points[i].slope+
         (int64_t)(t3-t2)*width*points[i+1].slope)/4096;
    int32_t result=(int32_t)((value+2048)/4096);
    if(result<points[i].percent)result=points[i].percent;
    if(result>points[i+1].percent)result=points[i+1].percent;
    return (uint32_t)result;
}
uint32_t battery_curve_usable(uint32_t mv,uint32_t reserve)
{
    uint32_t zero=battery_curve_percent(reserve);
    if(zero>=900)zero=0; /* Invalid/unknown system reserve: keep the default. */
    uint32_t value=battery_curve_percent(mv);
    return value<=zero?0:(1000U*(value-zero)+(1000U-zero)/2)/(1000U-zero);
}
