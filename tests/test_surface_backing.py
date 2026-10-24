"""Exercise the production surface client with bounded fake IOKit memory."""
import os
import pathlib
import re
import shlex
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

PREFIX = r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <vector>
#include <string>
#include "rdn_user.h"
using UInt8=uint8_t; using UInt32=uint32_t; using UInt64=uint64_t;
using IOReturn=int; using IOByteCount=size_t; using vm_address_t=uintptr_t;
using task_t=void*;
enum { kIOReturnSuccess,kIOReturnBadArgument,kIOReturnNotReady,kIOReturnUnsupported,
       kIOReturnNoMemory,kIOReturnNoResources,kIOReturnTimeout };
enum { PAGE_SIZE=4096,kIOMapAnywhere=1,kIOMapReadOnly=2,kIODirectionOutIn=3,
       kIOAccelSurfaceModeWindowedBit=0x100,kIOAccelSurfaceModeColorDepth8888=32,
       kIOAccelSurfaceShapeStaleBackingBit=4,kIOAccelSurfaceLockInBacking=8 };
enum { kIOAccelSurfaceReadLockOptions,kIOAccelSurfaceReadUnlockOptions,
       kIOAccelSurfaceGetState,kIOAccelSurfaceWriteLockOptions,
       kIOAccelSurfaceWriteUnlockOptions,kIOAccelSurfaceRead,
       kIOAccelSurfaceSetShapeBacking,kIOAccelSurfaceSetIDMode,kIOAccelSurfaceSetScale,
       kIOAccelSurfaceSetShape,kIOAccelSurfaceFlush,kIOAccelSurfaceQueryLock,
       kIOAccelSurfaceReadLock,kIOAccelSurfaceReadUnlock,kIOAccelSurfaceWriteLock,
       kIOAccelSurfaceWriteUnlock,kIOAccelSurfaceControl,
       kIOAccelSurfaceSetShapeBackingAndLength,kIOAccelNumSurfaceMethods };
