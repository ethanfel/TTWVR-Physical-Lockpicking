// SPDX-License-Identifier: GPL-3.0-only
// Export exactly the renderer's lock geometry for a private world-mount probe.
#include "LockMesh.h"
#include "PadlockMesh.h"
#include "SafeFrameMesh.h"
#include <string>
using namespace locks;
int main(int argc,char **argv) {
  if(argc!=3) { std::fprintf(stderr,"usage: export_hardware props.bin output-prefix\n");return 2; }
  FILE *f=std::fopen(argv[1],"rb");Props props;bool ok=readProps(f,props);if(f)std::fclose(f);
  if(!ok)return 3;
  for(unsigned style=0;style<3;style++) {
    std::vector<PropMesh> meshes;
    if(style==0) {auto frame=props[Frame];for(auto &v:frame.vertices)v=roundLockVertex(v);frame.material=0;meshes.push_back(frame);}
    if(style==1) {PadlockMesh p;for(auto *m:{&p.body,&p.shackle,&p.staple}) {m->material=1;meshes.push_back(*m);}}
    if(style==2) {SafeFrameMesh p;p.body.material=2;meshes.push_back(p.body);}
    auto plug=props[Cylinder];plug.material=0;for(auto &v:plug.vertices)v=plugVertex(v,style==1);meshes.push_back(plug);
    auto skirt=makePlugSkirt(props[Cylinder],style==1);skirt.material=0;meshes.push_back(skirt);
    std::string path=std::string(argv[2])+std::to_string(style)+".bin";f=std::fopen(path.c_str(),"wb");if(!f)return 4;
    uint32_t count=unsigned(meshes.size());ok=std::fwrite("TTWHard1",1,8,f)==8 && std::fwrite(&count,4,1,f)==1;
    for(const auto &m:meshes) {
      uint32_t head[]{unsigned(m.vertices.size()),unsigned(m.triangles.size()),m.material};
      ok &= std::fwrite(head,4,3,f)==3 && std::fwrite(m.vertices.data(),32,m.vertices.size(),f)==m.vertices.size() && std::fwrite(m.triangles.data(),12,m.triangles.size(),f)==m.triangles.size();
    }
    ok=std::fclose(f)==0 && ok;if(!ok)return 5;
  }
}
