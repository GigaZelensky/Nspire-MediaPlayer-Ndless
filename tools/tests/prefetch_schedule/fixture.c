
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define PREFETCH_CHUNK_COUNT 16
#define PREFETCH_IDLE 0
#define PREFETCH_READING 1
#define PREFETCH_READY 2
#define PREFETCH_FAILED 3
#define PREFETCH_PAUSED_SLICE_MS 12U
#define PREFETCH_ACTIVE_H264_SLICE_MS 8U
#define H264_PREFETCH_IO_PRIORITY_SLICE_MS 4U
typedef struct {unsigned first_frame,frame_count;}ChunkIndexEntry;
typedef struct {int chunk_index,state;size_t read_offset;unsigned char*chunk_storage;}PrefetchedChunk;
typedef struct {unsigned chunk_count,frame_count;}Header;
typedef struct {Header header;ChunkIndexEntry*chunk_index;PrefetchedChunk prefetched[16];int loaded_chunk;uint32_t current_frame,next_frame;unsigned queued,slots;bool lookahead,async,reloading;unsigned diag_usb_prefetch_early_turns,diag_usb_prefetch_early_services;uint64_t diag_usb_prefetch_early_bytes;unsigned diag_io_priority_count,diag_prefetch_tick_count,diag_active_prefetch_tick_count;}Movie;
typedef struct {int unused;}PointerState;
static unsigned clock_ms,allocs,submits,polls,copies,services,warm_steps,abort_checks,max_budget,service_progress,ready_after=3,blocks=1;static int phase,request_chunk;static bool parked,abort_value;
static unsigned char storage[16][256];static ChunkIndexEntry entries[64];
static uint32_t monotonic_clock_now_ms(void){return clock_ms;}
static uint32_t monotonic_clock_ticks_per_second(void){return 32768;}
static unsigned movie_prefetch_slots(const Movie*m){return m?m->slots:16;}
static bool video_lookahead_active(const Movie*m){return m&&m->lookahead;}
static bool video_lookahead_reloading(const Movie*m){return m&&m->reloading;}
static unsigned video_lookahead_queued(const Movie*m){return m->queued;}
static uint32_t video_lookahead_next_frame(const Movie*m){return m->next_frame;}
static bool movie_async_enabled(const Movie*m){return m&&m->async;}
static bool movie_async_needs_request(const Movie*m){(void)m;return !parked&&(phase==0||phase==2);}
static int movie_chunk_for_frame(const Movie*m,uint32_t frame){for(unsigned i=0;i<m->header.chunk_count;i++)if(frame>=entries[i].first_frame&&frame-entries[i].first_frame<entries[i].frame_count)return i;return-1;}
static PrefetchedChunk*find_prefetched_chunk(Movie*m,int index){for(unsigned i=0;i<16;i++)if(m->prefetched[i].chunk_index==index)return&m->prefetched[i];return NULL;}
static const PrefetchedChunk*find_prefetched_chunk_const(const Movie*m,int index){return find_prefetched_chunk((Movie*)m,index);}
static void add(Movie*m,int index,int state){for(unsigned i=0;i<16;i++)if(m->prefetched[i].chunk_index<0){m->prefetched[i]=(PrefetchedChunk){index,state,0,storage[i]};memset(storage[i],0xA5,32);return;}abort();}
static bool prefetch_abort_requested(const PointerState*p){(void)p;abort_checks++;return abort_value;}
static bool collect;
static bool debug_should_collect_metrics(void){return collect;}
static void debug_tracef(const char*f,...){(void)f;}
static bool movie_uses_decode_ahead(const Movie*m){return m->lookahead;}
static int prefetch_budget_for_state(const Movie*m,bool paused,uint32_t spare){(void)m;(void)paused;(void)spare;return 1;}
static bool should_prioritize_next_chunk_io(const Movie*m,int current){(void)m;(void)current;return false;}
static bool should_accelerate_next_chunk_io(Movie*m,int current){(void)m;(void)current;return false;}
static bool should_prefetch_second_next_chunk(Movie*m,int current){(void)m;(void)current;return false;}
static bool prefetch_deadline_reached(uint32_t deadline){return clock_ms>=deadline;}
static void prefetch_ahead(Movie*m,int current,int budget,int distance){(void)budget;allocs++;for(int n=1;n<=distance&&(unsigned)(current+n)<m->header.chunk_count;n++)if(!find_prefetched_chunk(m,current+n)){add(m,current+n,PREFETCH_READING);break;}}
static bool prefetch_read_step(Movie*m,PrefetchedChunk*c,bool respect,uint32_t due){(void)m;(void)respect;(void)due;polls++;if(!phase){phase=1;submits++;request_chunk=c->chunk_index;service_progress=0;return true;}assert(request_chunk==c->chunk_index);if(phase==2){memset(c->chunk_storage+c->read_offset,request_chunk,32);c->read_offset+=32;if(c->read_offset==blocks*32U)c->state=PREFETCH_READY;copies++;phase=0;}return true;}
static bool prefetch_read_step_impl(Movie*m,PrefetchedChunk*c,bool respect,uint32_t due,bool maintain){(void)maintain;return prefetch_read_step(m,c,respect,due);}
static void movie_async_service(Movie*m,unsigned ticks){(void)m;services++;if(ticks>max_budget)max_budget=ticks;assert(ticks<=128);clock_ms+=(ticks+31)/32;if(parked){parked=false;return;}if(phase==1&&++service_progress>=ready_after)phase=2;}
static bool movie_async_read_step(Movie*m,uint32_t ticks){(void)m;assert(ticks>0);warm_steps++;if(phase==1&&++service_progress>=ready_after)phase=2;return true;}
static void prefetch_do_work(Movie*m,int current,int distance,uint32_t due,bool defer,const PointerState*p){(void)m;(void)current;(void)distance;(void)due;(void)defer;(void)p;abort();}
static void reset(Movie*m){memset(m,0,sizeof(*m));m->header=(Header){12,1200};m->chunk_index=entries;m->loaded_chunk=0;m->next_frame=1;m->slots=8;m->lookahead=m->async=true;for(unsigned i=0;i<64;i++)entries[i]=(ChunkIndexEntry){i*100,100};for(unsigned i=0;i<16;i++)m->prefetched[i].chunk_index=-1;add(m,1,PREFETCH_READY);add(m,2,PREFETCH_READY);add(m,3,PREFETCH_READING);clock_ms=allocs=submits=polls=copies=services=warm_steps=abort_checks=max_budget=service_progress=phase=0;ready_after=3;blocks=1;request_chunk=-1;parked=abort_value=collect=false;}


