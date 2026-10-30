#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
namespace mp2r {
// EE Image Processing Unit: MPEG-2 macroblock decoding commands driven by the game's libmpeg.
// Commands (IPU_CMD bits 28-31): BCLR 0, IDEC 1, BDEC 2, VDEC 3, FDEC 4, SETIQ 5, SETVQ 6, CSC 7,
// PACK 8, SETTH 9. Every command first forwards FB (bits 0-5) bits. Input bitstream arrives on
// DMA channel 4 / FIFO 0x10007010, output macroblocks leave on channel 3 / FIFO 0x10007000.
// Commands run synchronously once enough input is queued; otherwise they stay pending (BUSY).
// Output formats: RGB32 macroblock 16x16 RGBA (64 qwords), RGB16 (32 qwords), RAW16 384 halfwords
// (Y 16x16, Cb 8x8, Cr 8x8; 48 qwords). Colour conversion constants are the IPU fixed-point ones.
class EeIpu {
public:
    // registers
    uint32_t ctrl{};                        // IPU_CTRL: CBP 8-13, ECD 14, SCD 15, IDP 16-17, AS 20, IVF 21, QST 22, MP1 23, PCT 24-26
    uint32_t cmd_data{},top{};
    bool busy{};
    std::string stop_reason;
    uint64_t commands{},macroblocks{};
    std::FILE* debug{};                        // per-coefficient trace (diagnostics)
    std::vector<uint8_t> output;            // bytes ready for channel 3

    void inputBytes(const uint8_t* data,size_t bytes) { in_.insert(in_.end(),data,data+bytes); input_total+=bytes; run(); }
    uint64_t input_total{};
    std::string peekHex(unsigned bytes) const {
        std::string out; char b[4];
        for(unsigned i=0;i<bytes && pos_/8+i<in_.size();++i) { std::snprintf(b,sizeof b,"%02x",in_[pos_/8+i]); out+=b; }
        return out;
    }
    void command(uint32_t value) {
        if(busy && pending_cmd_>>28!=0) { stop_reason="ipu_command_while_busy"; return; }
        pending_cmd_=value; busy=true; started_=false; csc_started_=false; ++commands; run();
    }
    uint64_t readCmd() const { return uint64_t(cmd_data)|(uint64_t(busy?0x80000000u:0)<<32); }
    uint64_t readTop() const { return uint64_t(top)|(uint64_t(busy?0x80000000u:0)<<32); }
    // Input side: an 8-qword FIFO feeding a 2-qword bit-stream buffer (IPU_BP.FP); BP counts bits
    // into the first buffered qword. Software that saves the stream rewinds IFC+FP qwords.
    uint32_t queuedQwords() const { size_t q=in_.size()/16,used=pos_/128; return uint32_t(q>used?q-used:0); }
    // Decoding works on whole macroblocks (rolled back when input runs short), so a command still
    // busy after run() is starved: it takes one more qword beyond the FIFO depth at a time.
    uint32_t inputRoom() const { uint32_t q=queuedQwords(); return q<10?10-q:busy?1:0; }
    uint32_t fp() const { return std::min<uint32_t>(2,queuedQwords()); }
    uint32_t readCtrl() const {
        uint32_t ifc=queuedQwords()-fp();
        uint32_t ofc=uint32_t(std::min<size_t>(8,output.size()/16));
        return (ctrl&~0x800000ffu)|ifc|(ofc<<4)|(busy?0x80000000u:0);
    }
    uint32_t readBp() const {
        return uint32_t(pos_%128)|((queuedQwords()-fp())<<8)|(fp()<<16);
    }
    void writeCtrl(uint32_t v) {
        if(v&0x40000000u) reset();                                 // RST is an action, not a stored bit
        ctrl=(v&0x07f30000u)|(ctrl&0x0000ffffu);
    }
    void reset() { in_.clear(); pos_=0; output.clear(); busy=false; cmd_data=0; top=0; ctrl&=0x7f33f00u; pending_cmd_=0; }
private:
    std::vector<uint8_t> in_;
    size_t pos_{};                          // bit position in in_
    uint32_t pending_cmd_{};
    bool started_{},csc_started_{};
    uint32_t csc_left_{};
    std::array<uint8_t,64> iq_{},niq_{};
    std::array<uint16_t,16> vqclut_{};
    uint32_t th0_{},th1_{};
    std::array<int,3> dc_pred_{};
    int qscale_{};
    bool iq_loaded_{},niq_loaded_{};

