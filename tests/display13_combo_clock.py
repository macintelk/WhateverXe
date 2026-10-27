#!/usr/bin/env python3
"""Production Display13 fractional write boundary and optional native PLL replay."""
from pathlib import Path
import hashlib,os,re,struct,subprocess,sys,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
guard=s[s.index('    // DISPLAY13_COMBO_FRACTION:'):s.index('    // DISPLAY13_COMBO_FRACTION_END')]
guard=guard.replace('reinterpret_cast<uintptr_t>(__builtin_return_address(0))','caller')
install=s[s.index('bool XE::processDisplay13FramebufferOutput('):s.index('uint32_t XE::wrapDisplay13GMBus(')]
def array(name):
 text=re.search(r'const uint(?:8|64)_t '+name+r'\[\] \{([^}]+)\}',install).group(1)
 return [int(x.strip(),0) for x in text.split(',')]
code=r'''
#include "kern_xelp_platform.hpp"
#include "kern_tgl_port_map.hpp"
#include "kern_xelp_combo_clock.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#include <fstream>
#include <iterator>
#include <sys/mman.h>
struct IOService;
struct State {
 bool display13FramebufferRequested=true;struct {XELP::Display display=XELP::Display::XeLPD;}xeProfile;
 uintptr_t adlnComboPLLFractionCaller=0x1000;IOService *adlnTopologyController=nullptr;
}state,*callbackXE=&state;
uint32_t adjust(void *access,uint64_t reg,uint32_t value,uintptr_t caller) {GUARD return value;}
template<class T>T &field(void*p,unsigned off){return *reinterpret_cast<T*>(static_cast<uint8_t*>(p)+off);}
alignas(8) uint8_t ctl[0xf00]{},port[0x700]{},access[0x80]{};
uint32_t config[6]{};std::map<unsigned long,uint32_t> mmio;
uint8_t *image;unsigned delays=0,errors=0,get=0,put=0,fault=0;
bool correction=true,managed=false;
XELPComboClock::ClockTranslation translation;
XELPComboClock::ClockAllocation allocation;
void jump(uint8_t*p,uintptr_t to){p[0]=0x48;p[1]=0xb8;memcpy(p+2,&to,8);p[10]=0xff;p[11]=0xe0;}
void redirect(unsigned at,uintptr_t to,unsigned stub){assert(image[at]==0xe8);jump(image+stub,to);int32_t d=stub-at-5;memcpy(image+at+1,&d,4);}
uint32_t read(void*p,unsigned long reg){
 if(managed)reg=translation.reg(reg,(uintptr_t)__builtin_return_address(0)-(uintptr_t)image);
 assert(p==access&&mmio.count(reg));return mmio.at(reg);
}
void write(void*p,unsigned long reg,uint32_t value){assert(p==access&&mmio.count(reg));
 if(correction)value=adjust(p,reg,value,(uintptr_t)__builtin_return_address(0));
 if(managed) {
  const auto pc=(uintptr_t)__builtin_return_address(0)-(uintptr_t)image;
  reg=translation.reg(reg,pc);
  if(reg==0x164280 && translation.enabling && pc==0x6d77e) {
   value=translation.mux(value,pc);
   translation.prepareMux([](uint32_t r){return mmio.at(r);},[](uint32_t r,uint32_t v){mmio[r]=v;},value);
  }
 }
 if(reg==0x46010 || reg==0x46014){value&=~0x44000000U;
  if((value&0x08000000)&&fault!=1)value|=0x04000000;
  if((value&0x80000000)&&fault!=2)value|=0x40000000;}
 mmio[reg]=value;
}
void mailbox(void*p,uint32_t command,uint32_t,bool wait){assert(p==ctl&&command==7&&!wait);}
uint32_t voltage(void*p){assert(p==ctl);return 1;}
void getDDI(void*p){assert(p==port);++get;}void putDDI(void*p){assert(p==port);++put;}
void delay(unsigned n){assert(n==1);++delays;}void error(const char*,...){++errors;}
int main(int argc,char**argv){
 state.adlnTopologyController=reinterpret_cast<IOService *>(ctl);field<void*>(ctl,0xc40)=access;field<uint32_t>(ctl,0xea8)=2;
 for(unsigned bad=0;bad<9;++bad){State saved=state;
  if(bad==0)state.display13FramebufferRequested=false;if(bad==1)state.xeProfile.display=XELP::Display::XeD;
  if(bad==2)state.adlnComboPLLFractionCaller=0;if(bad==3)state.adlnTopologyController=nullptr;
  if(bad==4)field<uint32_t>(ctl,0xea8)=1;if(bad==8)field<void*>(ctl,0xc40)=port;
  assert(adjust(bad==5?nullptr:access,bad==6?0x164288:0x164284,0x1c001a5,bad==7?0x1001:0x1000)==0x1c001a5);
  state=saved;field<uint32_t>(ctl,0xea8)=2;field<void*>(ctl,0xc40)=access;
 }
 assert(adjust(access,0x164284,0x1c001a5,0x1000)==0xe001a5);
 assert(adjust(access,0x16428c,0x1c001a5,0x1000)==0xe001a5);
 assert(adjust(access,0x164294,0x1c001a5,0x1000)==0x1c001a5);
 if(argc>1){
  std::ifstream f(argv[1],std::ios::binary);std::vector<char>raw((std::istreambuf_iterator<char>(f)),{});
  image=(uint8_t*)mmap(nullptr,0x400000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0);assert(image!=MAP_FAILED);
  memcpy(image,raw.data(),raw.size());
  for(auto table:{std::pair<unsigned,unsigned>{0xcd330,3},{0xcd348,6},{0xcd378,3},{0xcd3c0,3}})
   for(unsigned i=0;i<table.second;++i){auto &v=field<uint64_t>(image,table.first+8*i);assert(v>=0xd0000&&v<0x160000);v+=(uintptr_t)image;}
  jump(image+0x5e344,(uintptr_t)read);jump(image+0x4691e,(uintptr_t)mailbox);jump(image+0x6bf6e,(uintptr_t)voltage);
  jump(image+0x1fefe,(uintptr_t)getDDI);jump(image+0x1ff54,(uintptr_t)putDDI);
  for(unsigned at:{0x6d081U,0x6d9ecU,0x6dd6dU,0x6de16U})redirect(at,(uintptr_t)delay,0x300000);
  for(unsigned at:{0x6d54cU,0x6dda6U})redirect(at,(uintptr_t)error,0x300020);
  uintptr_t vt[64]{};vt[0x130/8]=(uintptr_t)write;field<void*>(access,0)=vt;field<void*>(port,0x548)=config;
  state.adlnComboPLLFractionCaller=(uintptr_t)image+0x6d973;
  auto enable=(void(*)(void*,void*,void*,uint32_t*))(image+0x6ce10);auto disable=(void(*)(void*,void*,void*))(image+0x6dae6);
  unsigned cases=0,counterexamples=0;
  for(unsigned ddi=0;ddi<2;++ddi)for(unsigned ref=0;ref<3;++ref)
  for(unsigned rate:{6U,10U,20U,30U})for(bool hdmi:{false,true})for(bool fixed:{false,true}) {
   if(hdmi && rate!=6)continue;
   correction=fixed;get=put=delays=errors=0;field<uint32_t>(ctl,0xea8)=ref;
   const uint32_t record[]={ddi,ddi+1,hdmi?2U:1U,0,hdmi?0x800U:0x400U,hdmi?0x202U:0x201U};memcpy(config,record,sizeof(record));
   field<uint8_t>(port,0x114)=hdmi?0:1;field<uint32_t>(port,0x128)=rate;
   const auto pll=0x46010+4*ddi,cfg0=0x164284+8*ddi,cfg1=cfg0+4,off=1U<<(10+ddi);
   mmio={{pll,0},{cfg0,0},{cfg1,0},{0x164280,0x01e07400}};
   uint32_t params[]={464,2049,2,3,2};enable(ctl,port,nullptr,hdmi?params:nullptr);
   assert(!errors&&!delays&&get==1&&mmio[pll]==0xcc000000);
   assert(mmio[0x164280]==((0x01e07400&~(off|(3U<<(2*ddi))))|(ddi<<(2*ddi))));
   const auto nominal=hdmi?(464|(2049<<10)):ref==0?0x1000151U:0x1c001a5U;
   const auto expected=ref==2?(nominal&~0x1fffc00U)|(((((nominal>>10)&32767)+1)/2)<<10):nominal;
   if(fixed)assert(mmio[cfg0]==expected);else if(ref==2){assert(mmio[cfg0]!=expected);++counterexamples;}
   assert(mmio[cfg1]==(hdmi?0xe84U:rate==6?0x90U:rate==10?0x88U:rate==20?0x48U:0x44U));
   disable(ctl,port,nullptr);assert(!errors&&!delays&&put==1&&!mmio[pll]&&(mmio[0x164280]&off));++cases;
  }
  assert(cases==60 && counterexamples==10);
  // Same native bodies and shared allocator on TGL/Display13. Occupied peer
  // PLLs have deliberately different coefficients, making clobber visible.
  unsigned sharing=0;
  for(auto layout:{XELPComboClock::Layout::TGL,XELPComboClock::Layout::Display13})
  for(unsigned target=0;target<2;++target)for(unsigned peer=0;peer<2u;++peer)
  for(unsigned occupied=0;occupied<2;++occupied)for(bool hdmi:{false,true}) {
   if(peer==target)continue;
   allocation={};get=put=delays=errors=fault=0;correction=true;
   const auto off=1U<<(peer==2?24:10+peer),shift=2*peer;
   mmio={{0x164280,(0x01000C00&~(off|(3U<<shift)))|(occupied<<shift)},
     {0x46010,0},{0x46014,0},{0x164284,0},{0x164288,0},{0x16428c,0},{0x164290,0}};
   const auto busy=0x46010+4*occupied,cfg0=0x164284+8*occupied,cfg1=cfg0+4;
   mmio[busy]=0xcc000000;mmio[cfg0]=0x12345;mmio[cfg1]=0x67890;
   XELPComboClock::ClockSnapshot snap;snap.layout=layout;snap.mux[0]=mmio.at(0x164280);
   snap.enable[0]=mmio.at(0x46010);snap.enable[1]=mmio.at(0x46014);
   XELPComboClock::Port route(target);auto choice=allocation.choose(route,snap,true);
   assert(choice.valid && choice.pll==1-occupied);
   config[0]=target;config[2]=hdmi?2:1;field<uint8_t>(port,0x114)=hdmi?0:1;field<uint32_t>(port,0x128)=20;
   field<uint32_t>(ctl,0xea8)=2;
   translation={target,choice.pll,true};managed=true;
   uint32_t params[]={464,2049,2,3,2};enable(ctl,port,nullptr,hdmi?params:nullptr);
   assert(mmio[busy]==0xcc000000 && mmio[cfg0]==0x12345 && mmio[cfg1]==0x67890);
   assert(((mmio[0x164280]>>(2*target))&3)==choice.pll);
   const auto nominal=hdmi?(464|(2049<<10)):0x1c001a5;
   const auto expected=(nominal&~0x1fffc00U)|(((((nominal>>10)&32767)+1)/2)<<10);
   assert(mmio[0x164284+8*choice.pll]==expected);
   translation.enabling=false;disable(ctl,port,nullptr);
   assert(!mmio[0x46010+4*choice.pll] && mmio[busy]==0xcc000000);
   assert(mmio[cfg0]==0x12345 && mmio[cfg1]==0x67890 && !errors && !delays && get==1 && put==1);
   if(peer<2) {
    alignas(8) uint8_t other[0x700]{};uint32_t otherConfig[]={peer,peer+1,hdmi?2U:1U,0,0,0};
    field<void*>(other,0x548)=otherConfig;
    snap.mux[0]=mmio.at(0x164280);snap.enable[choice.pll]=0;
    const auto shared=allocation.share(XELPComboClock::Port(peer),snap);
    assert(shared.valid && shared.pll==occupied);
    translation={target,shared.pll,true};enable(ctl,port,other,hdmi?params:nullptr);
    assert(mmio[cfg0]==0x12345 && mmio[cfg1]==0x67890 && mmio[busy]==0xcc000000);
    assert(((mmio[0x164280]>>(2*target))&3)==occupied);
    translation.enabling=false;disable(ctl,port,port);
    assert(mmio[cfg0]==0x12345 && mmio[cfg1]==0x67890 && mmio[busy]==0xcc000000);
    assert(!errors && !delays && get==2 && put==2);
   }
   managed=false;++sharing;
  }
  printf("PASS %u TGL/Display13 native shared-PLL DP/HDMI cycles; live A/B peer coefficients retained\n",sharing);
  munmap(image,0x400000);
  printf("PASS %u exact native Display13 Combo A/B DP/HDMI clock cycles, %u old 38.4 MHz counterexamples\n",cases,counterexamples);
 }
 puts("PASS production Display13 nominal-write owner/caller/reference/register guards");
}
'''.replace('GUARD',guard)
with tempfile.TemporaryDirectory(prefix='display13-combo-clock-') as d:
 p=Path(d);(p/'test.cpp').write_text(code);args=[]
 if len(sys.argv)>1:
  b=bytearray(Path(sys.argv[1]).read_bytes());assert hashlib.sha256(b).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
  assert list(struct.unpack_from('<6Q',b,0x9a0e8))==array('before')
  assert b[0x6d95d:0x6d973]==bytes(array('fractionWrite'))
  struct.pack_into('<6Q',b,0x9a0e8,*array('after'));(p/'donor').write_bytes(b);args=[str(p/'donor')]
 subprocess.run(['clang++','-arch','x86_64','-std=c++17','-O1','-g','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')]+args,check=True,env=dict(os.environ,UBSAN_OPTIONS='halt_on_error=1'))