static bool full_speed;
static bool movie_async_full_speed_usb(const Movie*m){return m&&m->async&&full_speed;}
/* PRODUCTION */
int main(void){Movie m;
 for(unsigned q=0;q<=2;q++){reset(&m);m.queued=q;prefetch_tick(&m,false,5,NULL);if(CANDIDATE&&q<=1){assert(!submits&&!services&&!polls&&!allocs&&!abort_checks);}else{assert(submits==1&&services==1&&max_budget==128);}}
 reset(&m);assert(prefetch_wait_step(&m,130)==!CANDIDATE);assert(submits==!CANDIDATE);
 /* A request already in flight keeps all bounded service, even as reserve empties. */
 reset(&m);m.queued=2;prefetch_tick(&m,false,5,NULL);assert(phase==1);m.queued=0;prefetch_tick(&m,false,5,NULL);assert(services==2&&phase==1);prefetch_tick(&m,false,5,NULL);assert(services==3&&copies==1&&phase==0);unsigned before=submits;prefetch_tick(&m,false,5,NULL);assert(submits==before+!CANDIDATE);
 /* The short-wait warm progression bypasses any new-request deferral. */
 reset(&m);m.queued=2;prefetch_tick(&m,false,5,NULL);m.queued=0;assert(prefetch_wait_step(&m,130));assert(warm_steps==1&&phase==1);assert(prefetch_wait_step(&m,130));assert(warm_steps==2&&phase==2);prefetch_tick(&m,true,5,NULL);assert(copies==1&&phase==0);
 /* A parked/native handoff retains the normal service path. */
 reset(&m);parked=true;prefetch_tick(&m,false,5,NULL);assert(services==1&&!parked);
 /* Pause and non-lookahead readers keep filling even with no decoded reserve. */
 reset(&m);prefetch_tick(&m,true,5,NULL);assert(submits==1&&services==1);reset(&m);m.lookahead=false;prefetch_tick(&m,false,5,NULL);assert(submits==1&&services==1);
 /* Missing current successor or second successor is never speculative. */
 for(int missing=1;missing<=2;missing++){reset(&m);find_prefetched_chunk(&m,missing)->state=PREFETCH_READING;prefetch_tick(&m,false,5,NULL);assert(submits==1&&request_chunk==missing&&services==1);}
 /* Use producer loaded chunk3, not the visible frame still in chunk0. */
 reset(&m);m.loaded_chunk=3;m.next_frame=301;find_prefetched_chunk(&m,3)->state=PREFETCH_READY;add(&m,4,PREFETCH_READING);add(&m,5,PREFETCH_READY);prefetch_tick(&m,false,5,NULL);assert(request_chunk==4&&services==1);
 /* At the existing urgent boundary, tiny ready chunks still earn service. */
 reset(&m);entries[0]=(ChunkIndexEntry){0,16};entries[1]=(ChunkIndexEntry){16,4};entries[2]=(ChunkIndexEntry){20,5};m.current_frame=8;m.next_frame=9;prefetch_tick(&m,false,0,NULL);assert(submits==1&&services==1&&max_budget==64);
 reset(&m);entries[0]=(ChunkIndexEntry){0,16};entries[1]=(ChunkIndexEntry){16,4};entries[2]=(ChunkIndexEntry){20,5};m.current_frame=7;m.next_frame=8;prefetch_tick(&m,false,5,NULL);assert(submits==!CANDIDATE);
 /* Crossing producer boundaries promotes the previously speculative job to
    protected second successor. Even permanently zero RGB reserve cannot starve it. */
 reset(&m);for(unsigned current=1;current<=7;current++){m.loaded_chunk=current;m.current_frame=current*100;m.next_frame=current*100+1;assert(find_prefetched_chunk(&m,current)->state==PREFETCH_READY);if(!find_prefetched_chunk(&m,current+2))add(&m,current+2,PREFETCH_READING);unsigned attempts=0;while(find_prefetched_chunk(&m,current+2)->state!=PREFETCH_READY){prefetch_tick(&m,false,5,NULL);assert(++attempts<=3);}assert(find_prefetched_chunk(&m,current+1)->state==PREFETCH_READY);for(unsigned byte=0;byte<32;byte++)assert(find_prefetched_chunk(&m,current+2)->chunk_storage[byte]==current+2);}

 /* Overloaded full-speed USB advances the immediate successor before the
    urgent boundary, with the existing64tick cap and exact payload publication. */
 reset(&m);full_speed=collect=true;find_prefetched_chunk(&m,1)->state=PREFETCH_READING;
 for(unsigned n=0;n<3;n++)prefetch_tick(&m,false,0,NULL);
 assert(submits==EARLY && services==3*EARLY && max_budget==64*EARLY);
 if(EARLY){assert(find_prefetched_chunk(&m,1)->state==PREFETCH_READY);assert(m.diag_usb_prefetch_early_turns==3&&m.diag_usb_prefetch_early_services==3&&m.diag_usb_prefetch_early_bytes==32);for(unsigned n=0;n<32;n++)assert(find_prefetched_chunk(&m,1)->chunk_storage[n]==1);}
 /* A24-frame chunk needing8three-turn blocks finishes before its boundary.
    The baseline16-frame gate cannot finish that same staged work in time. */
 reset(&m);full_speed=true;blocks=8;entries[0].frame_count=24;
 entries[1].first_frame=24;entries[1].frame_count=24;
 find_prefetched_chunk(&m,1)->state=PREFETCH_READING;
 for(unsigned frame=0;frame<24;frame++){m.next_frame=frame;prefetch_tick(&m,false,0,NULL);}
 assert(copies==(EARLY?8U:5U));
 assert((find_prefetched_chunk(&m,1)->state==PREFETCH_READY)==(bool)EARLY);
 assert(max_budget==64);
 if(EARLY)for(unsigned byte=0;byte<256;byte++)assert(find_prefetched_chunk(&m,1)->chunk_storage[byte]==1);
 /* A ready successor leaves distant jobs deferred even on full-speed USB. */
 reset(&m);full_speed=true;prefetch_tick(&m,false,0,NULL);assert(!services&&!polls&&!allocs&&!abort_checks);
 reset(&m);full_speed=true;find_prefetched_chunk(&m,2)->state=PREFETCH_READING;prefetch_tick(&m,false,0,NULL);assert(!services&&!submits);
 /* Other transports retain the previous overloaded behavior. */
 reset(&m);full_speed=false;find_prefetched_chunk(&m,1)->state=PREFETCH_READING;prefetch_tick(&m,false,0,NULL);assert(!services&&!submits);
 /* Use the producer, not the displayed chunk, for early reads. */
 reset(&m);full_speed=true;m.loaded_chunk=3;m.next_frame=301;find_prefetched_chunk(&m,3)->state=PREFETCH_READY;add(&m,4,PREFETCH_READING);
 prefetch_tick(&m,false,0,NULL);assert(submits==EARLY);if(EARLY)assert(request_chunk==4&&max_budget==64);
 /* Cancellation and recovery do not start a read. */
 reset(&m);full_speed=true;find_prefetched_chunk(&m,1)->state=PREFETCH_READING;abort_value=true;prefetch_tick(&m,false,0,NULL);assert(!services&&!submits);
 reset(&m);full_speed=true;m.reloading=true;find_prefetched_chunk(&m,1)->state=PREFETCH_READING;prefetch_tick(&m,false,0,NULL);assert(!services&&!submits);
 /* Missing first slot is created at distance1, not a far slot. */
 reset(&m);full_speed=true;find_prefetched_chunk(&m,1)->chunk_index=-1;prefetch_tick(&m,false,0,NULL);assert(submits==EARLY);if(EARLY)assert(request_chunk==1);
 puts(EARLY ? "PASS early full-speed USB successor scheduling and existing prefetch lifecycle" : "PASS baseline reproduces zero-slack successor starvation");
 return 0;
}
