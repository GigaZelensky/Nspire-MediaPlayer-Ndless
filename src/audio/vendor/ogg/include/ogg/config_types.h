#ifndef NDVIDEO_OGG_TYPES_H
#define NDVIDEO_OGG_TYPES_H
#include <stdint.h>
typedef short ogg_int16_t;
typedef unsigned short ogg_uint16_t;
typedef int ogg_int32_t;
typedef unsigned int ogg_uint32_t;
typedef long long ogg_int64_t;
typedef unsigned long long ogg_uint64_t;
typedef char ndvideo_ogg_integer_sizes[(sizeof(ogg_int16_t)==2 && sizeof(ogg_int32_t)==4 &&
                                       sizeof(ogg_int64_t)==8)?1:-1];
#endif
