#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "jpgd.h"
using namespace jpgd;
static unsigned char* rd(const char*p,size_t*n){FILE*f=fopen(p,"rb");fseek(f,0,2);long z=ftell(f);fseek(f,0,0);auto b=(unsigned char*)malloc(z);fread(b,1,z,f);fclose(f);*n=z;return b;}
int main(int argc,char**argv){
    size_t an,hn; unsigned char*ad=rd(argv[1],&an),*hd=rd(argv[2],&hn);
    int aw,ah,ac,hw,hh,hc;
    unsigned char*a=decompress_jpeg_image_from_memory(ad,an,&aw,&ah,&ac,3);
    unsigned char*h=decompress_jpeg_image_from_memory(hd,hn,&hw,&hh,&hc,3);
    if(!a||!h){printf("decode null a=%p h=%p\n",(void*)a,(void*)h);return 2;}
    long diffs=0,maxd=0; long sum=0;
    size_t N=(size_t)aw*ah*3;
    for(size_t i=0;i<N;i++){int d=(int)a[i]-(int)h[i];if(d<0)d=-d;if(d){diffs++;sum+=d;if(d>maxd)maxd=d;}}
    printf("dims %dx%d  diff pixels=%ld/%zu  max=%ld meanabs=%.3f\n",aw,ah,diffs,N,maxd,diffs?(double)sum/diffs:0);
    // show first 12 differing pixels
    int shown=0;
    for(size_t i=0;i<N&&shown<12;i++)if(a[i]!=h[i]){printf("  idx=%zu (x=%zu y=%zu ch=%zu) arith=%u huff=%u\n",i,i%3,(i/3)%aw,(i/3)/aw,a[i],h[i]);shown++;}
    return 0;
}
