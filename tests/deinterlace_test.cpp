#include "deinterlace.h"
#include "scene_preview.h"
#include <stdexcept>
#include <iostream>
void check(bool ok,const char* message){ if(!ok) throw std::runtime_error(message); }
int main(){
    mp2r::FieldBlend blend;
    std::vector<uint32_t> even{0xff0000ff,0xff00ff00},odd{0xffff0000,0xff00ff00};
    check(blend.process(even,1,2,true,false)==even,"first field displayed without history");
    auto expected=std::vector<uint32_t>{0xff7f007f,0xff00ff00};
    check(blend.process(odd,1,2,true,true)==expected,"interlaced field pair blends per channel");
    check(blend.process(even,1,2,true,false)==expected,"static field pair does not alternate output");
    std::vector<uint32_t> nextScene{0xff112233,0xff445566};
    check(blend.process(nextScene,1,2,true,false)==nextScene,"repeated field parity cannot mix an old scene");
    check(blend.process(even,1,2,false,false)==even,"progressive frames bypass blend");
    check(blend.process(odd,1,2,true,true)==odd,"progressive mode discards old interlace history");
    std::vector<uint32_t> resized{0xffffffff};
    check(blend.process(resized,1,1,true,false)==resized,"resolution change resets history");
    std::vector<uint32_t> scene(512*512);
    for(unsigned y=0;y<512;++y) for(unsigned x=0;x<512;++x) scene[y*512+x]=(y<<16)|x;
    const auto raw=scene;
    check(!mp2r::compensateScenePreview(scene,512,512,3,0x700000007000ull) && scene==raw,"movie FRAME mode retains orientation");
    check(!mp2r::compensateScenePreview(scene,512,512,1,0x780000006c00ull) && scene==raw,"other viewport retains orientation");
    check(mp2r::compensateScenePreview(scene,512,512,1,0x700000007000ull),"known scene viewport compensated");
    check(scene[7]==((511u<<16)|7) && scene[511*512+7]==7,"scene rows invert without horizontal mirroring");
    std::cout<<"PASS: field blend, progressive bypass and history resets\n";
}
