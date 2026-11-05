#!/usr/bin/env python3
"""Run the actual timing adapter and optionally execute the exact donor consumer."""
from pathlib import Path
import os,re,subprocess,tempfile,sys
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
wrapper='\n'.join(re.search(r'(?:uint32_t|void) XE::'+name+r'\(.*?\n\}',s,re.S).group() for name in ['tglApplyPanelTiming','wrapTGLPanelConfig','wrapTGLPanelTiming'])
start=s[s.index(' // A reused controller address'):s.index(' if (checkKernelArgument("-disablegfxfirmware")) {',s.index(' // A reused controller address'))]
install=s[s.index('    // PANEL_TIMING_INSTALL:'):s.index('    // PANEL_TIMING_INSTALL_END')]
entry=re.search(r'entry\[\]=\{([^}]+)',install).group(1)
call=re.search(r'call\[\]=\{([^}]+)',install).group(1)
if len(sys.argv)>1:
 b=Path(sys.argv[1]).read_bytes()
 assert b[0x4848c:0x4848c+20]==bytes(int(x,0) for x in entry.split(','))
 assert b[0x437d2:0x437d2+11]==bytes(int(x,0) for x in call.split(','))
 for at,key in [(0xa9951,b'AAPL00,PanelPowerUp'),(0xa9965,b'AAPL00,PanelPowerOn'),(0xa9979,b'AAPL00,PanelPowerDown'),(0xa998f,b'AAPL00,PanelPowerOff'),(0xa99a4,b'AAPL00,PanelCycleDelay')]:
  assert b[at:at+len(key)+1]==key+b'\0'
