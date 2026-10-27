#!/usr/bin/env python3
"""Replay production shared registration across physical Combo/TC topologies."""
from pathlib import Path
import re, subprocess, tempfile, sys
r=Path(__file__).resolve().parents[1]
s=(Path(sys.argv[1]) if len(sys.argv)>1 else r/'WhateverXe/kern_xe.cpp').read_text()
f='\n'.join(re.search(re.escape(sig)+r'.*?\n\}',s,re.S).group() for sig in [
 'uint32_t XE::adlnRequestedSharedReservation(', 'IOReturn XE::wrapDisplay13FramebufferPlatformCall('])
code=r'''
#include "kern_display13_fb_memory.hpp"
#include "kern_display13_fb_topology.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
struct OSSymbol {};
struct Meta {unsigned getClassSize(){return 0x1C20;}};
struct IOService {alignas(8) unsigned char data[0x1C20] {};Meta *getMetaClass(){static Meta m;return &m;}};
struct IORegistryEntry {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}void setProperty(const char*,const void*,unsigned){}};
using IOReturn=int;constexpr int kIOReturnSuccess=0,kIOReturnNotReady=1;
using Call=IOReturn(*)(IOService*,const OSSymbol*,bool,void*,void*,void*,void*);
#define FunctionCast(name,addr) reinterpret_cast<Call>(addr)
template<class T> void put(void *p,unsigned off,T value){memcpy(static_cast<unsigned char*>(p)+off,&value,sizeof(value));}
struct {IOService *controller{};bool initialized=true,faulted=false;uint32_t sharedNativePrefix{},sharedPoolPrefix{};}display13FBGTT;
struct {bool memoryFaulted=false;}display13FBPower;
IOService *adlnSharedOwner;uintptr_t adlnSharedCallback=0x12345678;
bool adlnFixedSysMemEnabled=false;unsigned calls=0;int originalResult=0;bool badBinding=false,badInfo=false;
IOReturn original(IOService *c,const OSSymbol*,bool,void *owner,void*,void *info,void*) {
 ++calls;if(originalResult)return originalResult;
 put(c,0xC0,owner);put(c,0xD38,badBinding?uintptr_t(0):adlnSharedCallback);
 put(info,0,badInfo?0U:Display13FBMemory::field<uint32_t>(c,0xDF8));return 0;
}
struct XE {
 Display13OutputWiring adlnOutputWiring;uintptr_t orgDisplay13FramebufferPlatformCall=reinterpret_cast<uintptr_t>(original);
 static uint32_t adlnRequestedSharedReservation(IOService*);
 static IOReturn wrapDisplay13FramebufferPlatformCall(IOService*,const OSSymbol*,bool,void*,void*,void*,void*);
};
XE *callbackXE;
FUNCTIONS
void initialize(IOService &c,unsigned stolen) {
 c={};put(&c,0xD5C,3U);put(&c,0xDF5,uint8_t(3));put(&c,0xDE4,stolen);
 put(&c,0xDC8,0x180000U);put(&c,0xDCC,0x12000U);put(&c,0xDD0,uint64_t(stolen+0x192000));
 put(&c,0xDD8,0x3000U);put(&c,0xDF8,stolen+0x195000U);put(&c,0xDE8,0x4000000U);
 put(&c,0x1A10,1U);put(&c,0x1A28,uint64_t(0x37800000));
 for(unsigned slot=0;slot<3;++slot) {
  const auto o=0x1A10+40*slot,base=slot?stolen+0x195000U+(slot-1)*0x4000000U:0;
  put(&c,o+4,base);put(&c,o+8,base/4096);put(&c,o+0x20,0x4000000U);put(&c,o+0x24,0x4000U);
 }
}
int main() {
 XE x;callbackXE=&x;IOService c,owner;OSSymbol symbol;unsigned cases=0;
 for(unsigned mask=1;mask<128;++mask)if(!(mask&~Display13OutputWiring::Physical) && (mask&(mask-1)))
 for(unsigned stolen:{0x04000000U,0x06000000U,0x08000000U,0x0c000000U})
 for(unsigned failure=0;failure<10;++failure) {
  initialize(c,stolen);adlnFixedSysMemEnabled=false;x.adlnOutputWiring={};x.adlnOutputWiring.valid=true;x.adlnOutputWiring.hdmi=mask;
  for(unsigned d=0;d<7;++d)if(mask&(1U<<d))x.adlnOutputWiring.ddc[d]=d<2?d+1:d+6;
  put(&c,0x1560,&symbol);display13FBGTT={&c,true,false,0,0};adlnSharedOwner=&owner;
  display13FBPower.memoryFaulted=false;originalResult=0;badBinding=badInfo=false;
  if(failure==1)display13FBGTT.faulted=true;
  if(failure==2)display13FBPower.memoryFaulted=true;
  if(failure==3)adlnSharedOwner=nullptr;
  if(failure==4)put(&c,0xDD8,0U);
  if(failure==5)originalResult=42;
  if(failure==6)badBinding=true;
  if(failure==7)badInfo=true;
  if(failure==8){put(&c,0x1A38,3U);put(&c,0x1A48,uintptr_t(1));}
  if(failure==9){adlnFixedSysMemEnabled=true;put(&c,0xDE4,0x10000000U);} // Invalid native prefix, not bypassed.
  const auto prefix=stolen+0x195000U;
  uint32_t info[]={0xdeadbeef,0x10000000,0x9a490000};const unsigned before=calls;
  const auto result=XE::wrapDisplay13FramebufferPlatformCall(&c,&symbol,false,&owner,&symbol,info,nullptr);
  if((result==0)!=(failure==0))fprintf(stderr,"mask=%x stolen=%x failure=%u result=%d calls=%u\n",mask,stolen,failure,result,calls-before);assert((result==0)==(failure==0));assert(calls==before+(failure==0 || (failure>=5 && failure<=7)));
  if(!failure) {
   assert(info[0]==prefix && info[1]==0x10000000 && info[2]==0x9a490000);
   assert(XE::adlnRequestedSharedReservation(&c)==prefix);
   put(&c,0xDF8,prefix+4096);assert(!XE::adlnRequestedSharedReservation(&c));
  } else assert(!display13FBGTT.sharedPoolPrefix);
  ++cases;
 }
 // Fixed backing enabled still requires its entire extent inside the aperture.
 for(unsigned stolen:{0x04000000U,0x06000000U,0x08000000U,0x0c000000U}) {
  initialize(c,stolen);put(&c,0x1560,&symbol);display13FBGTT={&c,true,false,0,0};
  adlnSharedOwner=&owner;adlnFixedSysMemEnabled=true;originalResult=0;badBinding=badInfo=false;
  uint32_t info[3]{};const auto before=calls;
  const auto result=XE::wrapDisplay13FramebufferPlatformCall(&c,&symbol,false,&owner,&symbol,info,nullptr);
  const bool fits=stolen<0x08000000U;
  assert((result==0)==fits && calls==before+fits);
  if(fits)assert(info[0]==stolen+0x8195000U && XE::adlnRequestedSharedReservation(&c)==info[0]);
  else assert(!display13FBGTT.sharedPoolPrefix);
 }
 assert(cases==2280);printf("PASS %u production shared registration cases: Combo/TC, native prefix, owner, callback, stale layout and failure propagation\n",cases);
}
'''.replace('FUNCTIONS',f)
with tempfile.TemporaryDirectory(prefix='wex-shared-registration-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
