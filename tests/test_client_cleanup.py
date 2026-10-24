"""Exercise production client teardown with reused client addresses."""
import os,pathlib,shlex,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
def function(source,signature):
 start=source.index(signature);at=source.index('{',start);depth=1;end=at+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
PREFIX=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include <strings.h>
using UInt32=uint32_t;
static unsigned locks;
static unsigned allocationLocks;
static int allocationMutex;
void IOLockLock(int *p){if(p==&allocationMutex){assert(!allocationLocks);++allocationLocks;}else{assert(!locks);++locks;}}
void IOLockUnlock(int *p){if(p==&allocationMutex){assert(allocationLocks==1);--allocationLocks;}else{assert(locks==1);--locks;}}
#define IOLog(...) ((void)0)
class RadeonNIAccel {
public:
 enum{kMaxGartBindings=4};int lock=0,*fLock=&lock;
 struct Binding{void *memory,*owner;UInt32 offset;}fGartBound[4]{};
 struct Shape{bool used;void *owner;UInt32 bufWidth,bufOffset;void *backingClient=nullptr;void *producerTask=nullptr;bool gl=false;UInt32 bufRowBytes=0,bufHeight=0;}fShapes[8]{};
 UInt32 fShapeCapacity=8;struct{UInt32 fence_emitted=19;}fAccel;
 bool fSurfaceGpuStalled=false;
 bool uvdSafe=true,gfxSafe=true;unsigned rawDrops=0,uvdCalls=0;std::vector<UInt32> freed,unbound;
 bool uvdDestroy(const UInt32*,UInt32,UInt32){++uvdCalls;return uvdSafe;}
 void uvdUser(bool add){assert(!add);++rawDrops;}
 bool fenceWait(UInt32 fence,UInt32 ms){assert(!locks&&fence==19&&ms==10000);return gfxSafe;}
 void freeVram(UInt32 offset){assert(!locks);freed.push_back(offset);}
 int gartUnbind(UInt32 offset,void *owner){for(auto &b:fGartBound)if(b.memory&&b.offset==offset&&b.owner==owner){unbound.push_back(offset);b.memory=nullptr;return 0;}assert(false);return 1;}
 void clearSurfaceBufferRecord(Shape *record);
 void gartUnbindAll(void*);
 void retainClientMemory(const UInt32*,UInt32,void*);
 void releaseClientMemory(const UInt32*,UInt32,void*);
};
class RadeonNIUserClient {
public:
 RadeonNIAccel *fAccel;UInt32 storage[4]{100,200,0,0};UInt32 *fOffsets=storage,fCount=2;
 bool fSurfaceGpuFailed=false;int *fAllocationLock=&allocationMutex;
 bool fUvdUser=true;UInt32 fUvdHandles[2]{3,4},fUvdHandleCount=2,fUvdHandleEpoch=1;
 void freeAll();
};
'''
SUFFIX=r'''
int main(){
 for(unsigned mode=0;mode<5;++mode){
  RadeonNIAccel a;RadeonNIUserClient client;client.fAccel=&a;int memory=1,other=2;
  a.fGartBound[0]={&memory,&client,1000};a.fGartBound[1]={&other,&other,2000};
  a.fShapes[0]={true,&client,0,0};a.fShapes[1]={true,&other,64,100};a.fShapes[2]={true,&other,64,999};
  if(mode==0)a.uvdSafe=false;if(mode==1)a.gfxSafe=false;
  if(mode==3)client.fSurfaceGpuFailed=true;if(mode==4)a.fSurfaceGpuStalled=true;
  client.freeAll();assert(a.rawDrops==1&&a.uvdCalls==1&&!client.fCount&&!client.fUvdUser&&!client.fUvdHandleCount);
  assert(!a.fShapes[0].used&&!a.fShapes[1].used&&a.fShapes[2].used);
  assert(a.fGartBound[1].owner==&other&&a.fGartBound[1].memory==&other);
  if(mode<2||mode==3){
   assert(a.fGartBound[0].memory==&memory&&!a.fGartBound[0].owner&&a.freed.empty()&&a.unbound.empty());
   // Reuse the same client address, as an object allocator can after close.
   a.uvdSafe=a.gfxSafe=true;client.fSurfaceGpuFailed=false;client.fCount=1;client.storage[0]=300;
   a.fGartBound[2]={&memory,&client,3000};a.fShapes[3]={true,&client,64,300};
   client.freeAll();assert(a.fGartBound[0].memory==&memory&&!a.fGartBound[0].owner);
   assert(a.freed.size()==1&&a.freed[0]==300&&a.unbound.size()==1&&a.unbound[0]==3000&&!a.fShapes[3].used);
  }else if(mode==4){assert(a.freed.empty()&&a.unbound.empty()&&!a.fGartBound[0].owner);}
  else{assert(a.freed.size()==2&&a.unbound.size()==1&&!a.fGartBound[0].memory);}
  assert(!locks&&!allocationLocks);
 }
 puts("PASS client teardown preserves unknown-busy allocations across owner address reuse");
}
'''
class CleanupTest(unittest.TestCase):
 def test_production_cleanup(self):
  source=(ROOT/'kext/RadeonNI/RadeonNIAccel.cpp').read_text()
  methods='\n'.join(function(source,x) for x in ['void RadeonNIAccel::clearSurfaceBufferRecord(', 'void RadeonNIAccel::gartUnbindAll(', 'void RadeonNIAccel::retainClientMemory(', 'void RadeonNIAccel::releaseClientMemory(', 'void RadeonNIUserClient::freeAll('])
  methods=methods.replace('SurfaceRecord *record','Shape *record')
  with tempfile.TemporaryDirectory() as d:
   path=pathlib.Path(d);(path/'test.cpp').write_text(PREFIX+methods+SUFFIX)
   subprocess.run(shlex.split(os.environ.get('CXX','g++'))+shlex.split(os.environ.get('TEST_CXXFLAGS','-O2'))+['-std=c++17',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
   subprocess.run(shlex.split(os.environ.get('TEST_RUNNER',''))+[str(path/'test')],check=True,timeout=30)
if __name__=='__main__':unittest.main()
