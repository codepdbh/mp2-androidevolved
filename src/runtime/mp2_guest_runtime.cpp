// Out-of-line forwarders from the stable guest interface to the MP2R runtime.
#include "mp2_runtime.h"
namespace { MP2InitializationRuntime* self(MP2GuestRuntime* r) { return static_cast<MP2InitializationRuntime*>(r); } }
bool MP2GuestRuntime::dispatchGuestBranch(uint8_t* ram,R5900Context* cpu,uint32_t target,uint32_t from,uint32_t link,MP2GuestRuntime::GuestBranchKind kind,const char* name) { return self(this)->dispatchGuestBranch(ram,cpu,target,from,link,kind,name); }
bool MP2GuestRuntime::eeCheckpointDue(uint32_t cycles) { return self(this)->eeCheckpointDue(cycles); }
void MP2GuestRuntime::handleSyscall(uint8_t* ram,R5900Context* cpu,uint32_t encoded) { return self(this)->handleSyscall(ram,cpu,encoded); }
void MP2GuestRuntime::handleBreak(uint8_t* ram,R5900Context* cpu) { return self(this)->handleBreak(ram,cpu); }
void MP2GuestRuntime::SignalException(R5900Context* cpu,PS2Exception exception) { return self(this)->SignalException(cpu,exception); }
void MP2GuestRuntime::executeVU0Microprogram(uint8_t* ram,R5900Context* cpu,uint32_t address) { return self(this)->executeVU0Microprogram(ram,cpu,address); }
void MP2GuestRuntime::handleTLBR(uint8_t* ram,R5900Context* cpu) { return self(this)->handleTLBR(ram,cpu); }
void MP2GuestRuntime::handleTLBWI(uint8_t* ram,R5900Context* cpu) { return self(this)->handleTLBWI(ram,cpu); }
void MP2GuestRuntime::handleTLBWR(uint8_t* ram,R5900Context* cpu) { return self(this)->handleTLBWR(ram,cpu); }
void MP2GuestRuntime::handleTLBP(uint8_t* ram,R5900Context* cpu) { return self(this)->handleTLBP(ram,cpu); }
void MP2GuestRuntime::clearLLBit(R5900Context* cpu) { return self(this)->clearLLBit(cpu); }
uint8_t MP2GuestRuntime::Load8(uint8_t* ram,R5900Context* cpu,uint32_t address) { return self(this)->Load8(ram,cpu,address); }
uint16_t MP2GuestRuntime::Load16(uint8_t* ram,R5900Context* cpu,uint32_t address) { return self(this)->Load16(ram,cpu,address); }
uint32_t MP2GuestRuntime::Load32(uint8_t* ram,R5900Context* cpu,uint32_t address) { return self(this)->Load32(ram,cpu,address); }
uint64_t MP2GuestRuntime::Load64(uint8_t* ram,R5900Context* cpu,uint32_t address) { return self(this)->Load64(ram,cpu,address); }
__m128i MP2GuestRuntime::Load128(uint8_t* ram,R5900Context* cpu,uint32_t address) { return self(this)->Load128(ram,cpu,address); }
void MP2GuestRuntime::Store8(uint8_t* ram,R5900Context* cpu,uint32_t address,uint8_t value) { return self(this)->Store8(ram,cpu,address,value); }
void MP2GuestRuntime::Store16(uint8_t* ram,R5900Context* cpu,uint32_t address,uint16_t value) { return self(this)->Store16(ram,cpu,address,value); }
void MP2GuestRuntime::Store32(uint8_t* ram,R5900Context* cpu,uint32_t address,uint32_t value) { return self(this)->Store32(ram,cpu,address,value); }
void MP2GuestRuntime::Store64(uint8_t* ram,R5900Context* cpu,uint32_t address,uint64_t value) { return self(this)->Store64(ram,cpu,address,value); }
void MP2GuestRuntime::Store128(uint8_t* ram,R5900Context* cpu,uint32_t address,__m128i value) { return self(this)->Store128(ram,cpu,address,value); }
void MP2GuestRuntime::stopMissing(R5900Context* cpu,uint32_t target,uint32_t from) { return self(this)->stopMissing(cpu,target,from); }
