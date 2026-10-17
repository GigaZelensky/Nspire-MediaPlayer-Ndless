#ifndef NDVIDEO_MEDIA_FILES_H
#define NDVIDEO_MEDIA_FILES_H
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include "media_config.h"
static inline bool media_suffix(const char *name,size_t length,const char *suffix)
{
    size_t n=strlen(suffix);
    if (n>length) return false;
    for (size_t i=0;i<n;++i)
        if (tolower((unsigned char)name[length-n+i])!=tolower((unsigned char)suffix[i])) return false;
    return true;
}
static inline const char *media_audio_format(const char *path)
{
    if (!path) return NULL;
    size_t n=strlen(path);
    if (media_suffix(path,n,".tns")) n-=4;
    if (media_suffix(path,n,".mp3")) return "MP3";
    if (media_suffix(path,n,".flac")) return "FLAC";
    if (media_suffix(path,n,".wav") || media_suffix(path,n,".wave")) return "WAV";
    if (media_suffix(path,n,".ogg") || media_suffix(path,n,".oga")) return "Ogg audio";
    if (media_suffix(path,n,".opus")) return "Opus";
    if (media_suffix(path,n,".aac") || media_suffix(path,n,".m4a") || media_suffix(path,n,".m4b")) return "AAC";
    return NULL;
}
static inline unsigned media_cover_rank(const char *name)
{
    size_t n=strlen(name);if(media_suffix(name,n,".tns"))n-=4;
    const char *parts[]={"cover.jpg","cover.jpeg","cover.png","folder.jpg","folder.jpeg","folder.png","front.jpg","front.jpeg","front.png"};
    for(unsigned i=0;i<sizeof(parts)/sizeof(*parts);++i)
        if(media_suffix(name,n,parts[i]))return i<3?3:i<6?2:1;
    return 0;
}
static inline bool media_is_audio(const char *path)
{ return NDMEDIA_AUDIO && media_audio_format(path)!=NULL; }
static inline bool media_audio_path_supported(const char *path)
{
    const char *format=media_audio_format(path);
    if(!format)return false;
    if(!strcmp(format,"MP3"))return media_audio_codec_supported(2);
    if(!strcmp(format,"FLAC"))return media_audio_codec_supported(3);
    if(!strcmp(format,"AAC"))return media_audio_codec_supported(5);
    if(!strcmp(format,"Opus"))return media_audio_codec_supported(6);
    if(!strcmp(format,"Ogg audio"))return media_audio_codec_supported(4) || media_audio_codec_supported(6);
    return NDMEDIA_AUDIO && NDMEDIA_WITH_WAV;
}
static inline bool media_is_playable(const char *path)
{
    if (!path) return false;
    size_t n=strlen(path);
    return media_is_audio(path) || media_suffix(path,n,".nvp") || media_suffix(path,n,".nvp.tns");
}
static inline void media_audio_title(const char *path,char *out,size_t bytes)
{
    if (!bytes) return;
    const char *name=strrchr(path,'/');name=name?name+1:path;
    size_t n=strlen(name);
    if (media_suffix(name,n,".tns")) n-=4;
    size_t dot=n;
    while (dot && name[dot-1]!='.') --dot;
    if (dot) n=dot-1;
    if (n>=bytes) n=bytes-1;
    memcpy(out,name,n);out[n]=0;
}
#endif
