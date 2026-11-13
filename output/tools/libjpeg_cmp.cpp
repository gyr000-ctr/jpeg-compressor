#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <setjmp.h>
#include <jpeglib.h>
#include "jpgd.h"
using namespace jpgd;
static unsigned char* rd(const char*p,size_t*n){FILE*f=fopen(p,"rb");fseek(f,0,2);long z=ftell(f);fseek(f,0,0);auto b=(unsigned char*)malloc(z);if(fread(b,1,z,f)!=z){}fclose(f);*n=z;return b;}
struct S{jpeg_source_mgr pub;const unsigned char*d;size_t n;int e;};
static void fi(j_decompress_ptr){}
static boolean ff(j_decompress_ptr c){auto m=(S*)c->src;if(m->e){static unsigned char eoi[2]={0xFF,JPEG_EOI};m->pub.next_input_byte=eoi;m->pub.bytes_in_buffer=2;}else{m->pub.next_input_byte=m->d;m->pub.bytes_in_buffer=m->n;m->e=1;}return TRUE;}
static void sk(j_decompress_ptr c,long num){auto m=(S*)c->src;while(num>0){if(!m->pub.bytes_in_buffer)ff(c);size_t s=m->pub.bytes_in_buffer;if((long)s>num)s=num;m->pub.next_input_byte+=s;m->pub.bytes_in_buffer-=s;num-=s;}}
static void tm(j_decompress_ptr){}
struct E{jpeg_error_mgr pub;jmp_buf j;};
static void ee(j_common_ptr c){longjmp(((E*)c->err)->j,1);}
static unsigned char* lj(const unsigned char*d,size_t n,int*w,int*h){
  jpeg_decompress_struct c;E e;c.err=jpeg_std_error(&e.pub);e.pub.error_exit=ee;if(setjmp(e.j)){jpeg_destroy_decompress(&c);return NULL;}
  jpeg_create_decompress(&c);auto m=(S*)(*c.mem->alloc_small)((j_common_ptr)&c,JPOOL_PERMANENT,sizeof(S));
  m->d=d;m->n=n;m->e=0;m->pub.init_source=fi;m->pub.fill_input_buffer=ff;m->pub.skip_input_data=sk;m->pub.resync_to_restart=jpeg_resync_to_restart;m->pub.term_source=tm;m->pub.bytes_in_buffer=0;m->pub.next_input_byte=NULL;c.src=&m->pub;
  jpeg_read_header(&c,TRUE);c.out_color_space=c.num_components==1?JCS_GRAYSCALE:JCS_RGB;jpeg_start_decompress(&c);
  *w=c.output_width;*h=c.output_height;int cc=c.output_components;auto o=(unsigned char*)malloc((size_t)*w**h*cc);
  while(c.output_scanline<c.output_height){JSAMPROW r=o+(size_t)c.output_scanline**w*cc;jpeg_read_scanlines(&c,&r,1);}
  jpeg_finish_decompress(&c);jpeg_destroy_decompress(&c);return o;
}
int main(int argc,char**argv){
  size_t n;auto d=rd(argv[1],&n);int w,h,ac;
  auto jd=decompress_jpeg_image_from_memory(d,n,&w,&h,&ac,3);
  int rw,rh;auto ref=lj(d,n,&rw,&rh);
  if(!jd||!ref){printf("null jd=%p ref=%p\n",(void*)jd,(void*)ref);return 2;}
  long diffs=0,maxd=0,hist[8]={0};size_t N=(size_t)w*h*3;
  for(size_t i=0;i<N;i++){long x=jd[i]>ref[i]?jd[i]-ref[i]:ref[i]-jd[i];if(x){diffs++;if(x>maxd)maxd=x;if(x<7)hist[x]++;}}
  printf("%-40s diff=%6ld/%zu (%.2f%%) max=%ld  hist[1..6]=%ld,%ld,%ld,%ld,%ld,%ld\n",
    argv[1],diffs,N,100.0*diffs/N,maxd,hist[1],hist[2],hist[3],hist[4],hist[5],hist[6]);
  return 0;
}
