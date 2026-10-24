/* Execute generated CP_DMA packets against guarded, padded pixel arrays. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "rdn_cp_dma_readback.h"
static unsigned seed=7139;
static unsigned next(void){seed=seed*1664525u+1013904223u;return seed;}
int main(void)
{
    const uint64_t source=0x40000124ull,destination=0x123001000ull;
    unsigned cases=0;
    for(unsigned trial=0;trial<3000;++trial){
        unsigned width=1+next()%193,height=1+next()%97;
        unsigned ss=(width+next()%37)*4,ds=(width+next()%41)*4;
        size_t sn=(size_t)ss*height,dn=(size_t)ds*height;
        uint8_t *src=malloc(sn),*dst=malloc(dn+64),*expected=malloc(dn+64);
        uint32_t *ib=malloc((height*6+12)*4),words=0;
        assert(src&&dst&&expected&&ib);
        for(size_t i=0;i<sn;++i)src[i]=(uint8_t)next();
        memset(dst,0xa5,dn+64);memcpy(expected,dst,dn+64);
        for(unsigned y=0;y<height;++y)memcpy(expected+32+y*ds,src+y*ss,width*4);
        assert(!rdn_cp_dma_surface_build(source,ss,destination,ds,width,height,ib,(height*6+12)*4,&words));
        assert(ib[0]==0xc0034300u && ib[1]==0x1980003cu);
        unsigned at=5,copies=0;
        while(at<words && ib[at]==0xc0044100u){
            uint64_t a=ib[at+1]|((uint64_t)(ib[at+2]&0xff)<<32);
            uint64_t b=ib[at+3]|((uint64_t)ib[at+4]<<32);unsigned n=ib[at+5];
            assert(a>=source&&a-source+n<=sn&&b>=destination&&b-destination+n<=dn);
            assert(!(ib[at+2]&0x7fffff00u));
            memcpy(dst+32+b-destination,src+a-source,n);++copies;at+=6;
            assert(!!(ib[at-4]&0x80000000u)==(ib[at]!=0xc0044100u));
        }
        assert(copies && ib[at]==0xc0034300u && ib[at+1]==0x1980003cu);
        assert(ib[at+5]==0xc0004200u&&ib[at+6]==0&&at+7==words);
        assert(!memcmp(dst,expected,dn+64));
        free(src);free(dst);free(expected);free(ib);++cases;
    }
    uint32_t out[64],before[64],n;
    for(unsigned test=0;test<9;++test){
        memset(out,0x5a,sizeof(out));memcpy(before,out,sizeof(out));n=0x13579;
        uint64_t a=source,b=destination;unsigned w=4,h=3,ss=32,ds=48;size_t cap=sizeof(out);
        switch(test){case 0:w=0;break;case 1:h=0;break;case 2:w=16385;break;case 3:ss=12;break;
        case 4:ds=17;break;case 5:++a;break;case 6:b=a+4;break;case 7:a=(1ull<<40)-8;break;case 8:cap=4;break;}
        assert(rdn_cp_dma_surface_build(a,ss,b,ds,w,h,out,cap,&n));
        assert(n==0x13579&&!memcmp(out,before,sizeof(out)));++cases;
    }
    unsigned height=16384;uint32_t *ib=malloc((height*6+12)*4);
    assert(!rdn_cp_dma_surface_build(source,65536,destination,65536,16384,height,ib,(height*6+12)*4,&n));
    assert(n==height*6+12);free(ib);++cases;
    printf("%u strided GPU-copy cases passed\n",cases);return 0;
}
