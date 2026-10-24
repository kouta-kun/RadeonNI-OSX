/* Separate producer and privileged compositor, with GPU-only pixel transfer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <mach/mach.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/graphics/IOAccelSurfaceConnect.h>
#include "rdn_user.h"
#define CHECK(x) do { kern_return_t e=(x);if(e){printf("line%d:0x%x\n",__LINE__,e);return 10;} }while(0)
static const unsigned id=0x7cffb881;
static kern_return_t info(io_connect_t c,struct rdn_user_surface_info *i){IOByteCount n=sizeof(*i);memset(i,0,n);return IOConnectMethodScalarIStructureO(c,RDN_UC_SURFACE_GPU_INFO,1,&n,id,i);}
static unsigned char pixel(unsigned frame,unsigned x,unsigned y,unsigned c){return (unsigned char)(frame*19+x*7+y*13+c*37);}
int main(void){
 setbuf(stdout,NULL);if(geteuid()!=0){puts("Run as root to exercise compositor authorization.");return 2;}
 io_service_t svc=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching(RDN_UC_SERVICE_CLASS));io_connect_t surface=0,dev=0;unsigned char *cpu=calloc(64,512);struct{IOAccelDeviceRegion region;IOAccelBounds rect;}shape;struct rdn_user_surface_info i;
 if(!svc||!cpu)return 3;CHECK(IOServiceOpen(svc,mach_task_self(),0,&surface));CHECK(IOServiceOpen(svc,mach_task_self(),RDN_UC_TYPE_V2,&dev));CHECK(IOConnectMethodScalarIScalarO(surface,kIOAccelSurfaceSetIDMode,2,0,id,0x24));
 int tochild[2],toparent[2];if(pipe(tochild)||pipe(toparent))return 4;pid_t child=fork();if(child<0)return 5;
 if(!child){
  close(tochild[1]);close(toparent[0]);io_service_t s=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching(RDN_UC_SERVICE_CLASS));io_connect_t c=0;CHECK(IOServiceOpen(s,mach_task_self(),RDN_UC_TYPE_V2,&c));int offset=0;CHECK(IOConnectMethodScalarIScalarO(c,RDN_UC_ALLOC,2,1,65536,4096,&offset));vm_address_t base=0;vm_size_t length=0;CHECK(IOConnectMapMemory(c,RDN_UC_MEMORY_APERTURE,mach_task_self(),&base,&length,kIOMapAnywhere));if((unsigned)offset+65536>length)return 6;unsigned char *dst=(unsigned char*)base+offset;
  unsigned mismatches=0;for(unsigned frame=1;frame<=16;++frame){unsigned char cmd;if(read(tochild[0],&cmd,1)!=1)return 7;CHECK(info(c,&i));unsigned w=frame<9?64:65,h=frame<9?32:33;if(i.id!=id||i.width!=w||i.height!=h||i.offset)return 8;if(frame==1||frame==9)memset(dst,0xa5,65536);__asm__ volatile("sync" ::: "memory");if(frame==1||frame==9)CHECK(IOConnectMethodScalarIScalarO(c,RDN_UC_SURFACE_GPU_COPY,5,0,id,offset,512,w,h));for(unsigned y=0;y<h;++y)for(unsigned x=0;x<512;++x){unsigned char expected=x<w*4?pixel(frame,x/4,y,x%4):0xa5;mismatches+=dst[y*512+x]!=expected;}if(mismatches){printf("frame %u: %u mismatched bytes\n",frame,mismatches);return 9;}unsigned char ok=1;if(write(toparent[1],&ok,1)!=1)return 11;}
  unsigned char cmd;if(read(tochild[0],&cmd,1)!=1)return 12;kern_return_t k=info(c,&i);if(k!=kIOReturnNotFound)return 13;
  for(unsigned y=0;y<33;++y)for(unsigned x=0;x<65*4;++x)mismatches+=dst[y*512+x]!=pixel(16,x/4,y,x%4);
  printf("GPU cross-process frames:16,imports:2,post-close retained pixels:%s,mismatches:%u\n",mismatches?"FAIL":"PASS",mismatches);
  CHECK(IOConnectUnmapMemory(c,0,mach_task_self(),base));CHECK(IOConnectMethodScalarIScalarO(c,RDN_UC_FREE,1,0,offset));IOServiceClose(c);IOObjectRelease(s);_exit(mismatches?14:0);
 }
 close(tochild[0]);close(toparent[1]);for(unsigned frame=1;frame<=16;++frame){unsigned w=frame<9?64:65,h=frame<9?32:33;for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)for(unsigned c=0;c<4;++c)cpu[y*512+x*4+c]=pixel(frame,x,y,c);__asm__ volatile("sync" ::: "memory");if(frame==1||frame==9){memset(&shape,0,sizeof(shape));shape.region.num_rects=1;shape.region.bounds.w=w;shape.region.bounds.h=h;shape.rect=shape.region.bounds;CHECK(IOConnectMethodScalarIStructureI(surface,kIOAccelSurfaceSetShapeBackingAndLength,5,sizeof(shape),7,0,(unsigned)cpu,512,64*512,&shape));}CHECK(IOConnectMethodScalarIScalarO(surface,kIOAccelSurfaceWriteUnlockOptions,1,0,0));unsigned char cmd=frame;if(write(tochild[1],&cmd,1)!=1)return 15;unsigned char ok;if(read(toparent[0],&ok,1)!=1)return 16;CHECK(info(dev,&i));}
 IOServiceClose(surface);IOServiceClose(dev);unsigned char done=0;if(write(tochild[1],&done,1)!=1)return 17;int status;waitpid(child,&status,0);free(cpu);IOObjectRelease(svc);printf("consumer_status=%d\n",status);return status?18:0;
}
