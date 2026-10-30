// Diagnostic tool: replays one IPU command on captured bitstream bytes.
// usage: ipu_replay_tool HEXBYTES BCLR_BP CTRL_HEX COMMAND_HEX
#include "ee_ipu.h"
#include <cstdio>
#include <string>
#include <vector>
int main(int argc,char** argv) {
    std::string hex=argv[1]; unsigned bit=std::stoul(argv[2]); uint32_t ctrl=std::stoul(argv[3],nullptr,16); uint32_t cmd=std::stoul(argv[4],nullptr,16);
    std::vector<uint8_t> b; for(size_t i=0;i+1<hex.size();i+=2) b.push_back(uint8_t(std::stoul(hex.substr(i,2),nullptr,16)));
    while(b.size()%16) b.push_back(0);
    mp2r::EeIpu ipu; ipu.debug=stdout;
    ipu.command(bit);                       // BCLR with BP
    ipu.writeCtrl(ctrl);
    ipu.inputBytes(b.data(),uint32_t(b.size()));
    ipu.command(cmd);
    std::printf("busy=%d stop=%s out=%zu\n",ipu.busy,ipu.stop_reason.c_str(),ipu.output.size());
}
