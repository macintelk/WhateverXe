#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>
#include <sys/mman.h>
#include "kern_alderlake_s.hpp"
#include "kern_tgl_port_map.hpp"
#include "kern_adls_clock.hpp"
template<class T>T &at(void *p,unsigned off){return *reinterpret_cast<T *>(static_cast<uint8_t *>(p)+off);}
void jump(uint8_t *p,uintptr_t t){p[0]=0x48;p[1]=0xb8;memcpy(p+2,&t,8);p[10]=0xff;p[11]=0xe0;}
struct Event {uint32_t kind,reg,value;};
std::vector<Event> events;std::map<unsigned long,uint32_t> mmio;
alignas(8) uint8_t ctl[0xF00] {},port[0x700] {},access[0x70] {};
uintptr_t vtable[64] {};uint32_t config[]={3,9,2,0,0x800,0x202};
uint8_t *image;unsigned fault,delays,errors;
AlderLakeS::ClockAllocation allocation;
AlderLakeS::ClockTranslation translation;
bool managed;
uint32_t readReg(void *p,unsigned long reg){
 if(managed)reg=translation.reg(reg,reinterpret_cast<uintptr_t>(__builtin_return_address(0))-reinterpret_cast<uintptr_t>(image));
 assert(p==access);assert(mmio.count(reg));return mmio.at(reg);
}
void writeReg(void *p,unsigned long reg,uint32_t v){
 assert(p==access);
 const auto pc=reinterpret_cast<uintptr_t>(__builtin_return_address(0))-reinterpret_cast<uintptr_t>(image);
 if(reinterpret_cast<uintptr_t>(__builtin_return_address(0))==reinterpret_cast<uintptr_t>(image+0x6d973))
  TGLComboPLLFraction::adjust(at<uint32_t>(ctl,0xEA8),reg==0x164294?0x164284:reg,v);
 if(managed){
  reg=translation.reg(reg,pc);
  if(reg==0x164280 && translation.enabling && pc==0x6d77e) {
   v=translation.mux(v,pc);
   translation.prepareMux([](uint32_t r){return mmio.at(r);},
    [](uint32_t r,uint32_t value){events.push_back({1,r,value});mmio[r]=value;},v);
  }
 }
 assert(mmio.count(reg));
 events.push_back({1,uint32_t(reg),v});
 if(reg==0x46010 || reg==0x46014 || reg==0x46018 || reg==0x46030) {
  // Model hardware ACK/LOCK independently from request bits. Failure cases
  // exercise the donor's bounded waits, not successful hardware operation.
  v&=~0x44000000U;
  if((v&0x08000000U) && fault!=1)v|=0x04000000U;
  if((v&0x80000000U) && fault!=2)v|=0x40000000U;
 }
 mmio[reg]=v;
}
void mailbox(void *p,uint32_t command,uint32_t value,bool wait){assert(p==ctl && command==7 && !wait);events.push_back({2,command,value});}
uint32_t voltage(void *p){assert(p==ctl);return 1;}
void getDDI(void *p){assert(p==port && (at<uint32_t *>(p,0x548)[0]==0 || at<uint32_t *>(p,0x548)[0]==3 || at<uint32_t *>(p,0x548)[0]==4));events.push_back({3,3,1});}
void putDDI(void *p){assert(p==port && (at<uint32_t *>(p,0x548)[0]==0 || at<uint32_t *>(p,0x548)[0]==3 || at<uint32_t *>(p,0x548)[0]==4));events.push_back({3,3,0});}
void delay(unsigned n){assert(n==1);++delays;}
void error(const char *,...){++errors;}
void redirectCall(unsigned site,uintptr_t target,unsigned stub){
 assert(image[site]==0xe8);jump(image+stub,target);int32_t d=stub-site-5;memcpy(image+site+1,&d,4);
}
int main(int argc,char **argv){
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});
 image=static_cast<uint8_t *>(mmap(nullptr,0x400000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0));assert(image!=MAP_FAILED);
 memcpy(image,bytes.data(),bytes.size());
 // Rebase only the four native profiling-pointer tables used on these paths.
 for(auto table: {std::pair<unsigned,unsigned>{0xcd330,3},{0xcd348,6},{0xcd378,3},{0xcd3c0,3}})
  for(unsigned i=0;i<table.second;++i){auto &v=at<uint64_t>(image,table.first+8*i);assert(v>=0xd0000 && v<0x160000);v+=reinterpret_cast<uintptr_t>(image);}
 jump(image+0x5e344,(uintptr_t)readReg);jump(image+0x4691e,(uintptr_t)mailbox);
 jump(image+0x6bf6e,(uintptr_t)voltage);jump(image+0x1fefe,(uintptr_t)getDDI);jump(image+0x1ff54,(uintptr_t)putDDI);
 for(unsigned site:{0x6d081u,0x6d9ecu,0x6dd6du,0x6de16u})redirectCall(site,(uintptr_t)delay,0x300000);
 for(unsigned site:{0x6d54cu,0x6dda6u})redirectCall(site,(uintptr_t)error,0x300020);
 vtable[0x130/8]=(uintptr_t)writeReg;at<void *>(access,0)=vtable;at<void *>(ctl,0xc40)=access;
 at<uint32_t *>(port,0x548)=config;
 auto enable=(void(*)(void*,void*,void*,uint32_t*))(image+0x6ce10);
 auto disable=(void(*)(void*,void*,void*))(image+0x6dae6);
 unsigned cases=0;
 for(unsigned ddi:{0u,3u,4u})for(unsigned ref=0;ref<=2;++ref)for(unsigned p:{2u,3u,5u,7u})for(unsigned k:{1u,2u,3u})for(unsigned q:{1u,3u}) {
  config[0]=ddi;config[1]=config[2]==0?0:ddi?ddi+6:1;
  AlderLakeS::Port route;AlderLakeS::PLL pll;assert(AlderLakeS::Port::resolve(ddi,route));assert(AlderLakeS::PLL::resolve(route.phy,pll));
  fault=0;events.clear();delays=errors=0;at<uint32_t>(ctl,0xea8)=ref;
  mmio={{pll.enable,0},{pll.cfg0,0},{pll.cfg1,0},{0x164280,0x01e07400}};
  uint32_t params[]={464,2049,p,q,k};AlderLakeS::HDMIClock expected;
  assert(AlderLakeS::HDMIClock::encode(params,ref,expected));
  enable(ctl,port,nullptr,params);
  assert(!errors&&!delays && mmio[pll.cfg0]==expected.cfg0 && mmio[pll.cfg1]==expected.cfg1);
  assert(mmio[pll.enable]==0xcc000000 && mmio[0x164280]==((0x01e07400&~(route.clockOff|(3U<<route.clockShift)))|(route.phy<<route.clockShift)));
  assert(events.front().kind==2 && events.front().value==3 && events.back().kind==3 && events.back().value==1);
  assert(config[0]==ddi);events.clear();disable(ctl,port,nullptr);
  assert(!errors&&!delays && mmio[pll.enable]==0 && (mmio[0x164280]&route.clockOff));
  assert(events.front().kind==3 && events.front().value==0 && events.back().kind==2 && events.back().value==1);++cases;
 }
 config[0]=3;config[1]=9;
 for(fault=1;fault<=2;++fault){
  events.clear();delays=errors=0;mmio={{0x46014,0},{0x16428c,0},{0x164290,0},{0x164280,0x01e07400}};
  uint32_t params[]={464,2048,2,3,2};enable(ctl,port,nullptr,params);
  assert(delays==2000 && errors==1 && mmio[0x164280]==0x01e07400);
 }
 // Real external DP Combo C uses physical TC2, dedicated DPLL2 and selector2.
 config[0]=4;config[1]=10;config[2]=1;config[4]=0x400;config[5]=0x201;
 at<uint8_t>(port,0x114)=1;
 for(unsigned kind:{0u,1u})for(unsigned ddi:{0u,3u,4u})for(unsigned ref=0;ref<=2;++ref)for(unsigned rate:{6u,10u,20u,30u}) {
  if(kind==0 && ddi!=0)continue;config[2]=kind;
  config[4]=kind?0x400:2;config[5]=kind?0x201:0x18;
  config[0]=ddi;config[1]=config[2]==0?0:ddi?ddi+6:1;
  AlderLakeS::Port route;AlderLakeS::PLL pll;assert(AlderLakeS::Port::resolve(ddi,route));assert(AlderLakeS::PLL::resolve(route.phy,pll));
  fault=0;events.clear();delays=errors=0;at<uint32_t>(ctl,0xea8)=ref;at<uint32_t>(port,0x128)=rate;
  mmio={{pll.enable,0},{pll.cfg0,0},{pll.cfg1,0},{0x164280,0x01e07400}};
  enable(ctl,port,nullptr,nullptr);
  assert(!errors&&!delays && config[0]==ddi && mmio[pll.enable]==0xcc000000);
  assert(mmio[0x164280]==((0x01e07400&~(route.clockOff|(3U<<route.clockShift)))|(route.phy<<route.clockShift)));
  const uint32_t cfg0=ref==0?0x1000151:ref==1?0x1c001a5:0xe001a5;
  const uint32_t cfg1=rate==6?0x90:rate==10?0x88:rate==20?0x48:0x44;
  assert(mmio[pll.cfg0]==cfg0 && mmio[pll.cfg1]==cfg1);
  disable(ctl,port,nullptr);assert(!errors && !delays && !mmio[pll.enable] && (mmio[0x164280]&route.clockOff));++cases;
 }
 // Firmware can route any live Combo PHY to any PLL. Replay the production
 // allocation and operand translation with the real enable/disable bodies.
 auto snapshot=[] {
  AlderLakeS::ClockSnapshot s;s.mux[0]=mmio.at(0x164280);s.mux[1]=mmio.at(0x1642bc);
  for(unsigned i=0;i<4;++i){AlderLakeS::PLL p;AlderLakeS::PLL::resolve(i,p);s.enable[i]=mmio.at(p.enable);}return s;
 };
 auto setup=[] {
  allocation={};fault=errors=delays=0;events.clear();mmio.clear();
  mmio[0x164280]=0x01e07c00;mmio[0x1642bc]=0x30;
  for(unsigned i=0;i<4;++i){AlderLakeS::PLL p;AlderLakeS::PLL::resolve(i,p);mmio[p.enable]=mmio[p.cfg0]=mmio[p.cfg1]=0;}
 };
 auto routeOn=[](unsigned phy,unsigned pll) {
  AlderLakeS::Port p;AlderLakeS::Port::resolve(phy?phy+2:0,p);
  auto &v=mmio[p.clock];v=(v&~(p.clockOff|(3U<<p.clockShift)))|(pll<<p.clockShift);
  AlderLakeS::PLL r;AlderLakeS::PLL::resolve(pll,r);mmio[r.enable]=0xcc000000;
  mmio[r.cfg0]=0x1234+pll;mmio[r.cfg1]=0x5678+pll;
 };
 auto run=[&](unsigned phy,bool on) {
  config[0]=phy?phy+2:0;config[2]=1;at<uint8_t>(port,0x114)=1;at<uint32_t>(port,0x128)=20;
  AlderLakeS::Port p;AlderLakeS::Port::resolve(config[0],p);
  auto choice=allocation.choose(p,snapshot(),on);assert(choice.valid);
  translation={phy,choice.pll,on};managed=true;
  if(on){allocation.binding[phy]=choice.pll+1;enable(ctl,port,nullptr,nullptr);}
  else {disable(ctl,port,choice.keepPLL?port:nullptr);
   AlderLakeS::PLL r;if(choice.keepPLL || (AlderLakeS::PLL::resolve(choice.pll,r)&&!(mmio[r.enable]&0xcc000000)))allocation.binding[phy]=0;
  }
  managed=false;assert(config[0]==(phy?phy+2:0));return choice;
 };
 unsigned sharedCases=0;
 for(unsigned target=0;target<3;++target)for(unsigned peer=0;peer<5;++peer)for(unsigned occupied=0;occupied<4;++occupied) {
  if(target==peer)continue;setup();routeOn(peer,occupied);
  const auto before=mmio;
  // A disabled output has no ownership merely because its stale selector is0.
  auto off=run(target,false);assert(off.keepPLL);
  auto on=run(target,true);assert(on.pll!=occupied);
  AlderLakeS::Port targetPort;AlderLakeS::Port::resolve(target?target+2:0,targetPort);
  uint32_t lastMux=before.at(0x164280);
  for(auto e:events)if(e.kind==1 && e.reg==0x164280) {
   if((e.value^lastMux)&(3U<<targetPort.clockShift))assert((lastMux&targetPort.clockOff)&&(e.value&targetPort.clockOff));
   lastMux=e.value;
  }
  AlderLakeS::PLL active;AlderLakeS::PLL::resolve(occupied,active);
  for(auto reg:{active.enable,active.cfg0,active.cfg1})assert(mmio[reg]==before.at(reg));
  auto done=run(target,false);assert(done.pll==on.pll && !done.keepPLL);
  for(auto reg:{active.enable,active.cfg0,active.cfg1})assert(mmio[reg]==before.at(reg));
  assert(!errors && !delays);++sharedCases;
 }
 // Inherited sharing: gate one output without stopping its peer's PLL.
 setup();routeOn(0,0);routeOn(1,0);auto before=mmio;
 assert(run(0,false).keepPLL);assert(mmio[0x46010]==before.at(0x46010));
 assert(!(mmio[0x164280]&(1U<<11)));assert(run(0,true).pll==1);
 assert(run(0,false).pll==1);assert(run(1,false).pll==0);assert(!mmio[0x46010]);
 // Failure leaves a reservation for teardown even though the gate never opens.
 for(unsigned failure:{1U,2U}) {
  setup();routeOn(1,0);fault=failure;
  assert(run(0,true).pll==1);assert(delays==2000 && errors==1);
  fault=0;errors=delays=0;assert(run(0,false).pll==1);
  assert(!allocation.binding[0] && !mmio[0x46014] && mmio[0x46010]==0xcc000000);
 }
 // Exhaustion refuses allocation; an enabled unknown PLL is never commandeered.
 setup();routeOn(1,0);routeOn(2,1);routeOn(3,2);
 AlderLakeS::Port a;AlderLakeS::Port::resolve(0,a);assert(!allocation.choose(a,snapshot(),true).valid);
 setup();mmio[0x46010]=0xcc000000;assert(allocation.choose(a,snapshot(),true).pll==1);
 // A helper outside these two native functions must see physical addresses.
 translation={0,1,true};assert(translation.reg(0x46010,0x6bf80)==0x46010);
 assert(translation.reg(0x46010,0x6d03d)==0x46014);
 printf("PASS %u firmware-peer native clock cases, shared detach, partial-enable teardown, exhaustion and helper isolation.\n",sharedCases);
 // Stop immediately after native GMBUS parameter validation, before MMIO.
 // Native request validation and its original return/status path execute.
 const uint8_t accepted[]={0x41,0xbc,0x78,0x56,0x34,0x12,0xe9};
 memcpy(image+0x53b96,accepted,sizeof(accepted));
 int32_t back=0x53b79-(0x53b96+11);memcpy(image+0x53b96+7,&back,4);
 auto bus=(uint32_t(*)(void*,uint32_t,uint32_t,void*))(image+0x53a7c);
 alignas(8) uint8_t request[0x80] {};
 for(unsigned pin:{1u,7u,9u,10u,11u,12u,13u,UINT32_MAX}) {
  const auto result=bus(ctl,pin,3,request);
  assert(result==(pin<13?0x12345678u:0xe00002c2u));assert(at<uint32_t>(request,0x1c)==result);
 }
 at<uint32_t>(request,0x38)=0x200;assert(bus(ctl,9,3,request)==0xe00002c2);
 munmap(image,0x400000);
 printf("PASS %u real donor Combo PLL enable/disable cases: physical A/TC1/TC2 preserved, PHY A/B/C selection, reference fraction, voltage/power order; 2 bounded timeout paths. MMIO and power hardware mocked.\n",cases);
}
