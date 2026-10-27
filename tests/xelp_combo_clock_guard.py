#!/usr/bin/env python3
"""Production Combo admission across Display12/13; no inferred TC ownership."""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
f=re.search(r'bool XE::comboClockTarget\(.*?\n\}',s,re.S).group()
code=r'''
#include "kern_display13_fb_topology.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
struct XE {
 bool adlsDisplaySelected=false,display13FramebufferRequested=false,tglActive=true;
 struct {XELP::Family family=XELP::Family::TigerLake;} xeProfile;
 void *tglObservationController,*adlnTopologyController;
 uint64_t tglPlatformCopy[0x1c0/sizeof(uint64_t)]{},adlnPlatform[0x1c0/sizeof(uint64_t)]{};
 Display13OutputWiring adlnOutputWiring;
 static bool adlsClockTarget(void*,void*,void*){return false;}
 static bool comboClockTarget(void*,void*,void*);
} state,*callbackXE=&state;
FUNCTION
alignas(8) uint8_t ctl[0xf00]{},ports[2][0x700]{},access[0x80]{};
template<class T>T &at(void *p,unsigned o){return *reinterpret_cast<T*>(static_cast<uint8_t*>(p)+o);}
int main(){
 for(bool d13:{false,true}) {
  state.display13FramebufferRequested=d13;state.adlnTopologyController=state.tglObservationController=ctl;
  state.adlnOutputWiring.valid=true;state.adlnOutputWiring.hdmi=3;state.adlnOutputWiring.ddc[0]=1;state.adlnOutputWiring.ddc[1]=2;
  auto platform=reinterpret_cast<uint8_t*>(d13?state.adlnPlatform:state.tglPlatformCopy);
  at<void*>(ctl,0xc40)=access;at<uint32_t>(access,0x60)=0x200000;
  for(unsigned d=0;d<2;++d) {
   const uint32_t c[]={d,d+1,2,0,0x800,0x202};memcpy(platform+0x20+24*d,c,sizeof(c));
   at<void*>(ports[d],0x548)=platform+0x20+24*d;at<void*>(ports[d],0x578)=access;at<uint32_t>(ports[d],0x5b8)=d?0x6c10c:0x16210c;
  }
  for(unsigned d=0;d<2;++d) {
   auto p=ports[d];assert(XE::comboClockTarget(ctl,p,nullptr));assert(XE::comboClockTarget(ctl,p,ports[1-d]));
   assert(!XE::comboClockTarget(nullptr,p,nullptr));assert(!XE::comboClockTarget(access,p,nullptr));
   at<void*>(p,0x578)=ctl;assert(!XE::comboClockTarget(ctl,p,nullptr));at<void*>(p,0x578)=access;
   auto config=at<uint32_t*>(p,0x548);uint32_t clone[6];memcpy(clone,config,sizeof(clone));
   at<void*>(p,0x548)=clone;assert(!XE::comboClockTarget(ctl,p,nullptr));at<void*>(p,0x548)=config;
   config[3]=1;assert(!XE::comboClockTarget(ctl,p,nullptr));config[3]=0;
   config[0]=3;assert(!XE::comboClockTarget(ctl,p,nullptr));config[0]=d;
   at<uint32_t>(p,0x5b8)^=0x1000;assert(!XE::comboClockTarget(ctl,p,nullptr));at<uint32_t>(p,0x5b8)^=0x1000;
   at<uint32_t>(access,0x60)=0x164290;assert(!XE::comboClockTarget(ctl,p,nullptr));at<uint32_t>(access,0x60)=0x200000;
  }
 }
 puts("PASS actual TGL/Display13 Combo entry guards, bound shared source, TC/foreign/cloned/unmapped rejection");
}
'''.replace('FUNCTION',f)
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
