#include "header_diagnostic.h"
#if NDMEDIA_HEADER_DIAGNOSTIC
#include "raw_player_io.h"
#include "native_interrupts.h"
#include "storage_mutation.h"
#include "performance_clock.h"
#include <libndls.h>
#include <os.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t asic,control,ttbr,dacr,kind,stage,ready,reused;
    uint64_t offset,file_bytes,token;
    uint32_t requested,result,inode,blocks,index,dirty,clean,regions,snapshot_fault;
    int snapshot_status;
    uint32_t raw_status,raw_error,rel_error,file_mode,index_mode,rel_requested,rel_copied;
    uint32_t region_id,region_phase,region_error,cursor,count,slot,page,region_pending;
    uint32_t request_offset,request_column,request_bytes;
    uint32_t physical,source,media_offset,column,bytes,raw_page,address_status,address_index;
    uint32_t nand_status,nand_error,nand_quiescent,nand_corrected;
    uint32_t chip_id,spi_phase,spi_status,spi_error,spi_features_a,spi_features_b;
    uint32_t device_bytes,media_bytes,physical_base,remap_count,tag_column;
    uint32_t sequences[21];uint16_t units[21];
    uint8_t state[FFX_MAP_STATE_BYTES],meta[64],io[2112];
    unsigned io_bytes;
} HeaderStorage;
static struct {
    bool pending,armed,captured,valid,encrypted,compared,compare_attempted;
    unsigned attempts;
    char path[512],stage[48];uint64_t file_bytes;
    uint32_t received,expected;int error;
    uint8_t header[56];HeaderStorage storage;
    uint8_t native_header[56];size_t native_bytes;long native_size;
    int opened,seek_result,close_result;
} trace;
void header_diagnostic_begin(const char *path)
{
    unsigned attempts=trace.attempts+1;memset(&trace,0,sizeof(trace));
    trace.attempts=attempts;trace.pending=trace.armed=true;
    trace.native_size=trace.seek_result=trace.close_result=-1;
    snprintf(trace.path,sizeof(trace.path),"%s",path?path:"");
    strcpy(trace.stage,"load started");
}
void header_diagnostic_storage(const PortableReaderPlatform *p,const PortableStorageSnapshot *s,
    const RawFileReader *r,uint64_t offset,uint32_t bytes,int result,unsigned stage,bool ready,bool reused)
{
    if(!trace.armed || offset || bytes>4096)return;
    trace.armed=false;trace.captured=true;
    HeaderStorage *d=&trace.storage;
    d->asic=p->asic;d->control=p->control;d->ttbr=p->ttbr;d->dacr=p->dacr;d->kind=p->kind;
    d->offset=offset;d->requested=bytes;d->result=(uint32_t)result;d->stage=stage;d->ready=ready;d->reused=reused;
    d->file_bytes=s->file_bytes;d->inode=s->inode;d->blocks=s->block_count;d->index=s->index_block;
    d->dirty=s->dirty_count;d->clean=s->clean_count;d->regions=s->regions_active;
    d->snapshot_status=s->status;d->snapshot_fault=s->fault;
    d->device_bytes=s->layout.device_bytes;d->media_bytes=s->layout.media_bytes;
    d->physical_base=s->layout.physical_base_bytes;d->remap_count=s->layout.remap_count;d->tag_column=s->tag_column;
    memcpy(d->meta,s->meta,sizeof(d->meta));memcpy(d->state,s->state_data,sizeof(d->state));
    if(!ready)return; /* Reader fields can belong to an older, rejected view. */
    d->raw_status=r->status;d->raw_error=r->error;d->rel_error=r->file.error;
    d->file_mode=r->file.file_mode;d->index_mode=r->file.index_mode;
    d->rel_requested=r->file.requested;d->rel_copied=r->file.copied;
    d->region_id=r->loader.id;d->region_phase=r->loader.phase;d->region_error=r->loader.error;
    d->cursor=r->loader.cursor;d->count=r->loader.count;d->slot=r->loader.slot;d->page=r->loader.page;
    d->region_pending=r->loader.pending;d->request_offset=r->loader.request.media_offset;
    d->request_column=r->loader.request.column;d->request_bytes=r->loader.request.bytes;
    d->token=r->token;d->physical=r->physical;d->source=r->source;
    d->media_offset=r->media_offset;d->column=r->column;d->bytes=r->bytes;
    d->raw_page=r->address.raw_page;d->address_status=r->address.status;d->address_index=r->address.index;
    d->nand_status=r->nand.status;d->nand_error=r->nand.error;
    d->nand_quiescent=r->nand.quiescent;d->nand_corrected=r->nand.corrected;
    if(p->kind==NAND_PAGE_CX2_SPI) {
        const SpiNandReader *spi=&r->nand.engine.spi;
        d->chip_id=spi->chip_id;d->spi_phase=spi->phase;d->spi_status=spi->last_status;
        d->spi_error=spi->error;d->spi_features_a=spi->feature_a0;d->spi_features_b=spi->feature_b0;
    }
    for(unsigned i=0;i<21;++i){d->sequences[i]=r->loader.units[i].sequence;d->units[i]=r->loader.units[i].unit;}
    uintptr_t io=(uintptr_t)r->io,begin=(uintptr_t)r->io_storage;
    if(io>=begin && io<=begin+sizeof(r->io_storage)-sizeof(d->io)) {
        d->io_bytes=r->bytes<sizeof(d->io)?r->bytes:sizeof(d->io);
        memcpy(d->io,r->io,d->io_bytes);
    }
}
void header_diagnostic_result(const char *stage,uint64_t bytes,bool encrypted,
    const uint8_t *data,size_t received,size_t expected,int error,bool valid)
{
    if(!trace.pending)return;
    snprintf(trace.stage,sizeof(trace.stage),"%s",stage);
    trace.file_bytes=bytes;trace.encrypted=encrypted;trace.received=(uint32_t)received;
    trace.expected=(uint32_t)expected;trace.error=error;trace.valid=valid;trace.armed=false;
    size_t count=received<sizeof(trace.header)?received:sizeof(trace.header);
    if(data && count)memcpy(trace.header,data,count);
}
static void dump(FILE *f,const char *label,const uint8_t *p,unsigned n)
{
    fprintf(f,"%s bytes=%u\n",label,n);
    for(unsigned i=0;i<n;i+=32) {
        fprintf(f,"%04x:",i);
        for(unsigned j=i;j<n && j<i+32;++j)fprintf(f,"%02x",p[j]);
        fputc('\n',f);
    }
}
static const char *raw_reason(unsigned code)
{
    switch(code) {
    case 10:case 11:return "physical address translation";
    case 12:return "NAND request refused";
    case 13:return "NAND transfer failed";
    case 14:return "FlashFX region supply rejected";
    case 15:return "Reliance block supply rejected";
    case 16:return "FlashFX region reconstruction";
    default:return "see numeric reader status";
    }
}
void header_diagnostic_compare(const char *path)
{
    if(!trace.pending || !path || strcmp(path,trace.path))return;
    trace.pending=false;
    trace.compared=true;
    /* Original failure is already copied into owned RAM. All media streams
     * for the failed movie have closed. The caller has drained foreground
     * I/O. Use actual SDK Nucleus calls, never our raw stdio wrapper.
     * This comparison cannot rescue or alter the original playback result. */
    unsigned saved=native_interrupt_mask();raw_player_before_native();native_critical_leave(saved);
    trace.compare_attempted=trace.path[0] && strncmp(trace.path,"usb:",4) && !trace.encrypted;
    if(trace.compare_attempted) {
        storage_mutation_invalidate();
        NUC_FILE *file=nuc_fopen(trace.path,"rb");
        if(file) {
            trace.opened=1;trace.native_bytes=nuc_fread(trace.native_header,1,sizeof(trace.native_header),file);
            trace.seek_result=nuc_fseek(file,0,SEEK_END);
            if(!trace.seek_result)trace.native_size=nuc_ftell(file);
            trace.close_result=nuc_fclose(file);
        }
        storage_mutation_invalidate();
    }
    native_critical_leave(saved);
}
void header_diagnostic_write(FILE *f)
{
    if(!trace.attempts) {
        fputs("header_diagnostic_capture=none\n",f);
        return;
    }
    fprintf(f,"nMedia header diagnostic v2 | %s %s\nlast_attempt=%u path=%s\n"
        "stage=%s expected=%lu received=%lu errno=%d valid_header=%u encrypted=%u logical_file_bytes=%llu\n",
        __DATE__,__TIME__,trace.attempts,trace.path,trace.stage,(unsigned long)trace.expected,
        (unsigned long)trace.received,trace.error,trace.valid,trace.encrypted,(unsigned long long)trace.file_bytes);
    fprintf(f,"hardware type=%u subtype=%u ndless_revision=%u\n",nl_hwtype(),nl_hwsubtype(),nl_ndless_rev());
    performance_clock_debug(f);
    dump(f,"primary_header",trace.header,trace.received<sizeof(trace.header)?trace.received:sizeof(trace.header));
    fprintf(f,"native_comparison completed=%u attempted=%u opened=%d received=%lu seek_end=%d size=%ld close=%d\n",
        trace.compared,trace.compare_attempted,trace.opened,(unsigned long)trace.native_bytes,
        trace.seek_result,trace.native_size,trace.close_result);
    dump(f,"native_header",trace.native_header,trace.native_bytes<sizeof(trace.native_header)?(unsigned)trace.native_bytes:sizeof(trace.native_header));
    fprintf(f,"storage_capture=%u\n",trace.captured);
    if(trace.captured) {
        const HeaderStorage *d=&trace.storage;
        fprintf(f,"asic=%08lx control=%08lx ttbr=%08lx dacr=%08lx kind=%lu stage=%lu reader_ready=%lu view_reused=%lu\n",
            (unsigned long)d->asic,(unsigned long)d->control,(unsigned long)d->ttbr,(unsigned long)d->dacr,
            (unsigned long)d->kind,(unsigned long)d->stage,(unsigned long)d->ready,(unsigned long)d->reused);
        fprintf(f,"snapshot status=%d fault=%08lx file_bytes=%llu inode=%lu blocks=%lu index=%lu dirty=%lu clean=%lu regions=%lu\n",
            d->snapshot_status,(unsigned long)d->snapshot_fault,(unsigned long long)d->file_bytes,(unsigned long)d->inode,
            (unsigned long)d->blocks,(unsigned long)d->index,(unsigned long)d->dirty,(unsigned long)d->clean,(unsigned long)d->regions);
        fprintf(f,"raw result=%ld status=%lu error=%lu (%s) rel_error=%lu modes=%lu/%lu requested=%lu copied=%lu\n",
            (long)(int32_t)d->result,(unsigned long)d->raw_status,(unsigned long)d->raw_error,raw_reason(d->raw_error),
            (unsigned long)d->rel_error,(unsigned long)d->file_mode,(unsigned long)d->index_mode,
            (unsigned long)d->rel_requested,(unsigned long)d->rel_copied);
        fprintf(f,"region id=%lu phase=%lu error=%lu cursor=%lu count=%lu slot=%lu page=%lu pending=%lu request=%08lx/%lu/%lu\n",
            (unsigned long)d->region_id,(unsigned long)d->region_phase,(unsigned long)d->region_error,(unsigned long)d->cursor,
            (unsigned long)d->count,(unsigned long)d->slot,(unsigned long)d->page,(unsigned long)d->region_pending,
            (unsigned long)d->request_offset,(unsigned long)d->request_column,(unsigned long)d->request_bytes);
        fprintf(f,"physical=%lu source=%lu media=%08lx raw_page=%lu column=%lu bytes=%lu address_status=%lu remap_index=%lu token=%llu\n",
            (unsigned long)d->physical,(unsigned long)d->source,(unsigned long)d->media_offset,(unsigned long)d->raw_page,
            (unsigned long)d->column,(unsigned long)d->bytes,(unsigned long)d->address_status,(unsigned long)d->address_index,(unsigned long long)d->token);
        fprintf(f,"nand status=%lu error=%lu quiescent=%lu corrected=%lu chip=%08lx spi_phase=%lu status=%08lx error=%lu features=%02lx/%02lx\n",
            (unsigned long)d->nand_status,(unsigned long)d->nand_error,(unsigned long)d->nand_quiescent,(unsigned long)d->nand_corrected,
            (unsigned long)d->chip_id,(unsigned long)d->spi_phase,(unsigned long)d->spi_status,(unsigned long)d->spi_error,
            (unsigned long)d->spi_features_a,(unsigned long)d->spi_features_b);
        fprintf(f,"layout device_bytes=%lu media_bytes=%lu base=%08lx remaps=%lu tag_column=%lu\n",
            (unsigned long)d->device_bytes,(unsigned long)d->media_bytes,(unsigned long)d->physical_base,
            (unsigned long)d->remap_count,(unsigned long)d->tag_column);
        for(unsigned i=0;i<d->count && i<21;++i)fprintf(f,"unit %u number=%u sequence=%lu\n",i,d->units[i],(unsigned long)d->sequences[i]);
        dump(f,"native_file_metadata",d->meta,sizeof(d->meta));
        dump(f,"FlashFX_state",d->state,sizeof(d->state));
        dump(f,"last_physical_read",d->io,d->io_bytes);
    }
}
#endif