struct IOAccelBounds { int16_t x,y,w,h; };
struct IOAccelDeviceRegion { UInt32 num_rects; IOAccelBounds bounds,rect[1]; };
struct IOAccelSurfaceInformation { uintptr_t address[4]; UInt32 rowBytes,width,height,pixelFormat; };
struct IOAccelSurfaceReadData {}; struct IOAccelSurfaceScaling {};
struct IOExternalMethod {}; struct OSSerialize {}; struct IOService {};
static int trueValue; static void *kOSBooleanTrue=&trueValue;
struct Calls {
 int waits=0,maps=0,releases=0,prepares=0,completes=0,copies=0,descriptors=0;
 int fail=0,lastFlags=0,locks=0,terminates=0,forgot=0,barriers=0,garts=0,retained=0;
 bool fence=false; std::vector<std::string> events;
} calls;
struct IOLock { bool held=false; };
static IOLock *IOLockAlloc() { return calls.fail==1 ? nullptr : new IOLock; }
static void IOLockFree(IOLock *p) { assert(!p->held); delete p; }
static void IOLockLock(IOLock *p) { assert(p&&!p->held); p->held=true; ++calls.locks; }
static void IOLockUnlock(IOLock *p) { assert(p&&p->held); p->held=false; --calls.locks; }
struct IOUserClient {
 bool initWithTask(task_t,void*,UInt32) { return calls.fail!=2; }
 void free() {} void terminate() { ++calls.terminates; }
};
static void IOLog(const char*,...) {}
#define OSDeclareDefaultStructors(x)
#define super IOUserClient
static uint8_t gpu[16384],ram[4096],replacement[4096];
struct IOMemoryMap {
 uint8_t *data;
 uintptr_t getVirtualAddress() { return reinterpret_cast<uintptr_t>(data); }
 void release() { ++calls.releases; calls.events.push_back("unmap"); delete this; }
};
struct IOMemoryDescriptor {
 uint8_t *data; uint64_t bytes; bool owned=false,prepared=false,aperture=false;
 uint64_t getLength() { return bytes; }
 static IOMemoryDescriptor *withAddress(vm_address_t address,IOByteCount bytes,int direction,task_t task) {
  assert(direction==kIODirectionOutIn && task==(task_t)0x123);
  if(calls.fail==3) return nullptr;
  assert((address==0x10000 || address==0x20000) && bytes<=4096);
  ++calls.descriptors;
  return new IOMemoryDescriptor{address==0x10000 ? ram:replacement,bytes,true,false,false};
 }
 IOReturn prepare() { ++calls.prepares; if(calls.fail==4)return kIOReturnNotReady; prepared=true; return 0; }
 void complete() { assert(prepared); prepared=false; ++calls.completes; calls.events.push_back("complete"); }
 void release() { assert(owned&&!prepared); --calls.descriptors; delete this; }
 IOMemoryMap *map(task_t=nullptr,uintptr_t=0,unsigned flags=0,UInt32 offset=0,UInt32 length=0) {
  if(aperture) assert(calls.fence);
  if(calls.fail==5 || (aperture&&calls.fail==6))return nullptr;
  assert(uint64_t(offset)+(length ? length:bytes)<=bytes);
  ++calls.maps;calls.lastFlags=flags;calls.events.push_back(aperture?"map_gpu":"map_ram");
  return new IOMemoryMap{data+offset};
 }
};
static void tracked_copy(const void *src,void *dst,size_t n) {
 assert(calls.fence);++calls.copies;calls.events.push_back("copy");memmove(dst,src,n);
}
#define bcopy tracked_copy
static uint64_t rdn_gart_addr(UInt32 page){return 0x40000000ull+uint64_t(page)*4096;}
class RadeonNIAccel {
public:
 bool gate=true,gl=false,backingEnabled=true,hasBuffer=true,hasAperture=true;
 UInt32 offset=128,stride=32,width=4,height=3,locked=0,flushes=0;
 IOReturn waitResult=0,flushResult=0;
 IOMemoryDescriptor aperture{gpu,sizeof(gpu),false,false,true};
 rdn_user_backing backing{};
 IOReturn gartBind(task_t,UInt64 address,UInt32,void*,UInt32 *offset){++calls.garts;*offset=address==0x10000?0:4096;return 0;}
 IOReturn gartUnbind(UInt32,void*){--calls.garts;return 0;}
 void retainClientMemory(const UInt32*,UInt32,void*){++calls.retained;calls.garts=0;}
 void setSurfaceBackingOwner(UInt32,void*){}
 void clearSurfaceBackingOwner(UInt32,void*){}
 UInt32 apertureBytes(){return sizeof(gpu);}
 IOReturn syncSurfaceGpu(UInt32,UInt64 ram,UInt32 rs,UInt32 w,UInt32 h,bool toCPU){
  if(!hasBuffer||width!=w||height!=h)return 0;
  if(stride<w*4||offset+(UInt64)stride*(h-1)+w*4>apertureBytes())return kIOReturnBadArgument;
  UInt64 v=surfaceVramAddress(offset);return surfaceGpuTransfer(toCPU?v:ram,toCPU?stride:rs,toCPU?ram:v,toCPU?rs:stride,w,h);
 }
 UInt64 surfaceVramAddress(UInt32 offset){return 0x200000000ull+offset;}
 IOReturn surfaceGpuTransfer(UInt64 source,UInt32 ss,UInt64 destination,UInt32 ds,UInt32 w,UInt32 h,UInt32 surface=0){
  IOReturn r=waitForCPU();if(r)return r;if(calls.fail==6)return kIOReturnNoMemory;
  if(surface&&hasBuffer&&width==w&&height==h){source=surfaceVramAddress(offset);ss=stride;}
  auto pointer=[&](UInt64 a){return a>=0x200000000ull?gpu+(a-0x200000000ull):a>=0x40001000ull?replacement+(a-0x40001000ull):ram+(a-0x40000000ull);};
  for(UInt32 y=0;y<h;++y)tracked_copy(pointer(source)+y*ss,pointer(destination)+y*ds,w*4);
  ++calls.barriers;return 0;
 }
 bool gpuEnabled=true;
 bool surfaceGpuReady(){return gpuEnabled;}
 bool surfaceGate(){return gate;}
 bool isGLSurface(UInt32){return gl;}
 void *getProperty(const char*){return backingEnabled?kOSBooleanTrue:nullptr;}
 bool getSurfaceBuffer(UInt32,UInt32 *o,UInt32 *s,UInt32 *w,UInt32 *h){
  if(!hasBuffer)return false;*o=offset;*s=stride;*w=width;*h=height;return true;
 }
 IOMemoryDescriptor *apertureMemory(){return hasAperture?&aperture:nullptr;}
 IOReturn waitForCPU(){++calls.waits;calls.events.push_back("wait");calls.fence=!waitResult;return waitResult;}
 void setReadLocked(UInt32 n){locked=n;}
 UInt32 readLocked(){return locked;}
 bool setSurfaceRegion(UInt32,const IOAccelDeviceRegion*,UInt32){return calls.fail!=8;}
 bool setSurfaceBacking(UInt32,task_t,const rdn_user_backing *b){if(calls.fail==7)return false;backing=*b;return true;}
 IOReturn flushSurface(UInt32){++flushes;return flushResult;}
 void forgetSurface(UInt32){++calls.forgot;backing={};}
};
'''

SUFFIX = r'''
void RadeonNISurfaceClient::note(UInt32,UInt32,UInt32,UInt32,UInt32) {}
void RadeonNISurfaceClient::noteRegion(UInt32,IOAccelDeviceRegion*,IOByteCount) {}
void RadeonNISurfaceClient::countControl(UInt32,UInt32) {}
void RadeonNISurfaceClient::countFlush(UInt32,UInt32) {}
void RadeonNISurfaceClient::publishCounts(bool) {}
static constexpr UInt32 mode=kIOAccelSurfaceModeWindowedBit|kIOAccelSurfaceModeColorDepth8888;
static IOAccelDeviceRegion region={1,{0,0,4,3},{{0,0,4,3}}};
struct Fixture {
 RadeonNIAccel accel; RadeonNISurfaceClient client{};
 Fixture() {
  assert(!calls.descriptors);calls=Calls{};
  memset(gpu,0xee,sizeof(gpu));memset(ram,0xa5,sizeof(ram));memset(replacement,0x5a,sizeof(replacement));
  assert(client.initWithTask((task_t)0x123,nullptr,0));client.fAccel=&accel;
 }
 ~Fixture(){client.clientClose();client.free();assert(!calls.descriptors&&!calls.locks);}
 IOReturn bind(UInt32 flags=0,UInt32 address=0x10000){
  return client.setShapeBackingAndLength(flags,0,address,24,72,&region,sizeof(region));
 }
};
int main(int argc,char **argv) {
 assert(argc==2);std::string which=argv[1];
 if(which=="admission") {
  Fixture f;assert(f.client.setIDMode(1,mode)==0);assert(!f.client.fBackingMode);
  assert(f.client.setIDMode(2,mode)==0);assert(f.client.fBackingMode);
  Fixture g;g.accel.backingEnabled=false;assert(g.client.setIDMode(2,mode)==kIOReturnUnsupported);
  Fixture h;assert(h.client.setIDMode(3,mode+1)==kIOReturnUnsupported);
  Fixture screen;screen.accel.backingEnabled=false;
  assert(screen.client.setIDMode(1,mode)==kIOReturnUnsupported);
  Fixture unavailable;unavailable.accel.gpuEnabled=false;
  assert(unavailable.client.setIDMode(1,mode)==kIOReturnUnsupported);
  assert(unavailable.client.setIDMode(2,mode)==kIOReturnUnsupported);
  Fixture gl;gl.accel.gl=true;gl.accel.gpuEnabled=false;
  assert(gl.client.setIDMode(2,mode)==0&&!gl.client.fBackingMode);
 } else if(which=="sync") {
  Fixture f;assert(f.client.setIDMode(2,mode)==0);assert(f.bind()==0);
  for(unsigned y=0;y<3;y++)for(unsigned x=0;x<32;x++)assert(gpu[128+y*32+x]==(x<16?0xa5:0xee));
  for(unsigned y=0;y<3;y++)memset(gpu+128+y*32,0x20+y,16);
  calls.fence=false;IOAccelSurfaceInformation info{};IOByteCount size=sizeof(info);
  assert(f.client.readLockOptions(0,&info,&size)==0);
  assert(info.address[0]==0x10000&&info.rowBytes==24&&info.width==4&&info.height==3);
  assert(info.pixelFormat==kIOAccelSurfaceModeColorDepth8888&&f.accel.locked==2&&!f.client.fReadMap);
  for(unsigned y=0;y<3;y++)for(unsigned x=0;x<24;x++)assert(ram[y*24+x]==(x<16?0x20+y:0xa5));
  assert(f.client.readUnlock()==0&&!f.accel.locked);
  memset(ram,0x42,72);assert(f.client.writeUnlockOptions(0)==0);
  for(unsigned y=0;y<3;y++)for(unsigned x=0;x<16;x++)assert(gpu[128+y*32+x]==0x42);
  assert(calls.barriers>=3);
 } else if(which=="invalid") {
  Fixture f;assert(f.client.setIDMode(2,mode)==0);
  const unsigned header=offsetof(IOAccelDeviceRegion,rect);IOAccelDeviceRegion r=region;
  assert(f.client.configureBacking(0,0,0x10000,24,72,nullptr,0)==kIOReturnBadArgument);
  assert(f.client.configureBacking(0,0,0x10000,24,72,&r,header-1)==kIOReturnBadArgument);
  r.num_rects=2;assert(f.client.configureBacking(0,0,0x10000,24,72,&r,sizeof(r))==kIOReturnBadArgument);r=region;
  assert(f.client.configureBacking(0,1,0x10000,24,72,&r,sizeof(r))==kIOReturnBadArgument);
  assert(f.client.configureBacking(0,0,0,24,72,&r,sizeof(r))==kIOReturnBadArgument);
  assert(f.client.configureBacking(0,0,0x10000,15,72,&r,sizeof(r))==kIOReturnBadArgument);
  assert(f.client.configureBacking(0,0,0x10000,24,71,&r,sizeof(r))==kIOReturnBadArgument);
  assert(f.client.configureBacking(0,0,0xfffffff0,24,0,&r,sizeof(r))==kIOReturnBadArgument);
  assert(f.client.configureBacking(0,0,0x10000,0xffffffff,0,&r,sizeof(r))==kIOReturnBadArgument);
  r.bounds.h=0;assert(f.client.configureBacking(0,0,0x10000,24,0,&r,sizeof(r))==kIOReturnBadArgument);
  assert(!calls.descriptors&&!calls.maps&&!calls.waits);
 } else if(which=="rebind") {
  for(int failure=3;failure<=8;failure++) {
   Fixture f;assert(f.client.setIDMode(2,mode)==0);assert(f.bind()==0);
   auto old=f.client.fBackingMemory;calls.fail=failure;IOReturn result=f.bind(0,0x20000);
   assert(result!=0);
   if(failure==8||failure==6)assert(f.client.fBackingMemory!=old);
   else assert(f.client.fBackingMemory==old&&f.client.fBackingAddress==0x10000);
   assert(calls.descriptors==1);calls.fail=0;
  }
  Fixture f;assert(f.client.setIDMode(2,mode)==0);assert(f.bind()==0);
  auto old=f.client.fBackingMemory;int copies=calls.copies;
  assert(f.bind(kIOAccelSurfaceShapeStaleBackingBit)==0&&f.client.fBackingMemory==old&&calls.copies==copies);
  f.accel.waitResult=kIOReturnTimeout;assert(f.bind(0,0x20000)==kIOReturnTimeout);
  assert(f.client.fBackingMemory==old&&calls.descriptors==1);
 } else if(which=="read_bounds") {
  Fixture f;f.accel.gl=true;assert(f.client.setIDMode(3,mode)==0);IOAccelSurfaceInformation info{};IOByteCount size=sizeof(info);
  assert(f.client.readLockOptions(0,&info,&size)==0&&f.client.fReadMap);
  assert(info.address[0]==reinterpret_cast<uintptr_t>(gpu+128));assert(calls.lastFlags==(kIOMapAnywhere|kIOMapReadOnly));
  f.client.readUnlock();int maps=calls.maps;
  f.accel.waitResult=kIOReturnTimeout;assert(f.client.readLock(&info,&size)==kIOReturnTimeout&&calls.maps==maps);
  f.accel.waitResult=0;f.accel.offset=0xfffffff0;assert(f.client.readLock(&info,&size)==kIOReturnBadArgument);
  f.accel.offset=128;f.accel.stride=15;assert(f.client.readLock(&info,&size)==kIOReturnBadArgument);
  f.accel.stride=32;f.accel.height=0xffffffff;assert(f.client.readLock(&info,&size)==kIOReturnBadArgument);
  f.accel.height=0;assert(f.client.readLock(&info,&size)==kIOReturnBadArgument);
  f.accel.height=3;f.accel.aperture.bytes=500;assert(f.client.readLock(&info,&size)==kIOReturnBadArgument);
  f.accel.aperture.bytes=sizeof(gpu);calls.fail=6;assert(f.client.readLock(&info,&size)==kIOReturnNoMemory);
  assert(!f.client.fReadMap&&!f.accel.locked);
 } else if(which=="control_close") {
  Fixture f;assert(f.client.setIDMode(2,mode)==0);assert(f.bind()==0);UInt32 result=7;
  assert(f.client.control(99,0,&result)==kIOReturnUnsupported);
  assert(f.client.control(4,0,&result)==0&&result==1);
  f.accel.waitResult=kIOReturnTimeout;assert(f.client.flush(1,0)==kIOReturnTimeout);
  assert(f.client.control(1,0,&result)==kIOReturnNotReady);
  f.client.clientClose();assert(!f.client.fBackingMemory&&!f.client.fBackingMap&&!calls.descriptors&&!f.accel.locked);
  assert(calls.completes==1&&calls.forgot==1);
 } else if(which=="gpu_copy") {
  Fixture f;assert(f.client.setIDMode(2,mode)==0);assert(f.bind()==0);
  rdn_user_surface_info info{};assert(f.client.gpuInfo(&info)==0&&info.width==4&&info.height==3&&info.offset==0);
  memset(gpu+512,0xa0,96);assert(f.client.gpuCopy(512,32,4,3)==0);
  for(unsigned y=0;y<3;++y)for(unsigned x=0;x<32;++x)assert(gpu[512+y*32+x]==(x<16?0xa5:0xa0));
  f.accel.hasBuffer=false;memset(ram,0x34,72);assert(f.client.gpuCopy(512,32,4,3)==0);
  for(unsigned y=0;y<3;++y)for(unsigned x=0;x<16;++x)assert(gpu[512+y*32+x]==0x34);
  assert(f.client.gpuCopy(512,32,5,3)==kIOReturnNotReady);
  f.accel.waitResult=kIOReturnTimeout;assert(f.client.gpuCopy(512,32,4,3)==kIOReturnTimeout);
  assert(f.client.gpuInfo(&info)==kIOReturnNotReady);f.client.clientClose();assert(calls.retained==1);
 } else if(which=="resize") {
  Fixture f;assert(f.client.setIDMode(2,mode)==0);assert(f.bind()==0);int copies=calls.copies,waits=calls.waits;
  f.accel.width=5;assert(f.client.flush(1,0)==0&&calls.copies==copies&&calls.waits==waits);
  f.accel.hasBuffer=false;assert(f.client.flush(1,0)==0&&calls.copies==copies);
  f.accel.width=4;f.accel.hasBuffer=true;f.accel.offset=0xfffffff0;
  assert(f.client.flush(1,0)==kIOReturnBadArgument&&calls.copies==copies);
 } else assert(false);
}
'''


def function(source, name):
    match = re.search(r'^(?:bool|void|IOReturn) RadeonNISurfaceClient::' + name + r'\(', source, re.M)
    assert match, name
    start = match.start()
    return source[start:source.index('\n}', start) + 2]


class SurfaceBackingTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / 'kext/RadeonNI/RadeonNISurface.cpp').read_text()
        header = (ROOT / 'kext/RadeonNI/RadeonNISurface.h').read_text()
        header = '\n'.join(x for x in header.splitlines() if not x.startswith('#'))
        header = header.replace('private:', 'public:').replace('virtual ', '')
        methods = ['initWithTask', 'lockForRead', 'unlockRead', 'clientClose', 'free', 'releaseBacking',
                   'configureBacking', 'syncBacking', 'gpuInfo', 'gpuCopy', 'lockBacking', 'readLockOptions', 'readUnlockOptions',
                   'setShapeBacking', 'setShapeBackingAndLength', 'setIDMode', 'setShape', 'flush',
                   'readLock', 'readUnlock', 'writeLockOptions', 'writeUnlockOptions', 'control']
        body = '\n'.join(function(source, name) for name in methods)
        # Only replace the architecture-specific ordering instruction for the host model.
        body = body.replace('__asm__ volatile("sync" ::: "memory");', '++calls.barriers;')
        cls.tmp = tempfile.TemporaryDirectory()
        path = pathlib.Path(cls.tmp.name)
        src = path / 'test.cpp'
        src.write_text(PREFIX + header + body + SUFFIX)
        cls.exe = path / 'test'
        command = shlex.split(os.environ.get('CXX', 'c++'))
        command += shlex.split(os.environ.get('TEST_CXXFLAGS', '-O2'))
        command += ['-std=c++17', '-Wall', '-Wextra', '-Wno-misleading-indentation', '-I'+str(ROOT/'hw'), str(src), '-o', str(cls.exe)]
        subprocess.run(command, check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def check(self, name):
        subprocess.run(shlex.split(os.environ.get('TEST_RUNNER', '')) + [str(self.exe), name], check=True)

    def test_compositor_and_backing_admission(self): self.check('admission')
    def test_cpu_backing_pixels_padding_and_bidirectional_sync(self): self.check('sync')
    def test_backing_descriptor_bounds_before_allocation(self): self.check('invalid')
    def test_rebind_failure_and_stale_backing_lifetime(self): self.check('rebind')
    def test_readonly_aperture_bounds_and_fence_handoff(self): self.check('read_bounds')
    def test_control_error_and_close_release(self): self.check('control_close')
    def test_resize_defers_copy_until_matching_store(self): self.check('resize')
    def test_gpu_copy_padding_metadata_and_timeout_quarantine(self): self.check('gpu_copy')


if __name__ == '__main__':
    unittest.main()
