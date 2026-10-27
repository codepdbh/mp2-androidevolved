#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include <array>
#include <iostream>
#include <stdexcept>
void sub_0010DCC8_0x10dcc8(uint8_t*,R5900Context*,PS2Runtime*);
void sub_001B1C20_0x1b1c20(uint8_t*,R5900Context*,PS2Runtime*);
void check(bool ok) { if(!ok) throw std::runtime_error("PAL leaf mismatch"); }
int main() {
    // Deliberately no PS2Runtime construction: these two leaves access only GPRs.
    // No renderer, kernel, game boot or hardware service is being tested.
    const std::array<uint64_t,4> seeds{0,1,0xffffffffffffffffULL,0x123456789abcdef0ULL};
    unsigned cases=0;
    for(auto seed:seeds) for(uint32_t ret:{0u,0x00100008u,0x01fffffcu}) {
        R5900Context ctx{};
        auto* pctx=&ctx;
        SET_GPR_U64(pctx,2,seed); SET_GPR_U64(pctx,31,ret);
        sub_0010DCC8_0x10dcc8(nullptr,&ctx,nullptr);
        check(GPR_U64(pctx,2)==0xffffffffffffffffULL);
        check(ctx.pc==ret && !ctx.in_delay_slot && GPR_U64(pctx,0)==0);
        SET_GPR_U64(pctx,2,seed);
        sub_001B1C20_0x1b1c20(nullptr,&ctx,nullptr);
        check(GPR_U64(pctx,2)==0x11940000ULL && ctx.pc==ret);
        ++cases;
    }
    std::cout<<"PASS: two original PAL leaf translations, "<<cases
             <<" register/return cases; ADDIU delay slot and LUI. No game boot.\n";
}
