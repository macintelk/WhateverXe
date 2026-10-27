// Display12/13 SST transcoder programming. Platform adapters must retain
// the route owner and prove their own generation's power contract first.
// This layer knows neither power-well numbers nor firmware/connector policy.
#ifndef KERN_XELP_SST_PROGRAM_HPP
#define KERN_XELP_SST_PROGRAM_HPP
#include <stdint.h>
namespace XELP { namespace SSTProgram {
struct Prepare {
 uint32_t reason {},clockBefore {},clockWanted {},clockAfter {},funcBefore {},funcWanted {},funcAfter {},bufferBefore {};
};
template<class Read,class Write> Prepare prepare(Read read,Write write,uint32_t ddi,uint32_t pipe,uint32_t lanes,bool powered,uint32_t clockSelector=0) {
 Prepare r;
 if(ddi>=9 || pipe>=3){r.reason=8;return r;}
 if(!powered){r.reason=5;return r;}
 const auto clock=0x46140+4*pipe,func=0x60400+0x1000*pipe,buffer=0x64000+0x100*ddi;
 const auto routeClock=(clockSelector?clockSelector:ddi+1)<<28,routePort=(ddi+1)<<27;
 r.clockBefore=r.clockAfter=read(clock);r.funcBefore=r.funcAfter=read(func);r.bufferBefore=read(buffer);
 if(lanes!=1 && lanes!=2 && lanes!=4)r.reason=1;
 else if((r.funcBefore|r.bufferBefore)&0x80000000U)r.reason=2;
 else if((r.clockBefore&0xF0000000U) && (r.clockBefore&0xF0000000U)!=routeClock)r.reason=3;
 else if((r.funcBefore&0x78000000U) && (r.funcBefore&0x78000000U)!=routePort)r.reason=4;
 r.clockWanted=(r.clockBefore&0x0FFFFFFFU)|routeClock;
 r.funcWanted=(r.funcBefore&~0x7F00000EU)|routePort|0x02000000U|((lanes-1)<<1&0xEU);
 if(!r.reason) {
  write(clock,r.clockWanted);r.clockAfter=read(clock);
  if(r.clockAfter!=r.clockWanted)r.reason=6;
  else {write(func,r.funcWanted);r.funcAfter=read(func);if(r.funcAfter!=r.funcWanted)r.reason=7;}
 }
 return r;
}
struct Framing {uint32_t before {},wanted {},after {};bool active {},alternate {},ready {};};
template<class Read,class Write> Framing framing(Read read,Write write,uint32_t pipe,uint32_t enhanced,
                                               bool panel,bool sinkReady,uint32_t alternate,bool powered) {
 Framing r;if(pipe>=3 || enhanced>1 || !powered)return r;
 const auto reg=0x60540+0x1000*pipe;
 r.before=read(reg);r.wanted=(r.before&~0x40000U)|(enhanced<<18);
 r.alternate=panel && sinkReady && alternate<=1;
 if(r.alternate)r.wanted=(r.wanted&~0x40U)|(alternate<<6);
 r.active=(r.before&0x88000000U)==0x80000000U;
 if(r.active && r.before!=r.wanted)write(reg,r.wanted);
 r.after=read(reg);r.ready=true;return r;
}
}}
#endif
