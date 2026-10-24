"""Exercise registry growth and GLD retirement using the production functions."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


def run(source, cpp=False):
    with tempfile.TemporaryDirectory() as directory:
        path = pathlib.Path(directory)
        src = path / ('test.cpp' if cpp else 'test.c')
        src.write_text(source)
        exe = path / 'test'
        subprocess.run(['c++' if cpp else 'cc', '-std=c++11' if cpp else '-std=c99',
                        '-Wall', '-Wextra', '-Wno-sign-compare', '-I', str(ROOT / 'hw'),
                        str(src), '-pthread', '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


class SurfaceRegistryTest(unittest.TestCase):
    def test_kernel_growth_and_allocation_failure(self):
        source = (ROOT / 'kext/RadeonNI/RadeonNIAccel.cpp').read_text()
        header = (ROOT / 'kext/RadeonNI/RadeonNIAccel.h').read_text()
        record = header[header.index('\tstruct SurfaceMirror {'):header.index('\n\tUInt32 fReadLocked;')]
        harness = r'''
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include "rdn_user.h"
typedef uint32_t UInt32;
typedef void *task_t;
struct IOAccelBounds { int16_t x,y,w,h; };
struct IOAccelDeviceRegion { unsigned num_rects; IOAccelBounds bounds,rect[1]; };
static bool fail_alloc;
static void *IOMalloc(size_t bytes) { return fail_alloc ? NULL : malloc(bytes); }
static void IOFree(void *p,size_t) { free(p); }
static void IOLockLock(int *p) { assert(!*p); *p=1; }
static void IOLockUnlock(int *p) { assert(*p); *p=0; }
class RadeonNISurfaceClient {
public: unsigned references=1;
 void retain(){++references;} void release(){assert(references>1);--references;}
};
class RadeonNIAccel {
public:
 int lock=0; int *fLock=&lock;
 void querySurface(UInt32,rdn_user_surface_info *);
 bool setSurfaceBuffer(UInt32,UInt32,UInt32,UInt32,UInt32,void *,task_t=0);
 bool setSurfaceRegion(UInt32,const IOAccelDeviceRegion *,UInt32);
 bool setSurfaceBacking(UInt32,task_t,const rdn_user_backing *);
 void forgetSurface(UInt32);
 void setSurfaceBackingOwner(UInt32,RadeonNISurfaceClient*);
 void clearSurfaceBackingOwner(UInt32,RadeonNISurfaceClient*);
 RadeonNISurfaceClient *retainSurfaceBacking(UInt32,task_t,bool);
'''
        harness += record + '\n};\n'
        for signature in ('int RadeonNIAccel::surfaceSlot(', 'void RadeonNIAccel::querySurface(',
                          'bool RadeonNIAccel::setSurfaceBuffer(', 'bool RadeonNIAccel::setSurfaceRegion(',
                          'bool RadeonNIAccel::setSurfaceBacking(', 'void RadeonNIAccel::forgetSurface(',
                          'void RadeonNIAccel::setSurfaceBackingOwner(',
                          'void RadeonNIAccel::clearSurfaceBackingOwner(',
                          'RadeonNISurfaceClient *RadeonNIAccel::retainSurfaceBacking('):
            harness += function(source, signature) + '\n'
        harness += r'''
int main() {
 RadeonNIAccel a{}; a.fShapes=NULL; a.fShapeCapacity=0;
 IOAccelDeviceRegion r={1,{0,0,16,16},{{0,0,16,16}}};
 rdn_user_backing b={4096,0,64,16,16};
 rdn_user_surface_info info;
 fail_alloc=true;
 assert(!a.setSurfaceRegion(1,&r,1));
 assert(!a.setSurfaceBacking(1,NULL,&b));
 assert(!a.setSurfaceBuffer(1,0,0,0,0,NULL));
 assert(!a.fShapes && !a.fShapeCapacity);
 fail_alloc=false;
 for(unsigned i=1;i<=32;i++) assert(a.setSurfaceBuffer(i,i*4096,64,16,16,&a));
 fail_alloc=true;
 assert(!a.setSurfaceBuffer(33,33*4096,64,16,16,&a));
 for(unsigned i=1;i<=32;i++) {
   a.querySurface(i,&info); assert(info.id==i && info.offset==i*4096 && info.width==16);
 }
 a.forgetSurface(7);
 assert(a.setSurfaceBuffer(33,33*4096,64,16,16,&a));
 assert(a.fShapeCapacity==32);
 fail_alloc=false;
 for(unsigned i=34;i<=256;i++) assert(a.setSurfaceBuffer(i,i*4096,64,16,16,&a));
 for(unsigned i=1;i<=256;i++) {
   a.querySurface(i,&info);
   if(i==7) { assert(!info.id); continue; }
   assert(info.id==i && info.offset==i*4096 && info.row_bytes==64 && info.height==16);
 }
 assert(a.setSurfaceBuffer(300,0,0,0,0,&a));
 a.querySurface(300,&info); assert(info.id==300 && !info.width);
 assert(a.setSurfaceRegion(300,&r,1));
 assert(a.setSurfaceBacking(300,NULL,&b));
 a.querySurface(300,&info); assert(info.id==300);
 RadeonNISurfaceClient producer,other;
 assert(a.setSurfaceBacking(300,(task_t)0x100,&b));a.setSurfaceBackingOwner(300,&producer);
 assert(!a.retainSurfaceBacking(300,(task_t)0x200,false));
 auto lease=a.retainSurfaceBacking(300,(task_t)0x100,false);assert(lease==&producer&&producer.references==2);lease->release();
 lease=a.retainSurfaceBacking(300,(task_t)0x200,true);assert(lease==&producer);lease->release();
 assert(a.setSurfaceBuffer(300,0x10000,64,16,16,&a,(task_t)0x200));
 lease=a.retainSurfaceBacking(300,(task_t)0x200,false);assert(lease==&producer);lease->release();
 assert(!a.retainSurfaceBacking(300,(task_t)0x300,false));
 a.forgetSurface(300);a.querySurface(300,&info);assert(info.id==300);
 a.clearSurfaceBackingOwner(300,&other);assert(a.retainSurfaceBacking(300,(task_t)0x100,false)==&producer);producer.release();
 a.clearSurfaceBackingOwner(300,&producer);assert(!a.retainSurfaceBacking(300,(task_t)0x100,true));
 a.forgetSurface(300); a.querySurface(300,&info); assert(!info.id);
 free(a.fShapes);
}
'''
        run(harness, cpp=True)

    def test_gld_retains_live_storage_on_query_and_publish_failure(self):
        source = (ROOT / 'gld/rdn_mesa.c').read_text()
        start = source.index('struct surface_store {')
        end = source.index('static pthread_mutex_t surface_store_lock', start)
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
struct context { uint32_t surface,stored,store_new,store_offset,store_row_bytes,store_width,store_height; };
static pthread_mutex_t surface_store_lock=PTHREAD_MUTEX_INITIALIZER;
static bool present[1024], query_fail, publish_fail, alloc_fail;
static unsigned allocated, freed;
static bool rdn_target_surface_query(uint32_t id,uint32_t info[5],bool *found) {
 (void)info; *found=present[id]; return !query_fail;
}
static bool rdn_target_surface_buffer(uint32_t id,uint32_t off,uint32_t stride,uint32_t w,uint32_t h) {
 (void)off; (void)stride; (void)w; (void)h;
 if(publish_fail) return false;
 present[id]=true; return true;
}
static bool rdn_target_vram_alloc(uint32_t bytes,uint32_t *off) {
 assert(bytes && !(bytes&4095));
 if(alloc_fail) return false;
 *off=++allocated*4096; return true;
}
static void rdn_target_vram_free(uint32_t off) { assert(off); freed++; }
'''
        harness += source[start:end]
        harness += function(source, 'static void clear_surface_store(') + '\n'
        harness += function(source, 'static void surface_store(') + '\n'
        harness += r'''
int main(void) {
 struct context c={0};
 for(unsigned i=1;i<=128;i++) {
   c.surface=i; surface_store(&c,16,16,1); assert(c.stored && c.store_new);
 }
 assert(allocated==128 && !freed);
 query_fail=true; present[1]=false;
 c.surface=129; surface_store(&c,16,16,1); assert(c.stored && !freed);
 query_fail=false;
 c.surface=129; surface_store(&c,16,16,1); assert(c.stored && !c.store_new && freed==1);
 publish_fail=true;
 surface_store(&c,16,16,1); assert(!c.stored && freed==1);
 surface_store(&c,32,16,1); assert(!c.stored && freed==1);
 publish_fail=false;
 surface_store(&c,16,16,1); assert(c.stored && !c.store_new && allocated==129);
 surface_store(&c,32,16,1); assert(c.stored && c.store_new && freed==2 && allocated==130);
 publish_fail=true;
 c.surface=130; surface_store(&c,16,16,1); assert(!c.stored && freed==3 && allocated==131);
 publish_fail=false; alloc_fail=true;
 surface_store(&c,16,16,1); assert(!c.stored && freed==3);
 alloc_fail=false; surface_store(&c,16,16,1); assert(c.stored);
 while(surface_stores) {
   struct surface_store *next=surface_stores->next; free(surface_stores); surface_stores=next;
 }
}
'''
        run(harness)


if __name__ == '__main__':
    unittest.main()
