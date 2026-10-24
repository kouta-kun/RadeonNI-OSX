"""Check the explicit CPU publication versus GPU-only store contract."""
import pathlib,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class StoreBackingTest(unittest.TestCase):
 def test_gpu_only_rebind_clears_publication_pointer(self):
  text=(ROOT/'mesa/frontend/osmesa.c').read_text();start=text.index('OSMesaStoreBacking(OSMesaContext');end=text.index('\n}',start)+2;body=text[start:end].replace('OSMesaStoreBacking(', 'int OSMesaStoreBacking(')
  prefix=r"""
#include <cassert>
#include <cstddef>
using GLsizei=int;
#define GL_FALSE 0
#define GL_TRUE 1
struct osmesa_buffer {bool store;unsigned width;struct{int color_format;}visual;void *map;int backing_stride;};
struct context{osmesa_buffer *current_buffer;};using OSMesaContext=context*;
void osmesa_sync(OSMesaContext){} int util_format_get_blocksize(int){return 4;}
"""
  suffix=r"""
int main(){int pixels;osmesa_buffer b{true,16,{0},&pixels,128};context c{&b};
 assert(OSMesaStoreBacking(&c,nullptr,0));assert(!b.map&&!b.backing_stride&&b.store);
 assert(!OSMesaStoreBacking(&c,nullptr,128)&&!b.map);
 assert(!OSMesaStoreBacking(&c,&pixels,63)&&!b.map);
 assert(OSMesaStoreBacking(&c,&pixels,64)&&b.map==&pixels&&b.backing_stride==64);
 assert(OSMesaStoreBacking(&c,nullptr,0)&&!b.map);
 b.store=false;assert(!OSMesaStoreBacking(&c,&pixels,64));assert(!OSMesaStoreBacking(nullptr,nullptr,0));}
"""
  with tempfile.TemporaryDirectory() as d:
   p=pathlib.Path(d);(p/'test.cpp').write_text(prefix+body+suffix);subprocess.run(['c++','-std=c++17',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
