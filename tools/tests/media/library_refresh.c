/* Included after the production cache/refresh code by check_library.py. */
static MovieFile fresh_row(const char *path, unsigned bytes)
{
    return (MovieFile){.name=dup_string(path),.path=dup_string(path),
        .kind=PICKER_AUDIO,.file_bytes=bytes,.row_new=true};
}
static MoviePickerCache fresh_cache(const char *a,const char *b)
{
    MoviePickerCache cache={.valid=true,.count=b?2:1};
    strcpy(cache.directory,"usb:/album");
    cache.files=calloc(cache.count,sizeof(*cache.files));assert(cache.files);
    cache.files[0]=fresh_row(a,100);if(b)cache.files[1]=fresh_row(b,200);
    return cache;
}
static void check_refresh(void)
{
    MoviePickerCache cache=fresh_cache("A","B"),next=fresh_cache("A","B");
    cache.has_position=true;cache.selected_index=1;cache.scroll_start=1;
    MovieFile *array=cache.files;char *name=cache.files[1].name;
    cache.files[1].timing_checked=true;cache.files[1].metadata_ready_ms=42;
    cache.files[1].reveal_ms=20;cache.files[1].row_new=false;
    next.files[1].has_resume=true;next.files[1].resume_frame=500;
    strcpy(next.cover_path,"front.jpg");
    movie_picker_merge_refresh(&cache,&next);
    assert(cache.files==array && cache.files[1].name==name && !cache.content_changed);
    assert(cache.files[1].timing_checked && cache.files[1].metadata_ready_ms==42);
    assert(cache.files[1].resume_frame==500 && cache.files[1].has_resume);
    assert(!strcmp(cache.cover_path,"front.jpg") && !next.files && !next.valid);
    assert(cache.selected_index==1 && cache.scroll_start==1);
    next=fresh_cache("B","C");next.files[0].file_bytes=200;
    movie_picker_merge_refresh(&cache,&next);
    assert(cache.content_changed && cache.count==2 && cache.files[0].name==name);
    assert(cache.selected_index==0 && cache.scroll_start==0);
    assert(!cache.files[0].row_new && cache.files[1].row_new);
    assert(cache.files[0].metadata_ready_ms==42 && !next.files);
    picker_reveal_rows(cache.files,cache.count,0,1000,false);
    assert(!cache.files[0].reveal_active && cache.files[1].reveal_active);
    assert(picker_row_reveal_mix(cache.files,1000)==255);
    assert(picker_row_reveal_mix(cache.files+1,1146)==0);
    assert(picker_row_reveal_mix(cache.files+1,1190)>0);
    assert(picker_row_reveal_mix(cache.files+1,1300)==255 && !cache.files[1].reveal_active);
    next=fresh_cache("B","C");next.files[0].file_bytes=201;
    movie_picker_merge_refresh(&cache,&next);
    assert(cache.files[0].file_bytes==201 && cache.files[0].metadata_ready_ms==0);
    assert(!cache.files[0].row_new && cache.files[1].file_bytes==200);
    clear_movie_picker_cache(&cache);

    /* Exercise table collisions, reused allocations, additions, removals and
       the null-path USB separator in the same replacement transaction. */
    cache=(MoviePickerCache){.count=257,.valid=true,.has_position=true,
        .selected_index=101,.scroll_start=91};
    next=(MoviePickerCache){.count=258,.valid=true};
    cache.files=calloc(cache.count,sizeof(*cache.files));next.files=calloc(next.count,sizeof(*next.files));
    assert(cache.files && next.files);
    for(unsigned i=0;i<256;++i){char path[32];snprintf(path,sizeof(path),"Track %u",i);cache.files[i]=fresh_row(path,i+1);}
    cache.files[256]=(MovieFile){.kind=PICKER_SEPARATOR};
    name=cache.files[101].name;
    for(unsigned i=0;i<255;++i){unsigned index=255-i;char path[32];snprintf(path,sizeof(path),"Track %u",index);next.files[i]=fresh_row(path,index+1);}
    next.files[255]=(MovieFile){.kind=PICKER_SEPARATOR,.row_new=true};
    next.files[256]=fresh_row("New track",10);next.files[257]=fresh_row("Other track",20);
    movie_picker_merge_refresh(&cache,&next);
    assert(cache.selected_index==154 && cache.scroll_start==164 && cache.files[154].name==name);
    assert(cache.files[255].kind==PICKER_SEPARATOR && !cache.files[255].row_new);
    assert(cache.files[256].row_new && cache.files[257].row_new);
    picker_reveal_rows(cache.files,cache.count,154,UINT32_MAX-100,true);
    assert(cache.files[154].reveal_ms==14 && cache.files[155].reveal_ms==46);
    assert(picker_row_reveal_mix(cache.files+154,UINT32_MAX-50)==0);
    assert(picker_row_reveal_mix(cache.files+154,60)>0);
    assert(picker_row_reveal_mix(cache.files+155,60)<picker_row_reveal_mix(cache.files+154,60));
    assert(picker_row_reveal_mix(cache.files+154,160)==255);
    assert(!cache.files[153].reveal_active && !cache.files[160].reveal_active);
    clear_movie_picker_cache(&cache);

    MovieFile folder={.folder_count_valid=true,.folder_files=3,.folder_folders=2,.folder_count_ready_ms=77};
    counting=&folder;pending_files=3;pending_folders=2;count_finished(true);
    assert(!counting && folder.folder_count_checked && folder.folder_count_ready_ms==77);
    counting=&folder;pending_files=9;pending_folders=0;count_finished(false);
    assert(folder.folder_count_valid && folder.folder_files==3 && folder.folder_folders==2 && folder.folder_count_ready_ms==77);
    counting=&folder;count_finished(true);
    assert(folder.folder_files==9 && folder.folder_folders==0 && folder.folder_count_ready_ms==fake_now);
    puts("PASS unchanged refresh retains allocations/metadata; changed rows preserve selection and ownership; additions and folders cascade; wraparound and folder counts remain stable");
}
