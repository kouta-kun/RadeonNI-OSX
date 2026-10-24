/* Verify CPU/GPU ownership of a packed compositor backing surface. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <mach-o/nlist.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/graphics/IOAccelSurfaceConnect.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include "rdn_user.h"

static void *driver_symbol(const char *name)
{
    unsigned i;
    for (i = 0; i < _dyld_image_count(); ++i) {
        const char *path = _dyld_get_image_name(i);
        if (!strstr(path, "RadeonNIGLDriver")) continue;
        struct nlist symbols[2];
        memset(symbols, 0, sizeof(symbols));
        symbols[0].n_un.n_name = (char *)name;
        if (!nlist(path, symbols) && symbols[0].n_value)
            return (void *)(symbols[0].n_value + _dyld_get_image_vmaddr_slide(i));
    }
    return NULL;
}
#define CHECK(x) do { unsigned err = (x); if (err) { printf("line %d: 0x%x\n", __LINE__, err); return 1; } } while (0)
int main(void)
{
    const unsigned id = 0x7e000000U;
    const unsigned w = 65, h = 33, stride = 288;
    io_service_t svc = IOServiceGetMatchingService(kIOMasterPortDefault,
        IOServiceMatching(RDN_UC_SERVICE_CLASS));
    io_connect_t device, surface;
    CGLPixelFormatAttribute attrs[] = {kCGLPFARendererID, 0x21a00, 0};
    CGLPixelFormatObj pf; CGLContextObj ctx; GLint n;
    unsigned char *cpu = calloc(h, stride), rgba[4];
    struct {IOAccelDeviceRegion region; IOAccelBounds rect;} shape;
    IOAccelSurfaceInformation info;
    unsigned i, y, failures = 0;
    IOByteCount size;
    setbuf(stdout, NULL);
    if (!svc || !cpu) return 2;
    CHECK(IOServiceOpen(svc, mach_task_self(), RDN_UC_TYPE, &device));
    CHECK(IOConnectMethodScalarIScalarO(device,RDN_UC_SURFACE_BUFFER,5,0,id,0,0,0,0));
    CHECK(IOServiceOpen(svc, mach_task_self(), 0, &surface));
    CHECK(IOConnectMethodScalarIScalarO(surface,kIOAccelSurfaceSetIDMode,2,0,id,0x24));
    memset(&shape,0,sizeof(shape)); shape.region.num_rects=1;
    shape.region.bounds.w=w;shape.region.bounds.h=h;shape.rect=shape.region.bounds;
    for(y=0;y<h;++y)for(i=0;i<w;++i) {
        cpu[y*stride+i*4]=128;cpu[y*stride+i*4+1]=0;
        cpu[y*stride+i*4+2]=255;cpu[y*stride+i*4+3]=0;
    }
    CHECK(IOConnectMethodScalarIStructureI(surface,kIOAccelSurfaceSetShapeBackingAndLength,
        5,sizeof(shape),4,0,(unsigned)cpu,stride,h*stride,&shape));
    failures += IOConnectMethodScalarIStructureI(surface,kIOAccelSurfaceSetShapeBackingAndLength,
        5,sizeof(shape),4,0,(unsigned)cpu,stride,h*stride-1,&shape)==0;
    CHECK(CGLChoosePixelFormat(attrs,&pf,&n));CHECK(CGLCreateContext(pf,NULL,&ctx));
    CHECK(CGLSetCurrentContext(ctx));printf("renderer %s\n",glGetString(GL_RENDERER));
    int (*import_cpu)(void *,int,int *,int *)=driver_symbol("_rdn_mesa_cgls_set_integer");
    GLuint cpu_texture;glGenTextures(1,&cpu_texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,cpu_texture);
    int cpu_values[]={id,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA,w,h},cpu_result=-1;
    if(!import_cpu || !import_cpu(ctx,999,cpu_values,&cpu_result) || cpu_result) {
        printf("CPU-only backing import failed: 0x%x\n",cpu_result);return 12;
    }
    unsigned char *cpu_image=calloc(w*h,4);
    glGetTexImage(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA,GL_UNSIGNED_BYTE,cpu_image);
    for(i=0;i<w*h;++i)
        failures+=cpu_image[i*4]!=0 || cpu_image[i*4+1]!=255 ||
                  cpu_image[i*4+2]!=0 || cpu_image[i*4+3]!=128;
    printf("CPU-only backing import: %u failures\n",failures);
    free(cpu_image);glDeleteTextures(1,&cpu_texture);
    int (*attach)(void *,unsigned long)=driver_symbol("_rdn_mesa_attach_screen");
    void **contexts=driver_symbol("_contexts");
    if (!attach || !contexts || !*contexts) return 3;
    int (*attach_surface)(void *,unsigned long,unsigned long,unsigned long)=
        driver_symbol("_rdn_mesa_attach_surface");
    if (!attach_surface || !attach_surface(*contexts,0,0,id)) return 4;
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    printf("initial CPU -> GPU: %u,%u,%u,%u\n",rgba[0],rgba[1],rgba[2],rgba[3]);
    failures += rgba[0]!=0 || rgba[1]!=255 || rgba[2]!=0 || rgba[3]!=128;
    glClearColor(1,0,0,0.25);glClear(GL_COLOR_BUFFER_BIT);glFinish();
    printf("direct backing after finish: %u,%u,%u,%u\n",cpu[0],cpu[1],cpu[2],cpu[3]);
    failures += cpu[0]!=128 || cpu[1]!=0 || cpu[2]!=255 || cpu[3]!=0;
    size=sizeof(info);
    CHECK(IOConnectMethodScalarIStructureO(surface,kIOAccelSurfaceReadLockOptions,1,&size,0,&info));
    printf("GPU -> CPU: %u,%u,%u,%u stride %u\n",cpu[0],cpu[1],cpu[2],cpu[3],(unsigned)info.rowBytes);
    for(y=0;y<h;++y)for(i=0;i<w;++i)
        failures += cpu[y*stride+i*4]!=64 || cpu[y*stride+i*4+1]!=255 ||
                    cpu[y*stride+i*4+2]!=0 || cpu[y*stride+i*4+3]!=0;
    CHECK(IOConnectMethodScalarIScalarO(surface,kIOAccelSurfaceReadUnlockOptions,1,0,0));
    size=sizeof(info);
    CHECK(IOConnectMethodScalarIStructureO(surface,kIOAccelSurfaceWriteLockOptions,1,&size,0,&info));
    for(y=0;y<h;++y)for(i=0;i<w;++i) {
        cpu[y*stride+i*4]=192;cpu[y*stride+i*4+1]=0;
        cpu[y*stride+i*4+2]=0;cpu[y*stride+i*4+3]=255;
    }
    CHECK(IOConnectMethodScalarIScalarO(surface,kIOAccelSurfaceWriteUnlockOptions,1,0,0));
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    printf("CPU edit -> GPU: %u,%u,%u,%u GL error %x\n",rgba[0],rgba[1],rgba[2],rgba[3],glGetError());
    failures += rgba[0]!=0 || rgba[1]!=0 || rgba[2]!=255 || rgba[3]!=192;
    CGLContextObj consumer;
    CHECK(CGLCreateContext(pf,NULL,&consumer));CHECK(CGLSetCurrentContext(consumer));
    glGetString(GL_RENDERER);
    GLuint texture;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    int (*import)(void *,int,int *,int *)=driver_symbol("_rdn_mesa_cgls_set_integer");
    int values[]={id,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA,w,h,GL_UNSIGNED_INT_8_8_8_8_REV,0,0};
    int result=-1;
    if (!import || !import(consumer,999,values,&result) || result) return 5;
    unsigned char *image=calloc(w*h,4);
    glGetTexImage(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA,GL_UNSIGNED_BYTE,image);
    printf("imported alpha: %u,%u,%u,%u\n",image[0],image[1],image[2],image[3]);
    failures += image[0]!=0 || image[1]!=0 || image[2]!=255 || image[3]!=192;
    values[2]=GL_RGB8;
    if (!import(consumer,999,values,&result) || result) return 7;
    glGetTexImage(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA,GL_UNSIGNED_BYTE,image);
    printf("RGB import alpha: %u\n",image[3]);
    failures += image[3]!=255;
    values[2]=GL_RGBA;
    if (!import(consumer,999,values,&result) || result) return 8;
    CHECK(CGLSetCurrentContext(ctx));
    shape.region.bounds.w=32;shape.region.bounds.h=24;shape.rect=shape.region.bounds;
    CHECK(IOConnectMethodScalarIStructureI(surface,kIOAccelSurfaceSetShapeBackingAndLength,
        5,sizeof(shape),4,0,(unsigned)cpu,stride,h*stride,&shape));
    if (!attach(*contexts,id)) return 6;
    glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);glFinish();
    size=sizeof(info);
    CHECK(IOConnectMethodScalarIStructureO(surface,kIOAccelSurfaceReadLockOptions,1,&size,0,&info));
    failures += info.width!=32 || info.height!=24 || cpu[0]!=255 || cpu[2]!=255;
    CHECK(IOConnectMethodScalarIScalarO(surface,kIOAccelSurfaceReadUnlockOptions,1,0,0));
    CHECK(CGLSetCurrentContext(consumer));
    memset(image,0,w*h*4);
    glGetTexImage(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA,GL_UNSIGNED_BYTE,image);
    printf("import retained across resize: %u,%u,%u,%u GL error %x\n",image[0],image[1],image[2],image[3],glGetError());
    failures += image[0]!=0 || image[1]!=0 || image[2]!=255 || image[3]!=192;
    void *(*get_context)(void)=driver_symbol("_OSMesaGetCurrentContext");
    int (*adopt)(void *)=driver_symbol("_OSMesaAdoptContext");
    void *(*get_proc)(const char *)=driver_symbol("_OSMesaGetProcAddress");
    if (!get_context || !adopt || !get_proc) return 9;
    void *consumer_mesa=get_context();
    CHECK(CGLSetCurrentContext(ctx));glGetString(GL_RENDERER);
    void *producer_mesa=get_context();
    CHECK(CGLSetCurrentContext(consumer));
    void (*clear_color)(float,float,float,float)=get_proc("glClearColor");
    void (*clear)(unsigned)=get_proc("glClear");
    void (*bind)(unsigned,unsigned)=get_proc("glBindTexture");
    void (*read_image)(unsigned,int,unsigned,unsigned,void *)=get_proc("glGetTexImage");
    unsigned unpublished=0;
    values[3]=32;values[4]=24;
    for (int frame=0;frame<32;++frame) {
        adopt(producer_mesa);
        clear_color(frame&1,!(frame&1),0,0.5);clear(GL_COLOR_BUFFER_BIT);
        adopt(consumer_mesa);bind(GL_TEXTURE_RECTANGLE_ARB,texture);
        if (!import(consumer,999,values,&result) || result) return 10;
        read_image(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA,GL_UNSIGNED_BYTE,image);
        for (i=0;i<32*24;++i)
            unpublished += image[i*4]!=((frame&1)?255:0) ||
                           image[i*4+1]!=((frame&1)?0:255) ||
                           image[i*4+2]!=0 || image[i*4+3]!=128;
    }
    printf("backing imports without producer flush: %u mismatched pixels\n",unpublished);
    failures += unpublished;
    CHECK(CGLSetCurrentContext(ctx));glGetString(GL_RENDERER);
    int *window_server=driver_symbol("_rdn_window_server");
    if (!window_server) return 11;
    int was_window_server=*window_server;
    *window_server=1;glViewport(0,0,32,18);*window_server=was_window_server;
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);glFinish();
    size=sizeof(info);
    CHECK(IOConnectMethodScalarIStructureO(surface,kIOAccelSurfaceReadLockOptions,1,&size,0,&info));
    CHECK(IOConnectMethodScalarIScalarO(surface,kIOAccelSurfaceReadUnlockOptions,1,0,0));
    printf("unpadded backing: first row %u,%u,%u,%u; padding alpha %u\n",
           cpu[0],cpu[1],cpu[2],cpu[3],cpu[18*stride]);
    failures += cpu[0]!=255 || cpu[1]!=0 || cpu[2]!=0 || cpu[3]!=255 ||
                cpu[18*stride]!=128;
    io_connect_t shared_surface;
    CHECK(IOConnectMethodScalarIScalarO(device,RDN_UC_SURFACE_BUFFER,5,0,id+1,0,0,0,0));
    CHECK(IOServiceOpen(svc,mach_task_self(),0,&shared_surface));
    CHECK(IOConnectMethodScalarIScalarO(shared_surface,kIOAccelSurfaceSetIDMode,2,0,id+1,0x24));
    shape.region.bounds.w=32;shape.region.bounds.h=24;shape.rect=shape.region.bounds;
    CHECK(IOConnectMethodScalarIStructureI(shared_surface,kIOAccelSurfaceSetShape,
        2,sizeof(shape),0,0,&shape));
    if(!attach_surface(*contexts,0,0,id+1)) return 13;
    glViewport(0,0,32,18);glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);glFinish();
    CHECK(CGLSetCurrentContext(consumer));
    values[0]=id+1;values[3]=32;values[4]=24;
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    if(!import(consumer,997,values,&result)||result) return 14;
    glGetTexImage(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA,GL_UNSIGNED_BYTE,image);
    unsigned windowless_failures=0;
    for(i=0;i<32*18;++i)
        windowless_failures+=image[i*4]!=0||image[i*4+1]!=255||image[i*4+2]!=0||image[i*4+3]!=255;
    printf("windowless shared surface: %u mismatched pixels\n",windowless_failures);
    failures+=windowless_failures;
    IOServiceClose(shared_surface);
    CHECK(CGLSetCurrentContext(consumer));
    free(image);glDeleteTextures(1,&texture);
    CGLSetCurrentContext(NULL);CGLDestroyContext(consumer);CGLDestroyContext(ctx);CGLDestroyPixelFormat(pf);
    IOServiceClose(surface);IOServiceClose(device);IOObjectRelease(svc);free(cpu);
    printf("backing surface: %u failures\n",failures);return failures!=0;
}
