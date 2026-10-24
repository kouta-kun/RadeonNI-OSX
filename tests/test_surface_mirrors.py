"""Exercise retained GPU imports through producer updates and allocation release."""
import pathlib, subprocess, tempfile, unittest
from test_surface_registry import function
ROOT = pathlib.Path(__file__).resolve().parents[1]
class SurfaceMirrorsTest(unittest.TestCase):
 def test_producer_updates_and_free(self):
  source = (ROOT/'kext/RadeonNI/RadeonNIAccel.cpp').read_text()
  harness = r"""
#include <cassert>
#include <cstdint>
#include <vector>
using UInt32=uint32_t;using UInt64=uint64_t;using IOReturn=int;
const int kIOReturnSuccess=0,kIOReturnBadArgument=1;
void IOLockLock(int *p){assert(!*p);*p=1;}void IOLockUnlock(int *p){assert(*p);*p=0;}
void rdn_mem_free(int*,UInt32){}
struct RadeonNIAccel{
 int lock=0,*fLock=&lock,fHidden=0,fMem=0;bool fSurfaceGpuStalled=false;
 struct{UInt32 aperture_size=65536;}fAccel;
 UInt32 fHiddenSize=0,fHiddenOffset=0;
 enum{kSurfaceMirrors=4};struct SurfaceMirror{UInt32 surface,offset,stride,width,height;}fSurfaceMirrors[4]{};
 struct SurfaceRecord{void *owner;UInt32 bufWidth,bufHeight,bufOffset,bufRowBytes;}fShapes[2]{};
 struct Copy{UInt64 source,destination;};std::vector<Copy> copies;
 int surfaceSlot(UInt32 id,bool){return id==10?0:id==20?1:-1;}
 UInt64 surfaceVramAddress(UInt32 offset){return 0x200000000ull+offset;}
 IOReturn surfaceGpuTransfer(UInt64 s,UInt32,UInt64 d,UInt32,UInt32,UInt32,UInt32,bool held){assert(held&&lock);copies.push_back({s,d});return 0;}
 IOReturn syncSurfaceGpu(UInt32,UInt64,UInt32,UInt32,UInt32,bool);
 IOReturn refreshSurfaceGpuOwner(void*);void freeVram(UInt32);
};
"""
  for name in ['IOReturn RadeonNIAccel::syncSurfaceGpu(', 'IOReturn RadeonNIAccel::refreshSurfaceGpuOwner(', 'void RadeonNIAccel::freeVram(']:
   harness += function(source,name)+'\n'
  harness += r"""
int main(){RadeonNIAccel a;int owner,other;
 a.fSurfaceMirrors[0]={10,4096,256,64,32};a.fSurfaceMirrors[1]={10,8192,512,64,32};
 a.fSurfaceMirrors[2]={10,12288,512,65,33};a.fSurfaceMirrors[3]={20,16384,256,64,32};
 assert(!a.syncSurfaceGpu(10,0x40000000,256,64,32,false));assert(a.copies.size()==2);
 assert(a.copies[0].destination==a.surfaceVramAddress(4096));assert(a.copies[1].destination==a.surfaceVramAddress(8192));
 a.copies.clear();a.fShapes[0]={&owner,64,32,20480,256};a.fShapes[1]={&other,64,32,24576,256};
 assert(!a.refreshSurfaceGpuOwner(&owner));assert(a.copies.size()==2&&a.copies[0].source==a.surfaceVramAddress(20480));
 a.fShapes[0].bufWidth=0;a.freeVram(4096);assert(!a.fSurfaceMirrors[0].surface);a.copies.clear();
 assert(!a.syncSurfaceGpu(10,0x40000000,256,64,32,false));assert(a.copies.size()==1);
 a.copies.clear();assert(!a.syncSurfaceGpu(10,0x40000000,512,65,33,false));assert(a.copies.size()==1&&a.copies[0].destination==a.surfaceVramAddress(12288));
}
"""
  with tempfile.TemporaryDirectory() as d:
   p=pathlib.Path(d);(p/'test.cpp').write_text(harness)
   subprocess.run(['c++','-std=c++11',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
