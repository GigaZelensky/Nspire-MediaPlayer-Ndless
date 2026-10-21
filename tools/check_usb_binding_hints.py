"""Compare authenticated lookup hints with full discovery; optional raw OS/RAM carriers."""
from pathlib import Path
import os,shutil,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1]
body=(root/'src/player/native_usb_ui_resolver.c').read_text()
assert body.count('if(hinted_api(v,classic_cx,api))')==1
body=body.replace('if(hinted_api(v,classic_cx,api))','if(hints_enabled && hinted_api(v,classic_cx,api))')
fixture=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
static bool hints_enabled=true;
/* PRODUCTION_RESOLVER */
static unsigned char *data;static size_t bytes;
static unsigned reads,mapped_words;
static bool allow(void *p,uint32_t at,uint32_t n,unsigned kind)
{(void)p;return at>=0x10000000U && n<=bytes && at-0x10000000U<=bytes-n && (kind!=PORTABLE_CODE || at+n<=0x11000000U);}
static bool read_word(void *p,uint32_t at,uint32_t *w)
{if(!allow(p,at,4,PORTABLE_DATA))return false;memcpy(w,data+at-0x10000000U,4);++reads;return true;}
static const uint32_t *code_words(void *p,uint32_t at,uint32_t n)
{if(!allow(p,at,n,PORTABLE_CODE))return NULL;mapped_words+=n/4;return (const uint32_t *)(const void *)(data+at-0x10000000U);}
static unsigned resolve(const PortableView *v,NativeUsbUiApi *api,bool hints,int *status)
{hints_enabled=hints;unsigned turns=0;do{*status=native_usb_ui_resolve_step(v,false,api,4096);assert(++turns<2000);}while(!*status);return turns;}
static void check(const char *path)
{
 if(path){FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);bytes=ftell(f);rewind(f);assert(bytes>=0x1000000U && bytes<=0x4000000U);data=malloc(bytes);assert(data && fread(data,1,bytes,f)==bytes);fclose(f);}
 else {bytes=0x2000000U;data=calloc(1,bytes);assert(data);}
 unsigned char *before=malloc(bytes);assert(before);memcpy(before,data,bytes);
 PortableView v={NULL,allow,read_word,0x10000000U,0x11000000U,0x10000000U,0x10000000U+(uint32_t)bytes,code_words};
 NativeUsbUiApi fast={0},reference={0};int a,b;reads=mapped_words=0;
 unsigned fast_turns=resolve(&v,&fast,true,&a),fast_work=reads+mapped_words;
 reads=mapped_words=0;unsigned reference_turns=resolve(&v,&reference,false,&b),reference_work=reads+mapped_words;
 assert(a==b && !memcmp(&fast,&reference,offsetof(NativeUsbUiApi,classic_cx)) && fast.classic_cx==reference.classic_cx);
 assert(!memcmp(before,data,bytes));
 NativeUsbUiApi unchanged;memset(&unchanged,0x5a,sizeof(unchanged));NativeUsbUiApi sentinel=unchanged;
 assert(!hinted_api(&v,true,&unchanged) && !memcmp(&unchanged,&sentinel,sizeof(unchanged)));
 if(a==1){
  assert(fast_turns<reference_turns/4 && fast_work<reference_work/4);
  uint32_t saved;memcpy(&saved,data+0x69e4,4);memset(data+0x69e4,0,4);
  assert(!hinted_api(&v,false,&unchanged) && !memcmp(&unchanged,&sentinel,sizeof(unchanged)));
  memcpy(data+0x69e4,&saved,4);assert(!memcmp(before,data,bytes));
  /* Valid function hints do not authorize missing input ownership proofs.
   * Remove every matching setter, then verify the hinted scan falls back
   * once and reaches exactly the general resolver's refusal. */
  uint32_t center=branch(&v,fast.function[UI_CURSOR_SHOW]+0x198),changed[16];unsigned n=0;
  const uint32_t setter[]={0xe59f3004U,0xe1c300b0U,0xe12fff1eU};
  for(uint32_t at=center-0x10000U;at<center+0x10000U;at+=4)
   if(prefix(&v,at,setter,3) && literal(&v,at)==fast.event_mask){assert(n<16);changed[n++]=at;memset(data+at-0x10000000U,0,4);}
  assert(n);NativeUsbUiApi rejected_hint={0},rejected_scan={0};
  (void)resolve(&v,&rejected_hint,true,&a);(void)resolve(&v,&rejected_scan,false,&b);
  assert(a==-1 && b==-1);
  for(unsigned i=0;i<n;++i)memcpy(data+changed[i]-0x10000000U,&setter[0],4);
  assert(!memcmp(before,data,bytes));a=1;
 } else assert(!path && a==-1);
 printf("PASS %s: result=%d identical API; turns %u -> %u, inspected words/spans %u -> %u; rejected/corrupt hints publish nothing; no carrier writes\n",path?path:"unsupported synthetic firmware",a,reference_turns,fast_turns,reference_work,fast_work);
 free(before);free(data);
}
int main(int argc,char **argv){check(NULL);for(int i=1;i<argc;++i)check(argv[i]);return 0;}
'''
fixture=fixture.replace('/* PRODUCTION_RESOLVER */',body)
fixture=fixture.replace('#include <assert.h>','#include <stdbool.h>\n#include <assert.h>',1)
cc=os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
with tempfile.TemporaryDirectory(prefix='nmedia-usb-hints-') as tmp:
    c=Path(tmp)/'test.c';c.write_text(fixture);exe=Path(tmp)/'test.exe'
    subprocess.run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'src/player'),str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe),*sys.argv[1:]],check=True,timeout=60)