    size_t availableBits() const { return in_.size()*8>pos_?in_.size()*8-pos_:0; }
    bool need(size_t bits) const { return availableBits()>=bits; }
    mutable bool overrun_{};                 // a read went past the queued input
    uint32_t peek(unsigned n) const {        // n<=32, MSB-first; missing bits read as 0 and flag overrun
        uint64_t v=0; size_t p=pos_;
        for(unsigned i=0;i<n;++i,++p) {
            uint32_t byte=0;
            if((p/8)<in_.size()) byte=in_[p/8]; else overrun_=true;
            v=(v<<1)|((byte>>(7-p%8))&1);
        }
        return uint32_t(v);
    }
    struct Snapshot { size_t pos,out; std::array<int,3> dc; int qs; uint32_t ctrl,top; };
    Snapshot snapshot() const { return {pos_,output.size(),dc_pred_,qscale_,ctrl,top}; }
    std::string describe(const Snapshot& s) const {             // diagnostics for a bitstream stop
        std::string out="bit="+std::to_string(s.pos%8)+" qs="+std::to_string(s.qs)+" dc="+std::to_string(s.dc[0])+","+std::to_string(s.dc[1])+","+std::to_string(s.dc[2])+" bytes=";
        char b[4]; for(size_t i=s.pos/8;i<s.pos/8+48 && i<in_.size();++i) { std::snprintf(b,sizeof b,"%02x",in_[i]); out+=b; }
        return out;
    }
    void rollback(const Snapshot& s) { pos_=s.pos; output.resize(s.out); dc_pred_=s.dc; qscale_=s.qs; ctrl=s.ctrl; top=s.top; }
    void skip(unsigned n) { pos_+=n; }
    uint32_t get(unsigned n) { uint32_t v=peek(n); skip(n); return v; }
    void compact() { if(pos_>=8*4096) { size_t bytes=std::min((pos_/128)*16,in_.size()/16*16); in_.erase(in_.begin(),in_.begin()+bytes); pos_-=bytes*8; } }
    void emit(const void* data,size_t bytes) { auto p=static_cast<const uint8_t*>(data); output.insert(output.end(),p,p+bytes); }

