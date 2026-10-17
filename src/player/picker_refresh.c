#include "player_internal.h"

static bool same_row(const MovieFile *a,const MovieFile *b)
{
    return a->kind==b->kind && ((!a->path && !b->path) ||
        (a->path && b->path && !strcmp(a->path,b->path)));
}
static uint32_t row_hash(const MovieFile *f)
{
    uint32_t hash=2166136261U^(unsigned)f->kind;
    for(const unsigned char *p=(const unsigned char *)f->path;p && *p;++p)
        hash=(hash^*p)*16777619U;
    return hash;
}
static void refresh_history(MovieFile *old,const MovieFile *fresh)
{
    bool changed=old->resume_frame!=fresh->resume_frame || old->has_resume!=fresh->has_resume;
    old->resume_frame=fresh->resume_frame;old->has_resume=fresh->has_resume;
    if(old->kind==PICKER_MOVIE) {
        old->timing_checked=false;
        if(changed)old->resume_time_known=false;
    }
    if(old->kind==PICKER_FOLDER)old->folder_count_checked=false;
}
void movie_picker_merge_refresh(MoviePickerCache *cache,MoviePickerCache *next)
{
    bool identical=cache->count==next->count;
    for(size_t i=0;identical && i<cache->count;++i)
        identical=same_row(cache->files+i,next->files+i) &&
            cache->files[i].file_bytes==next->files[i].file_bytes;
    if(identical) {
        for(size_t i=0;i<cache->count;++i)refresh_history(cache->files+i,next->files+i);
        snprintf(cache->cover_path,sizeof(cache->cover_path),"%s",next->cover_path);
        free_movie_files(next->files,next->count);next->files=NULL;next->count=0;
        cache->valid=true;cache->load_failed=false;cache->content_changed=false;
        memset(next,0,sizeof(*next));
        return;
    }
    size_t capacity=16;
    while(capacity<cache->count*2)capacity*=2;
    uint16_t *lookup=calloc(capacity,sizeof(*lookup));
    if(lookup)for(size_t i=0;i<cache->count;++i) {
        size_t at=row_hash(cache->files+i)&(capacity-1);
        while(lookup[at])at=(at+1)&(capacity-1);
        lookup[at]=(uint16_t)(i+1);
    }
    next->has_position=cache->has_position;
    next->selected_index=cache->selected_index<next->count?cache->selected_index:0;
    next->scroll_start=cache->scroll_start<next->count?cache->scroll_start:0;
    for(size_t i=0;i<next->count;++i) {
        MovieFile *fresh=next->files+i;
        size_t match=cache->count;
        if(lookup) {
            size_t at=row_hash(fresh)&(capacity-1);
            while(lookup[at]) {
                size_t index=lookup[at]-1;
                if(same_row(cache->files+index,fresh)){match=index;break;}
                at=(at+1)&(capacity-1);
            }
        } else for(size_t j=0;j<cache->count;++j)
            if(same_row(cache->files+j,fresh)){match=j;break;}
        if(match==cache->count)continue;
        MovieFile *old=cache->files+match;
        if(match==cache->selected_index)next->selected_index=i;
        if(match==cache->scroll_start)next->scroll_start=i;
        fresh->row_new=false;fresh->reveal_ms=old->reveal_ms;fresh->reveal_active=old->reveal_active;
        if(old->file_bytes==fresh->file_bytes) {
            refresh_history(old,fresh);
            free(fresh->name);free(fresh->detail);free(fresh->path);free(fresh->encrypted_header);
            *fresh=*old;*old=(MovieFile){0};fresh->row_new=false;
        }
    }
    free(lookup);
    free_movie_files(cache->files,cache->count);
    next->valid=true;next->load_failed=false;next->content_changed=true;
    *cache=*next;
    memset(next,0,sizeof(*next));
}
void picker_reveal_rows(MovieFile *files,size_t count,size_t start,uint32_t now,bool all)
{
    for(size_t i=0;i<count;++i) {
        if(all || files[i].row_new) {
            bool visible=i>=start && i<start+PICKER_VISIBLE_ROWS+1;
            files[i].reveal_active=visible;
            files[i].reveal_ms=now+115U+(visible?(uint32_t)(i-start)*PICKER_INTRO_ROW_STAGGER_MS:0);
        }
        files[i].row_new=false;
    }
}
uint8_t picker_row_reveal_mix(MovieFile *file,uint32_t now)
{
    if(!file->reveal_active)return 255;
    int32_t elapsed=(int32_t)(now-file->reveal_ms);
    if(elapsed<=0)return 0;
    uint8_t mix=ui_ease_out_cubic((uint32_t)elapsed,PICKER_INTRO_ANIM_MS-120U);
    if(mix==255)file->reveal_active=false;
    return mix;
}