code=r'''
#include "kern_xelp_panel_power.hpp"
#include "kern_alderlake_s.hpp"
#include "kern_xelp_panel_pwm.hpp"
#include "kern_tgl_tc_native.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#include <fstream>
#include <iterator>
#include <sys/mman.h>
using mach_vm_address_t=uintptr_t;
#define FunctionCast(fn,addr) reinterpret_cast<decltype(&fn)>(addr)
struct IORegistryEntry {
 void setProperty(const char *,bool){}
 void setProperty(const char *,void *,size_t){}
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
};
struct KernelPatcher {
 unsigned routes=0;bool fail=false;uintptr_t delta=0;
 struct RouteRequest {template<class F> RouteRequest(const char*,F,uintptr_t&) {}};
 uintptr_t solveSymbol(unsigned,const char *s,uintptr_t a,size_t){return a+(strstr(s,"GetPanel")?0x4848c:0x437a6)+delta;}
 bool routeMultiple(unsigned,RouteRequest*,unsigned,uintptr_t,size_t){++routes;return !fail;}
 void clearError(){}
};
struct XE {
 static XE *callbackXE;
 bool display13FramebufferRequested=false;
 static void display13PanelTiming(void *,bool){assert(false);}
 bool tglActive=true,tglPanelTimingReady=true,adlsDisplaySelected=false;
 AlderLakeS::Wiring adlsWiring;XELP::PanelPWM adlsPanelPWM;
 uintptr_t orgTGLPanelTiming=0,orgTGLRegisterRead=0,orgTGLPanelConfig=0;
 uint32_t tglPanelTimingBIOS[3]{};bool tglPanelTimingBIOSValid=false;
 XELP::VBT::Ports firmwarePorts;
 struct {XELP::Family family=XELP::Family::TigerLake;}xeProfile;
 TGLTCNativeFunctions tglTCNativeFunctions;
 void *tglObservationController=nullptr;
 uint64_t tglPlatformCopy[0x1c0/8]{};
 static uint32_t wrapTGLPanelTiming(void *);
 static void wrapTGLPanelConfig(void *,uint32_t);
 static void tglApplyPanelTiming(void *,bool);
 void start(void *that){START}
 static uint32_t wrapTGLRegisterRead(void *,unsigned long);
 void install(KernelPatcher &patcher,unsigned index,uintptr_t address,size_t size){INSTALL}
};
XE *XE::callbackXE;
WRAPPER
alignas(8) uint8_t ctl[0x1600]{},port[0x630]{},access[0x80]{},power[0x90]{};
static void *owner=ctl;
static uint32_t input[5]{},result=0,on=0,off=0,control=0,wells=1,fuse=0x1000000;
static unsigned calls=0,refs=0,get=0,put=0,reads=0,writes=0;
template<class T> T& at(void *p,unsigned n){return *reinterpret_cast<T*>(static_cast<uint8_t*>(p)+n);}
static uint32_t original(void *c){++calls;memcpy(static_cast<uint8_t*>(c)+0x1548,input,sizeof(input));return result;}
static void *lookup(uint32_t d){assert(d<2);return port;}
static void pgGet(void *p,uint32_t n){assert(p==power&&n==1);++get;++refs;}
static void pgPut(void *p,uint32_t n){assert(p==power&&n==1&&refs==1);++put;--refs;}
static uint32_t read(void *a,unsigned long reg){
 assert(a==access&&refs==1);++reads;
 switch(reg){case 0x45404:return wells;case 0x42000:return fuse;case 0xC7208:return on;case 0xC720C:return off;case 0xC7204:return control;default:assert(false);return ~0U;}
}
static void write(void *a,unsigned long reg,uint32_t value){
 assert(a==access&&refs==0);++writes;
 switch(reg){case 0xC7208:on=value;break;case 0xC720C:off=value;break;default:assert(false);}
}
static uint32_t base(void *,uint32_t){return 0;}
static void aux(void *){}
struct Datum {uintptr_t *vtable;uint32_t value;};
static Datum properties[5]{};static unsigned propertyCalls=0;
static void *property(void *,const char *key){
 const char *keys[]={"AAPL00,PanelPowerUp","AAPL00,PanelPowerOn","AAPL00,PanelPowerDown","AAPL00,PanelPowerOff","AAPL00,PanelCycleDelay"};
 for(unsigned i=0;i<5;++i)if(!strcmp(key,keys[i])){++propertyCalls;return &properties[i];}
 assert(false);return nullptr;
}
static void *bytes(Datum *d){return &d->value;}
static void *cast(void *object,void *){return object;}
static void jump(uint8_t *p,uintptr_t target){const uint8_t code[]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};memcpy(p,code,sizeof(code));memcpy(p+2,&target,sizeof(target));}
static void patchCall(uint8_t *mem,unsigned at,unsigned target){const int32_t displacement=target-at-5;mem[at]=0xe8;memcpy(mem+at+1,&displacement,4);}

static void setup(XE &s){
 XE::callbackXE=&s;memset(ctl,0,sizeof(ctl));memset(port,0,sizeof(port));memset(access,0,sizeof(access));memset(power,0,sizeof(power));
 memset(input,0,sizeof(input));calls=get=put=reads=writes=refs=result=on=off=control=0;wells=1;fuse=0x1000000;owner=ctl;
 s.orgTGLPanelTiming=(uintptr_t)original;s.orgTGLRegisterRead=(uintptr_t)read;s.tglObservationController=ctl;
 s.tglTCNativeFunctions={base,aux,aux,pgGet,pgPut,lookup,&owner,true};
 auto &v=s.firmwarePorts;v.status=XELP::VBT::Status::Valid;v.present=1;v.port[0]={0x78C6,10,0,0x40,0,0};
 auto &p=v.panel;p.valid=true;p.ddi=0;p.backlightValid=true;p.pwmPresent=true;p.backlightType=2;
 const uint16_t delay[]={2500,700,800,5500,5000};memcpy(p.delays,delay,sizeof(delay));
 at<void*>(ctl,0xc40)=access;at<void*>(ctl,0xc8)=power;at<void*>(ctl,0xd40)=s.tglPlatformCopy;
 at<void*>(port,0x548)=reinterpret_cast<uint8_t*>(s.tglPlatformCopy)+0x20;at<uint32_t>(port,0x550)=1;at<void*>(port,0x578)=access;
 at<void*>(power,0x10)=ctl;at<void*>(power,0x78)=access;at<void*>(access,0x50)=access;at<uint32_t>(access,0x60)=0x200000;
}
int main(int argc,char **argv){
 // Execute the production installer, including short image/symbol/byte/route failures.
 std::vector<uint8_t> image(0x49000);const uint8_t entry[]={ENTRY},call[]={CALL};
 memcpy(image.data()+0x4848c,entry,sizeof(entry));memcpy(image.data()+0x437d2,call,sizeof(call));
 for(unsigned n=0;n<6;++n){
  XE s;KernelPatcher p;size_t size=image.size();
  if(n==0)size=0x48650;if(n==1)p.delta=1;if(n==2)image[0x4848c]^=1;if(n==3)image[0x437d2]^=1;if(n==4)p.fail=true;
  s.install(p,0,(uintptr_t)image.data(),size);assert(s.tglPanelTimingReady==(n==5));
  assert(p.routes==(n>=4?1U:0U));
  memcpy(image.data()+0x4848c,entry,sizeof(entry));memcpy(image.data()+0x437d2,call,sizeof(call));
 }
 for(unsigned n=0;n<25;++n){
  XE s;setup(s);auto &p=s.firmwarePorts.panel;
  switch(n){
   case 0:s.tglActive=false;break;case 1:s.tglPanelTimingReady=false;break;case 2:s.tglObservationController=nullptr;break;
   case 3:s.xeProfile.family=XELP::Family::AlderLakeN;break;case 4:p.controller=1;break;case 5:p.backlightType=5;break;
   case 6:p.backlightValid=false;break;case 7:s.tglTCNativeFunctions.qualified=false;break;case 8:owner=nullptr;break;
   case 9:s.orgTGLRegisterRead=0;break;case 10:ctl[0x918]=1;break;case 11:at<void*>(ctl,0xd0)=port;break;
   case 12:at<void*>(ctl,0xd40)=nullptr;break;case 13:port[0x117]=1;break;case 14:at<void*>(port,0x548)=ctl;break;
   case 15:at<uint32_t>(port,0x550)=0;break;case 16:at<void*>(port,0x578)=nullptr;break;
   case 17:at<void*>(power,0x10)=nullptr;break;case 18:power[0x18]=1;break;case 19:at<void*>(power,0x78)=nullptr;break;
   case 20:at<void*>(access,0x50)=nullptr;break;case 21:at<uint32_t>(access,0x60)=0xc720c;break;
   case 22:result=0xe00002bc;break;case 23:s.firmwarePorts.unmapped=1;break;case 24:s.firmwarePorts.port[0].aux=0x10;break;
  }
  assert(XE::wrapTGLPanelTiming(ctl)==result && calls==1 && get==0 && reads==0 && writes==0);
  assert(!memcmp(ctl+0x1548,input,sizeof(input)));
 }
 for(unsigned n=0;n<7;++n){
  XE s;setup(s);
  if(n==0)wells=0;if(n==1)wells=~0U;if(n==2)fuse=0;if(n==3)fuse=~0U;
  if(n==4)on=~0U;if(n==5)s.firmwarePorts.panel.delays[3]=9000;
  assert(XE::wrapTGLPanelTiming(ctl)==0&&calls==1&&get==1&&put==1&&refs==0&&writes==0);
  assert(reads==(n<4?2U:5U));
  const uint32_t expected[]={700,800,2500,5500,6};
  assert(!memcmp(ctl+0x1548,n==6?expected:input,sizeof(input)));
 }
 // Optional exact native hwSetPanelPowerConfig replay. Only its property
 // callsite, imported services and virtual register writer are replaced; the encoding/masking,
 // property conversions, defaults, cached fields and write order are real donor code.
 for(unsigned platform=0;platform<2;++platform)if(argc>1){
  propertyCalls=0;
  std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t>b((std::istreambuf_iterator<char>(f)),{});assert(b.size()>0xde4e0);
  const auto stubs=unsigned((b.size()+4095)&~4095U);const size_t length=stubs+4096;
  auto mem=(uint8_t*)mmap(nullptr,length,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(mem!=MAP_FAILED);memcpy(mem,b.data(),b.size());
  jump(mem+stubs,(uintptr_t)XE::wrapTGLPanelTiming);patchCall(mem,0x437d2,stubs);
  jump(mem+stubs+16,(uintptr_t)memset);patchCall(mem,0x484b1,stubs+16);
  jump(mem+stubs+32,(uintptr_t)cast);
  for(unsigned site: {0x484ddU,0x48524U,0x48568U,0x485afU,0x485f6U})patchCall(mem,site,stubs+32);
  uintptr_t meta=0;const uintptr_t metaPointer=(uintptr_t)&meta;memcpy(mem+0xc8050,&metaPointer,sizeof(metaPointer));
  assert(mprotect(mem,length,PROT_READ|PROT_WRITE|PROT_EXEC)==0);
  auto nativeConfig=reinterpret_cast<void(*)(void*,uint32_t)>(mem+0x437a6);
  uintptr_t vtable[40]{};vtable[0x130/8]=(uintptr_t)write;
  uintptr_t providerTable[90]{},dataTable[50]{};providerTable[0x2b8/8]=(uintptr_t)property;dataTable[0x178/8]=(uintptr_t)bytes;
  auto provider=providerTable;for(auto &p:properties)p.vtable=dataTable;
  XE s;setup(s);at<void*>(access,0)=vtable;s.orgTGLPanelConfig=(uintptr_t)nativeConfig;
  s.orgTGLPanelTiming=(uintptr_t)(mem+0x4848c);at<void*>(ctl,0xc60)=&provider;
  const uint32_t milliseconds[]={300,100,600,90,800};for(unsigned i=0;i<5;++i)properties[i].value=milliseconds[i];
  if(platform){
   s.adlsDisplaySelected=true;s.xeProfile.family=XELP::Family::AlderLakeS;
   s.adlsWiring=AlderLakeS::Wiring::fromFirmware(s.firmwarePorts);assert(s.adlsWiring.panel==1);
   const uint32_t record[]={0,0,0,0,2,0x18};memcpy(reinterpret_cast<uint8_t*>(s.tglPlatformCopy)+0x20,record,sizeof(record));
  }
  const auto config=XE::wrapTGLPanelConfig;
  control=0xabcd0080;at<uint32_t>(ctl,0xd80)=control;
  on=4000U<<16|1200;off=8000U<<16|1100;
  // First config disables PPS and overwrites the hardware with short delays.
  // The wrapper must capture BIOS before that first write, not at first ON.
  config(ctl,0);assert(on==0&&off==3000&&get==1&&put==1&&calls==0);
  writes=0;
  // BIOS 800ms cycle beats VBT 600ms; native property 900ms then beats both.
  for(unsigned n=0;n<16;++n){
   s.firmwarePorts.panel.type=n;
   properties[4].value=n*100+1;const auto expectedCycle=n+2<8?8:n+2;
   config(ctl,1);
   assert(on==(4000U<<16|1200)&&off==(8000U<<16|1100));
   assert(at<uint32_t>(ctl,0xd78)==on&&at<uint32_t>(ctl,0xd7c)==off);
   assert(at<uint32_t>(ctl,0xd80)==(0xabcd0000|(expectedCycle<<4)));
   assert(refs==0&&get==1&&put==1&&propertyCalls==5*(n+1)&&writes==4*n+2);
   config(ctl,0);assert(on==0&&off==3000);
  }
  config(ctl,0);assert(on==0&&off==3000&&at<uint32_t>(ctl,0xd80)==0xabcd0000);
  assert(propertyCalls==80&&get==1&&put==1);
  // Execute the actual startup invalidation, even for a reused address.
  s.start(ctl);assert(!s.tglPanelTimingBIOSValid);
  on=5000U<<16|1300;off=8100U<<16|1150;
  config(ctl,1);assert(on==(5000U<<16|1300)&&off==(8100U<<16|1150)&&get==2&&put==2);
  munmap(mem,length);
 }
 puts("PASS production PPS timing installer, ownership/method guards, balanced PG, native consumer replay");
}
'''.replace('INSTALL',install).replace('START',start).replace('WRAPPER',wrapper).replace('ENTRY',entry).replace('CALL',call)
with tempfile.TemporaryDirectory(prefix='panel-timing-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-arch','x86_64','-std=c++17','-O1','-g','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')]+sys.argv[1:],check=True,env=dict(os.environ,UBSAN_OPTIONS="halt_on_error=1"))
