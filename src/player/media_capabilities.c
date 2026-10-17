#include "player_internal.h"
#include "media_files.h"

bool picker_codec_unavailable(const MovieFile *file)
{
    if(!file)return false;
    if(file->kind==PICKER_AUDIO)return !media_audio_path_supported(file->path);
    return file->kind==PICKER_MOVIE && file->metadata_codec!=MOVIE_CODEC_UNKNOWN &&
        !media_video_codec_supported((unsigned)file->metadata_codec);
}
