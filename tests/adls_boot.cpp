#include <cstring>
#include <cassert>
#include <map>
#include <vector>
#include <cstdio>
#include "kern_adls_boot.hpp"
using namespace AlderLakeS;
struct IO {
 std::map<uint32_t,uint32_t> m;std::vector<uint32_t> writes;
 unsigned ticks=0;bool stallFrames=false,stallPipe=false,stallBuffer=false,drift=false,driftConfig=false;
 bool readable(uint32_t r){return r<0x1000000;}
 uint32_t read(uint32_t r){return m[r];}
 void write(uint32_t r,uint32_t v){
  writes.push_back(r);
  for(unsigned p=0;p<3;++p){
   auto b=p*0x1000;
   if(r==0x70008+b && !(v&0x80000000)){
    assert(!(m[0x70180+b]&0x80000000) && ticks>=2);
    if(!stallPipe)v&=~0x40000000U;
   }
   if(r==0x60400+b && !(v&0x80000000)){
    auto encoding=(m[r]>>27)&15;
    if(encoding && !stallBuffer)m[0x64000+0x100*(encoding-1)]|=0x80;
    if(encoding && driftConfig)m[0x64000+0x100*(encoding-1)]^=1; // live idle indication after transport consumer stops
   }
   if(r==0x46140+4*p && !(v&0xf0000000)){
    assert(!(m[0x70008+b]&0xc0000000) && !(m[0x60400+b]&0x80000000) && !(m[0x60540+b]&0x80000000));
   }
  }
  if((r==0x64000 || r==0x64300 || r==0x64400) && !(v&0x80000000)){
   if(!stallBuffer)v|=0x80;
  }
  m[r]=v;
 }
 void delay(){++ticks;for(unsigned p=0;p<3;++p)if(!stallFrames && (m[0x70008+0x1000*p]&0x80000000))++m[0x70040+0x1000*p];
  if(drift && !writes.empty())m[0x70188]^=1;
 }
 IO(){m[0x45404]=0xff;m[0x164280]=0x01e07c00;m[0x1642bc]=0x30;for(unsigned d:{0U,3U,4U,5U,6U})m[0x64000+0x100*d]=0x80;}
 void gop(unsigned p,unsigned d,bool dp){
  auto b=0x1000*p;Port port;assert(Port::resolve(d,port));
  m[0x70008+b]=0xc0000024;m[0x60400+b]=0x80000000|((d+1)<<27)|(dp?2U<<24:0);
  m[0x60540+b]=dp?0x80040300:0;m[0x46140+4*p]=(d+1)<<28;
  m[port.buffer]=0x80000000|(dp?6:0);m[port.clock]&=~port.clockOff;
  m[0x70180+b]=0x84000008;m[0x70188+b]=120;m[0x70190+b]=0x0437077f;
  m[0x60000+b]=0x0897077f;m[0x6000c+b]=0x04640437;
 }
};
Wiring wiring(){Wiring w;w.valid=true;w.hdmi=1U<<3;w.dp=1|(1U<<4);return w;}
void protectedState(IO &io,const std::map<uint32_t,uint32_t>&before,unsigned primary){
 for(auto pair:before){auto reg=pair.first;
  bool peer=(reg>=0x60000+0x1000*primary && reg<0x61000+0x1000*primary) ||
            (reg>=0x70000+0x1000*primary && reg<0x71000+0x1000*primary && reg!=0x70040+0x1000*primary) ||
            reg==0x46140+4*primary || reg==0x45404 || reg==0x164280 || reg==0x1642bc || (reg>=0x46010&&reg<=0x46030);
  if(peer)assert(io.m[reg]==pair.second);
 }
}
int main(){
 unsigned cases=0;
 for(unsigned first=0;first<2;++first)for(unsigned count=2;count<=3-first;++count){
  IO io;io.gop(first,3,false);io.gop(first+1,4,true);if(count==3)io.gop(2,0,true);
  auto before=io.m;auto r=Boot::prepare(io,true,wiring());
  assert(r.status==Boot::Status::Complete && r.primary==first && r.parked==(((1U<<count)-2)<<first));
  protectedState(io,before,first);
  for(unsigned p=first+1;p<first+count;++p)assert(!(io.m[0x70008+0x1000*p]&0xc0000000) && !io.m[0x46140+4*p]);
  // No PLL/mux/power write: native ownership handles them after consumer stop.
  for(auto reg:io.writes)assert(reg!=0x45404 && reg!=0x164280 && reg!=0x1642bc && !(reg>=0x46010&&reg<=0x46030));
  ++cases;
 }
 // Physical output order is independent of pipe order, including a DP primary.
 const unsigned orders[][3] {{0,3,4},{0,4,3},{3,0,4},{3,4,0},{4,0,3},{4,3,0}};
 for(const auto &ports:orders){IO io;for(unsigned p=0;p<3;++p)io.gop(p,ports[p],ports[p]!=3);
  const auto before=io.m;auto r=Boot::prepare(io,true,wiring());
  assert(r.status==Boot::Status::Complete && r.primary==0 && r.parked==6);protectedState(io,before,0);++cases;
 }
 // Stop/ack failures retain remaining consumers and every primary/shared field.
 for(unsigned f=0;f<5;++f){IO io;io.gop(0,3,false);io.gop(1,4,true);
  io.stallFrames=f==0;io.stallPipe=f==1;io.stallBuffer=f==2;io.drift=f==3;io.driftConfig=f==4;
  auto before=io.m;auto r=Boot::prepare(io,true,wiring());assert(r.status==Boot::Status::Timeout || r.status==Boot::Status::Drift);
  assert(!r.parked);if(f!=3)protectedState(io,before,0);
  assert(io.m[0x46144]==0x50000000);++cases;
 }
 // Reject foreign/ambiguous firmware before any write.
 for(unsigned f=0;f<9;++f){IO io;io.gop(0,3,false);io.gop(1,4,true);auto w=wiring();
  switch(f){case 0:io.m[0x71280]=0x80000000;break;
   case 1:io.m[0x71180]|=1U<<10;break;case 2:io.m[0x61540]|=1U<<27;break;
   case 3:io.m[Display13FBIRQ::DSBControl(1,0)]=1;break;
   case 4:io.m[0x61400]=io.m[0x60400];break;case 5:w.dp=1;break;
   case 6:io.m[0x46144]=0x70000000;break;case 7:io.m[0x711ac]=4096;break;
   case 8:io.m[0x61404]=1;break;
  }
  auto r=Boot::prepare(io,true,w);assert(r.status==Boot::Status::Topology && io.writes.empty());++cases;
 }
 {IO io;io.gop(0,3,false);assert(Boot::prepare(io,true,wiring()).status==Boot::Status::Unchanged && io.writes.empty());++cases;}
 {IO io;io.gop(0,3,false);io.gop(1,4,true);assert(Boot::prepare(io,false,wiring()).status==Boot::Status::Unavailable && io.writes.empty());++cases;}
 printf("PASS %u ADL-S firmware consumer cases: multi-GOP retirement, first-active preservation, timeout and foreign-state rejection\n",cases);
}