    // ---------------- VLC tables (ISO/IEC 13818-2 Annex B). Codes as strings for clarity.
    struct Vlc { const char* code; int value; };
    template<size_t N> static bool lookup(const EeIpu& s,const Vlc (&table)[N],int& value,unsigned& length) {
        uint32_t bits=s.peek(17);
        for(const auto& e:table) {
            unsigned len=unsigned(std::strlen(e.code)); uint32_t code=0;
            for(unsigned i=0;i<len;++i) code=(code<<1)|uint32_t(e.code[i]-'0');
            if((bits>>(17-len))==code) { value=e.value; length=len; return true; }
        }
        return false;
    }
    static constexpr Vlc mba_[]={{"1",1},{"011",2},{"010",3},{"0011",4},{"0010",5},{"00011",6},{"00010",7},
        {"0000111",8},{"0000110",9},{"00001011",10},{"00001010",11},{"00001001",12},{"00001000",13},{"00000111",14},
        {"00000110",15},{"0000010111",16},{"0000010110",17},{"0000010101",18},{"0000010100",19},{"0000010011",20},
        {"0000010010",21},{"00000100011",22},{"00000100010",23},{"00000100001",24},{"00000100000",25},{"00000011111",26},
        {"00000011110",27},{"00000011101",28},{"00000011100",29},{"00000011011",30},{"00000011010",31},{"00000011001",32},
        {"00000011000",33},{"00000001000",0x23}/*escape*/};
    // macroblock_type flags: QUANT 0x10, MOTION_FORWARD 0x08, MOTION_BACKWARD 0x04, PATTERN 0x02, INTRA 0x01
    static constexpr Vlc mb_i_[]={{"1",0x01},{"01",0x11}};
    static constexpr Vlc mb_p_[]={{"1",0x0a},{"01",0x02},{"001",0x08},{"00011",0x01},{"00010",0x1a},{"00001",0x12},{"000001",0x11}};
    static constexpr Vlc mb_b_[]={{"10",0x0c},{"11",0x0e},{"010",0x04},{"011",0x06},{"0010",0x08},{"0011",0x0a},{"00011",0x01},
        {"00010",0x1e},{"000011",0x1a},{"000010",0x16},{"000001",0x11}};
    static constexpr Vlc cbp_[]={{"111",60},{"1101",4},{"1100",8},{"1011",16},{"1010",32},{"10011",12},{"10010",48},{"10001",20},
        {"10000",40},{"01111",28},{"01110",44},{"01101",52},{"01100",56},{"01011",1},{"01010",61},{"01001",2},{"01000",62},
        {"001111",24},{"001110",36},{"001101",3},{"001100",63},{"0010111",5},{"0010110",9},{"0010101",17},{"0010100",33},
        {"0010011",6},{"0010010",10},{"0010001",18},{"0010000",34},{"00011111",7},{"00011110",11},{"00011101",19},{"00011100",35},
        {"00011011",13},{"00011010",49},{"00011001",21},{"00011000",41},{"00010111",14},{"00010110",50},{"00010101",22},
        {"00010100",42},{"00010011",15},{"00010010",51},{"00010001",23},{"00010000",43},{"00001111",25},{"00001110",37},
        {"00001101",26},{"00001100",38},{"00001011",29},{"00001010",45},{"00001001",53},{"00001000",57},{"00000111",30},
        {"00000110",46},{"00000101",54},{"00000100",58},{"000000111",31},{"000000110",47},{"000000101",55},{"000000100",59},
        {"000000011",27},{"000000010",39},{"000000001",0}};
    static constexpr Vlc motion_[]={{"1",0},{"01",1},{"001",2},{"0001",3},{"000011",4},{"0000101",5},{"0000100",6},{"0000011",7},
        {"000001011",8},{"000001010",9},{"000001001",10},{"0000010001",11},{"0000010000",12},{"0000001111",13},{"0000001110",14},
        {"0000001101",15},{"0000001100",16}};
    static constexpr Vlc dmv_[]={{"0",0},{"10",1},{"11",-1}};
    static constexpr Vlc dc_lum_[]={{"100",0},{"00",1},{"01",2},{"101",3},{"110",4},{"1110",5},{"11110",6},{"111110",7},
        {"1111110",8},{"11111110",9},{"111111110",10},{"111111111",11}};
    static constexpr Vlc dc_chr_[]={{"00",0},{"01",1},{"10",2},{"110",3},{"1110",4},{"11110",5},{"111110",6},{"1111110",7},
        {"11111110",8},{"111111110",9},{"1111111110",10},{"1111111111",11}};
    // DCT coefficients: value = run<<8 | level; codes exclude the sign bit. EOB=-1, escape=-2.
#define MP2R_DCT_COMMON \
        {"00000000011111",0x0010},{"00000000011110",0x0011},{"00000000011101",0x0012},{"00000000011100",0x0013}, \
        {"00000000011011",0x0014},{"00000000011010",0x0015},{"00000000011001",0x0016},{"00000000011000",0x0017}, \
        {"00000000010111",0x0018},{"00000000010110",0x0019},{"00000000010101",0x001a},{"00000000010100",0x001b}, \
        {"00000000010011",0x001c},{"00000000010010",0x001d},{"00000000010001",0x001e},{"00000000010000",0x001f}, \
        {"000000000011000",0x0020},{"000000000010111",0x0021},{"000000000010110",0x0022},{"000000000010101",0x0023}, \
        {"000000000010100",0x0024},{"000000000010011",0x0025},{"000000000010010",0x0026},{"000000000010001",0x0027}, \
        {"000000000010000",0x0028},{"000000000011111",0x0108},{"000000000011110",0x0109},{"000000000011101",0x010a}, \
        {"000000000011100",0x010b},{"000000000011011",0x010c},{"000000000011010",0x010d},{"000000000011001",0x010e}, \
        {"0000000000010011",0x010f},{"0000000000010010",0x0110},{"0000000000010001",0x0111},{"0000000000010000",0x0112}, \
        {"0000000000010100",0x0603},{"0000000000011010",0x0b02},{"0000000000011001",0x0c02},{"0000000000011000",0x0d02}, \
        {"0000000000010111",0x0e02},{"0000000000010110",0x0f02},{"0000000000010101",0x1002},{"0000000000011111",0x1b01}, \
        {"0000000000011110",0x1c01},{"0000000000011101",0x1d01},{"0000000000011100",0x1e01},{"0000000000011011",0x1f01}
    static constexpr Vlc b14_[]={{"10",-1},{"11",0x0001},{"011",0x0101},{"0100",0x0002},{"0101",0x0201},{"00101",0x0003},
        {"00111",0x0301},{"00110",0x0401},{"000110",0x0102},{"000111",0x0501},{"000101",0x0601},{"000100",0x0701},
        {"0000110",0x0004},{"0000100",0x0202},{"0000111",0x0801},{"0000101",0x0901},{"000001",-2},
        {"00100110",0x0005},{"00100001",0x0006},{"00100101",0x0103},{"00100100",0x0302},{"00100111",0x0a01},{"00100011",0x0b01},
        {"00100010",0x0c01},{"00100000",0x0d01},{"0000001010",0x0007},{"0000001100",0x0104},{"0000001011",0x0203},{"0000001111",0x0402},
        {"0000001001",0x0502},{"0000001110",0x0e01},{"0000001101",0x0f01},{"0000001000",0x1001},
        {"000000011101",0x0008},{"000000011000",0x0009},{"000000010011",0x000a},{"000000010000",0x000b},{"000000011011",0x0105},
        {"000000010100",0x0204},{"000000011100",0x0303},{"000000010010",0x0403},{"000000011110",0x0602},{"000000010101",0x0702},
        {"000000010001",0x0802},{"000000011111",0x1101},{"000000011010",0x1201},{"000000011001",0x1301},{"000000010111",0x1401},
        {"000000010110",0x1501},{"0000000011010",0x000c},{"0000000011001",0x000d},{"0000000011000",0x000e},{"0000000010111",0x000f},
        {"0000000010110",0x0106},{"0000000010101",0x0107},{"0000000010100",0x0205},{"0000000010011",0x0304},{"0000000010010",0x0503},
        {"0000000010001",0x0902},{"0000000010000",0x0a02},{"0000000011111",0x1601},{"0000000011110",0x1701},{"0000000011101",0x1801},
        {"0000000011100",0x1901},{"0000000011011",0x1a01},MP2R_DCT_COMMON};
    static constexpr Vlc b15_[]={{"0110",-1},{"10",0x0001},{"010",0x0101},{"110",0x0002},{"00101",0x0201},{"0111",0x0003},
        {"00111",0x0301},{"000110",0x0401},{"00110",0x0102},{"000111",0x0501},{"0000110",0x0601},{"0000100",0x0701},
        {"11100",0x0004},{"0000111",0x0202},{"0000101",0x0801},{"1111000",0x0901},{"000001",-2},
        {"11101",0x0005},{"000101",0x0006},{"1111001",0x0103},{"00100110",0x0302},{"1111010",0x0a01},{"00100001",0x0b01},
        {"00100101",0x0c01},{"00100100",0x0d01},{"000100",0x0007},{"00100111",0x0104},{"11111100",0x0203},{"11111101",0x0402},
        {"000000100",0x0502},{"000000101",0x0e01},{"000000111",0x0f01},{"0000001101",0x1001},
        {"1111011",0x0008},{"1111100",0x0009},{"00100011",0x000a},{"00100010",0x000b},{"00100000",0x0105},
        {"0000001100",0x0204},{"000000011100",0x0303},{"000000010010",0x0403},{"000000011110",0x0602},{"000000010101",0x0702},
        {"000000010001",0x0802},{"000000011111",0x1101},{"000000011010",0x1201},{"000000011001",0x1301},{"000000010111",0x1401},
        {"000000010110",0x1501},{"11111010",0x000c},{"11111011",0x000d},{"11111110",0x000e},{"11111111",0x000f},
        {"0000000010110",0x0106},{"0000000010101",0x0107},{"0000000010100",0x0205},{"0000000010011",0x0304},{"0000000010010",0x0503},
        {"0000000010001",0x0902},{"0000000010000",0x0a02},{"0000000011111",0x1601},{"0000000011110",0x1701},{"0000000011101",0x1801},
        {"0000000011100",0x1901},{"0000000011011",0x1a01},MP2R_DCT_COMMON};
#undef MP2R_DCT_COMMON
public:
    // Exposed for the table self-check test: Kraft sums of the code sets.
    static double kraft(int table) {
        auto sum=[](const auto& t){ double s=0; for(const auto& e:t) s+=std::ldexp(1.0,-int(std::strlen(e.code))); return s; };
        switch(table) { case 0:return sum(mba_); case 1:return sum(cbp_); case 2:return sum(motion_); case 3:return sum(dc_lum_);
                        case 4:return sum(dc_chr_); case 5:return sum(b14_); case 6:return sum(b15_); }
        return 0;
    }
    static std::vector<std::pair<std::string,int>> dumpTable(int table) {
        std::vector<std::pair<std::string,int>> out;
        auto add=[&](const auto& t){ for(const auto& e:t) out.push_back({e.code,e.value}); };
        if(table==5) add(b14_); else if(table==1) add(cbp_); else if(table==0) add(mba_);
        return out;
    }
private:
    static constexpr uint8_t zigzag_[64]={0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,
        35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};
    static constexpr uint8_t alternate_[64]={0,8,16,24,1,9,2,10,17,25,32,40,48,56,57,49,41,33,26,18,3,11,4,12,19,27,34,42,50,58,
        35,43,51,59,20,28,5,13,6,14,21,29,36,44,52,60,37,45,53,61,22,30,7,15,23,31,38,46,54,62,39,47,55,63};
    static constexpr uint8_t nonlinear_[32]={0,1,2,3,4,5,6,7,8,10,12,14,16,18,20,22,24,28,32,36,40,44,48,52,56,64,72,80,88,96,104,112};
    static constexpr uint8_t default_intra_[64]={8,16,19,22,26,27,29,34,16,16,22,24,27,29,34,37,19,22,26,27,29,34,34,38,22,22,26,27,
        29,34,37,40,22,26,27,29,32,35,40,48,26,27,29,32,35,40,48,58,26,27,29,34,38,46,56,69,27,29,35,38,46,56,69,83};

    bool mp1() const { return (ctrl>>23)&1; }
    int idp() const { return int((ctrl>>16)&3); }
    // MPEG-1 (MP1) has no q_scale_type, intra_vlc_format or alternate_scan: QST, IVF and AS do not apply.
    int quantScale(int code) const { return (!mp1() && ((ctrl>>22)&1))?nonlinear_[code&31]:(code&31)*2; }

    // ---------------- block decoding
    // Decodes one 8x8 block; returns false on a bitstream error.
    bool block(bool intra,int component,int16_t out[64]) {
        int coef[64]={};
        const uint8_t* scan=(!mp1() && ((ctrl>>20)&1))?alternate_:zigzag_;
        int i=0;
        if(intra) {
            int size; unsigned len;
            if(!(component?lookup(*this,dc_chr_,size,len):lookup(*this,dc_lum_,size,len))) return false;
            skip(len);
            int diff=0;
            if(size) { int v=int(get(unsigned(size))); diff=(v&(1<<(size-1)))?v:v-((1<<size)-1); }
            dc_pred_[component]+=diff;
            coef[0]=dc_pred_[component]*(8>>idp());
            i=1;
        }
        const bool table_b15=!mp1() && ((ctrl>>21)&1) && intra;
        bool first=!intra;
        const uint8_t* matrix=intra?iq_.data():niq_.data();
        for(;;) {
            int value; unsigned len;
            if(first && peek(1)==1) { value=0x0001; len=1; }       // first non-intra coefficient: '1s' is run 0 level 1
            else if(!(table_b15?lookup(*this,b15_,value,len):lookup(*this,b14_,value,len))) { if(debug) std::fprintf(debug,"blk%d pos=%zu NO CODE peek=%08x i=%d\n",component,pos_,peek(32),i); return false; }
            first=false;
            if(debug) std::fprintf(debug,"blk%d pos=%zu value=0x%x len=%u peek=%08x\n",component,pos_,unsigned(value),len,peek(32));
            skip(len);
            if(value==-1) break;
            int run,level;
            if(value==-2) {                                        // escape: 6-bit run, 12-bit signed level (MPEG-2)
                run=int(get(6));
                if(mp1()) { level=int(int8_t(get(8))); if(level==0) level=int(get(8)); else if(level==-128) level=int(get(8))-256; }
                else { level=int(get(12)); if(level&0x800) level-=0x1000; }
            } else {
                run=value>>8; level=value&0xff;
                if(get(1)) level=-level;
            }
            i+=run;
            if(i>63) return false;
            int pos=scan[i];
            int q=matrix[pos]*qscale_;
            int v=intra?(level*q)/16:((2*level+(level>0?1:-1))*q)/32;
            if(mp1() && v && !(v&1)) v-=v>0?1:-1;                   // MPEG-1 oddification
            coef[pos]=std::clamp(v,-2048,2047);
            ++i;
        }
        if(!mp1()) {                                               // MPEG-2 mismatch control
            int sum=0; for(int k=0;k<64;++k) sum+=coef[k];
            if(!(sum&1)) coef[63]^=1;
        }
        idct(coef,out);
        return true;
    }
    static void idct(const int in[64],int16_t out[64]) {
        static double table[8][8]; static bool init=false;
        if(!init) { for(int x=0;x<8;++x) for(int u=0;u<8;++u) table[x][u]=(u?std::sqrt(0.25):std::sqrt(0.125))*std::cos((2*x+1)*u*3.14159265358979323846/16); init=true; }
        double tmp[64];
        for(int y=0;y<8;++y) for(int x=0;x<8;++x) { double s=0; for(int u=0;u<8;++u) s+=table[x][u]*in[y*8+u]; tmp[y*8+x]=s; }
        for(int x=0;x<8;++x) for(int y=0;y<8;++y) { double s=0; for(int v=0;v<8;++v) s+=table[y][v]*tmp[v*8+x]; out[y*8+x]=int16_t(std::clamp(int(std::lround(s)),-256,255)); }
    }
    // Intra IDCT already yields absolute samples (DC predictor starts at 128);
    // non-intra IDCT yields signed residuals for motion compensation.
    bool macroblock(bool intra,int cbp,bool field_dct,int16_t y[256],int16_t cb[64],int16_t cr[64]) {
        int16_t b[64];
        for(int n=0;n<4;++n) {
            std::memset(b,0,sizeof b);
            if((cbp>>(5-n))&1) { if(!block(intra,0,b)) return false; }
            int bx=(n&1)*8,by=field_dct?(n>>1):(n>>1)*8,stride=field_dct?32:16;
            for(int r=0;r<8;++r) for(int c=0;c<8;++c) y[(by*16+bx)+r*stride+c]=b[r*8+c];
        }
        std::memset(cb,0,64*2); std::memset(cr,0,64*2);
        if((cbp>>1)&1) { if(!block(intra,1,cb)) return false; }
        if(cbp&1) { if(!block(intra,2,cr)) return false; }
        return true;
    }
    // ---------------- colour conversion (IPU fixed point), alpha thresholds, sign and dithering
    void csc(const uint8_t y[256],const uint8_t cb[64],const uint8_t cr[64],bool rgb16,bool dither,bool sgn) {
        static const int dith[4][4]={{-4,0,-3,1},{2,-2,3,-1},{-3,1,-4,0},{3,-1,2,-2}};
        uint8_t px[256][4];
        for(int r=0;r<16;++r) for(int c=0;c<16;++c) {
            int lum=(0x95*std::max(0,int(y[r*16+c])-16))>>6;
            int cR=int(cr[(r>>1)*8+(c>>1)])-128,cB=int(cb[(r>>1)*8+(c>>1)])-128;
            int R=(lum+((0xcc*cR)>>6)+1)>>1,G=(lum+((-0x68*cR)>>6)+((-0x32*cB)>>6)+1)>>1,B=(lum+((0x102*cB)>>6)+1)>>1;
            uint8_t* p=px[r*16+c];
            p[0]=uint8_t(std::clamp(R,0,255)); p[1]=uint8_t(std::clamp(G,0,255)); p[2]=uint8_t(std::clamp(B,0,255)); p[3]=0x80;
            if(th0_ && p[0]<th0_ && p[1]<th0_ && p[2]<th0_) std::memset(p,0,4);
            else if(th1_ && p[0]<th1_ && p[1]<th1_ && p[2]<th1_) p[3]=0x40;
            if(sgn) { p[0]^=0x80; p[1]^=0x80; p[2]^=0x80; }
        }
        if(!rgb16) { emit(px,sizeof px); return; }
        uint16_t out[256];
        for(int i=0;i<256;++i) {
            int d=dither?dith[(i/16)&3][(i%16)&3]:0;
            auto c5=[&](int v){ return uint16_t(std::clamp(v+d,0,255)>>3); };
            out[i]=uint16_t(c5(px[i][0])|(c5(px[i][1])<<5)|(c5(px[i][2])<<10)|(px[i][3]?0x8000:0));
        }
        emit(out,sizeof out);
    }
    // Start-code check after a macroblock: zero byte -> align and scan for 0x000001.
    void startCodeCheck() {
        if(need(8) && peek(8)==0) {
            pos_=(pos_+7)&~size_t(7);
            while(need(24)) {
                uint32_t v=peek(24);
                if(v) { if(v==1) ctrl|=0x8000; else ctrl|=0x4000; break; }
                skip(8);
            }
        }
        top=peek(32);
    }
    bool macroblockModes(int& modes,unsigned& len) {
        int pct=int((ctrl>>24)&7); if(!pct) pct=1;
        bool ok=pct==1?lookup(*this,mb_i_,modes,len):pct==2?lookup(*this,mb_p_,modes,len):pct==3?lookup(*this,mb_b_,modes,len):false;
        if(ok && ((pct==2 && (modes&0x08)) || pct==3)) modes|=0x80;   // MC_FRAME (frame_pred_frame_dct)
        return ok;
    }
    // ---------------- command execution; returns once the command completes or input runs dry
    void run() {
        if(!busy || !stop_reason.empty()) return;
        uint32_t c=pending_cmd_,op=c>>28;
        if(!started_) {                                            // FB forward bits
            unsigned fb=c&0x3f;
            if(op==1 || op==2 || op==3 || op==4 || op==5) { if(!need(fb)) return; skip(fb); }
            started_=true;
            if(op==1) { for(auto& p:dc_pred_) p=128<<idp(); qscale_=quantScale(int((c>>16)&31)); ctrl&=~0xc000u; }
            if(op==2) { if((c>>26)&1) for(auto& p:dc_pred_) p=128<<idp(); qscale_=quantScale(int((c>>16)&31)); ctrl&=~0xc000u; }
        }
        switch(op) {
        case 0: in_.clear(); pos_=c&0x7f; ctrl&=~0xc000u; break;  // BCLR: empty FIFO, BP = bits 0-6 of the next data
        case 3: {                                                  // VDEC
            auto snap=snapshot(); overrun_=false;
            int tbl=int((c>>26)&3),value=0; unsigned len=0; bool ok=false;
            if(tbl==0) { ok=lookup(*this,mba_,value,len); if(ok) { skip(len); cmd_data=value==0x23?0xb0023:uint32_t(value)|(len<<16); } }
            else if(tbl==1) { ok=macroblockModes(value,len); if(ok) { skip(len); cmd_data=uint32_t(value)|(((ctrl>>24)&7)==3?len<<16:0); } }
            else if(tbl==2) {
                if(peek(1)) { skip(1); cmd_data=0x00010000; ok=true; }
                else { ok=lookup(*this,motion_,value,len); if(ok) { skip(len); int s=int(get(1)); cmd_data=uint32_t(s?-value:value)&0xffffu; cmd_data|=uint32_t(len)<<16; } }
            } else { ok=lookup(*this,dmv_,value,len); if(ok) { skip(len); cmd_data=uint32_t(value)&0xffffu; cmd_data|=uint32_t(len)<<16; } }
            if(!ok) cmd_data=0;
            if(!cmd_data) ctrl|=0x4000u; else ctrl&=~0x4000u;
            top=peek(32);
            if(overrun_) { rollback(snap); return; }
            break;
        }
        case 4: if(!need(32)) return; cmd_data=peek(32); top=cmd_data; break;   // FDEC
        case 5: {                                                  // SETIQ: 64 bytes, IQM bit 27 selects non-intra
            if(!need(512)) return;
            auto& m=((c>>27)&1)?niq_:iq_;
            for(int i=0;i<64;++i) m[zigzag_[i]]=uint8_t(get(8));
            ((c>>27)&1?niq_loaded_:iq_loaded_)=true; break;
        }
        case 6: {                                                  // SETVQ: 16 halfwords
            if(!need(256)) return;
            for(auto& v:vqclut_) { uint32_t hi=get(8),lo=get(8); v=uint16_t((lo<<8)|hi); }
            break;
        }
        case 9: th0_=c&0x1ff; th1_=(c>>16)&0x1ff; break;           // SETTH
        case 2: {                                                  // BDEC
            auto snap=snapshot(); overrun_=false;
            bool intra=(c>>27)&1,field=(c>>25)&1;
            int cbp=0x3f;
            if(!intra) { int v; unsigned len; if(!lookup(*this,cbp_,v,len)) { if(overrun_) { rollback(snap); return; } stop_reason="ipu_bdec_cbp"; return; } skip(len); cbp=v; }
            ctrl=(ctrl&~0x3f00u)|(uint32_t(cbp)<<8);
            if(!intra && !niq_loaded_) for(auto& v:niq_) v=16;
            if(intra && !iq_loaded_) std::memcpy(iq_.data(),default_intra_,64);
            int16_t y[256],cb[64],cr[64];
            bool decoded=macroblock(intra,cbp,field,y,cb,cr);
            if(overrun_) { rollback(snap); return; }
            if(!decoded) { stop_reason="ipu_bdec_bitstream "+describe(snap); return; }
            uint16_t raw[384];
            for(int i=0;i<256;++i) raw[i]=uint16_t(intra?std::clamp(int(y[i]),0,255):y[i]);
            for(int i=0;i<64;++i) { raw[256+i]=uint16_t(intra?std::clamp(int(cb[i]),0,255):cb[i]); raw[320+i]=uint16_t(intra?std::clamp(int(cr[i]),0,255):cr[i]); }
            emit(raw,sizeof raw);
            startCodeCheck();
            if(overrun_) { rollback(snap); return; }
            ++macroblocks; break;
        }
        case 1: {                                                  // IDEC: intra slice until a start code
            if(!iq_loaded_) std::memcpy(iq_.data(),default_intra_,64);
            for(;;) {
                auto snap=snapshot(); overrun_=false;               // one macroblock at a time; partial output kept
                int modes; unsigned len;
                if(!macroblockModes(modes,len)) { if(overrun_) { rollback(snap); return; } stop_reason="ipu_idec_mbtype"; return; }
                skip(len);
                bool field=false;
                if(((c>>24)&1)) field=get(1);                      // DTD: dct_type present
                if(modes&0x10) qscale_=quantScale(int(get(5)));
                int16_t y[256],cb[64],cr[64];
                bool decoded=macroblock(true,0x3f,field,y,cb,cr);
                if(overrun_) { rollback(snap); return; }
                if(!decoded) { stop_reason="ipu_idec_bitstream"; return; }
                uint8_t y8[256],cb8[64],cr8[64];
                for(int i=0;i<256;++i) y8[i]=uint8_t(std::clamp(int(y[i]),0,255));
                for(int i=0;i<64;++i) { cb8[i]=uint8_t(std::clamp(int(cb[i]),0,255)); cr8[i]=uint8_t(std::clamp(int(cr[i]),0,255)); }
                csc(y8,cb8,cr8,(c>>27)&1,(c>>26)&1,(c>>25)&1);
                // next macroblock address increment, or the end of the slice at a start code
                bool end=peek(23)==0; int inc=1; unsigned l2=0;
                if(!end && !lookup(*this,mba_,inc,l2)) end=true;
                if(overrun_) { rollback(snap); return; }
                ++macroblocks;
                if(end) { startCodeCheck(); break; }
                skip(l2);
                if(inc!=1) { stop_reason="ipu_idec_skipped_macroblocks"; return; }
            }
            break;
        }
        case 7: {                                                  // CSC: MBC macroblocks of RAW8 (384 bytes each)
            if(!csc_started_) { csc_left_=c&0x7ff; csc_started_=true; }
            for(;csc_left_;--csc_left_) {                            // one macroblock per 384 input bytes, as they arrive
                if(!need(384*8)) return;
                uint8_t y8[256],cb8[64],cr8[64];
                for(auto& v:y8) v=uint8_t(get(8)); for(auto& v:cb8) v=uint8_t(get(8)); for(auto& v:cr8) v=uint8_t(get(8));
                csc(y8,cb8,cr8,(c>>27)&1,(c>>26)&1,false);
            }
            csc_started_=false;
            break;
        }
        default: stop_reason="ipu_command_unsupported "+std::to_string(op); return;
        }
        busy=false; compact();
    }
};
}
