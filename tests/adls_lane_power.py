#!/usr/bin/env python3
"""Actual ADL-S wrappers: parent-before-IO and IO-before-release, no ref edits."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
a=s.index('uint8_t *XE::adlsPowerTarget');b=s.index('// Software-only transmitter boundary.',a)
code=r'''
#include "kern_alderlake_s.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#define FunctionCast(fn,p) reinterpret_cast<decltype(&fn)>(p)
struct XE {
 bool adlsDisplaySelected=true,adlsPowerReady=true;void *tglObservationController;
 AlderLakeS::Wiring adlsWiring;uintptr_t orgADLSPowerPG,orgTGLPowerDDIState;
 static uint8_t *adlsPowerTarget(void*);
 static void wrapADLSPowerPG(void*,bool,uint32_t);
 static void wrapTGLPowerDDIState(void*,bool,uint32_t);
};
XE state;XE *callbackXE=&state;
struct Call{unsigned kind,target;bool enable;};std::vector<Call> calls;
void pg(void*,bool e,uint32_t t){calls.push_back({0,t,e});}
void ddi(void*,bool e,uint32_t t){calls.push_back({1,t,e});}
template<class...T>void tglTransitionRecord(T...){}
template<class...T>void tglObserveActivation(T...){}
template<class T>T &at(void*p,unsigned o){return *reinterpret_cast<T*>(static_cast<uint8_t*>(p)+o);}
FUNCTIONS
int main(){
 alignas(8) uint8_t ctl[0xF00]{},power[0x80]{},access[0x70]{};
 state.tglObservationController=ctl;state.adlsWiring.valid=true;state.adlsWiring.hdmi=8;
 state.orgADLSPowerPG=reinterpret_cast<uintptr_t>(pg);state.orgTGLPowerDDIState=reinterpret_cast<uintptr_t>(ddi);
 at<void*>(power,0x10)=ctl;at<void*>(ctl,0xC8)=power;at<void*>(power,0x78)=access;at<void*>(ctl,0xC40)=access;
 assert(XE::adlsPowerTarget(power)==power);assert(!XE::adlsPowerTarget(nullptr));unsigned count=0;
 for(bool dp:{false,true})for(unsigned activeDP:{0U,1U,7U})for(unsigned active:{0U,1U,7U})for(unsigned mask=0;mask<256;++mask)for(bool on:{false,true}){
  state.adlsWiring.dp=dp?16:0;at<uint32_t>(power,0x40)=activeDP;at<uint32_t>(power,0x3C)=active;calls.clear();XE::wrapADLSPowerPG(power,on,mask);
  assert(calls.size()==1&&calls[0].target==(mask|((active || (dp && activeDP))?0x2A:0))&&calls[0].enable==on);
  assert(at<uint32_t>(power,0x3C)==active);++count;
 }
 state.adlsWiring.valid=true;state.adlsWiring.dp=16;state.adlsWiring.pin[4]=10;
 for(unsigned bits=0;bits<16;++bits)for(unsigned tc:{0U,0x80U,0x200U,0x280U}){
  unsigned expected=0;for(unsigned i=0;i<4;++i){at<uint32_t>(power,0x20+4*i)=(bits>>i)&1;if((bits>>i)&1)expected|=2U<<(2*i);}
  uint8_t before[sizeof(power)];memcpy(before,power,sizeof(power));calls.clear();XE::wrapTGLPowerDDIState(power,false,tc);
  assert(calls.size()==2);assert(calls[tc?0:1].kind==0&&calls[tc?0:1].target==(expected|(tc?0x2A:0)));
  assert(calls[tc?0:1].enable==bool(tc));assert(calls[tc?1:0].kind==1&&calls[tc?1:0].target==tc);
  assert(!memcmp(before,power,sizeof(power)));++count;
 }
 // A DP-only route on TC1 must own the same parents as HDMI there.
 // Unwired consumers and physical A must not acquire TC lane parents.
 for(unsigned selected:{1u,8u,16u,9u,17u,24u,25u})for(unsigned live=0;live<32;++live) {
  state.adlsWiring.hdmi=0;state.adlsWiring.dp=selected;unsigned expected=0;
  for(unsigned d=0;d<5;++d){at<uint32_t>(power,0x30+4*d)=(live>>d)&1;
   if(d>=3 && (selected&live&(1U<<d)))expected=0x2A;}
  calls.clear();XE::wrapADLSPowerPG(power,true,2);
  assert(calls.size()==1 && calls[0].target==(expected|2));++count;
 }
 // Request generation is corrected in the common donor producer. This
 // backend owns only lane parents and must preserve every supplied DDI bit.
 for(unsigned refsC:{0U,1U,7U})for(unsigned refsTC1:{0U,1U,7U})
 for(bool on:{false,true})for(unsigned mask:{0x20U,0x80U,0xA0U,0xA2U,0x280U,0x2A0U,0x2A2U}){
  state.adlsWiring.hdmi=8;state.adlsWiring.dp=17;
  at<uint32_t>(power,0x38)=refsC;at<uint32_t>(power,0x3C)=refsTC1;
  uint8_t before[sizeof(power)];memcpy(before,power,sizeof(power));
  calls.clear();XE::wrapTGLPowerDDIState(power,on,mask);
  unsigned expected=mask;
  unsigned seen=0;for(auto c:calls)if(c.kind==1){assert(c.target==expected && c.enable==on);++seen;}
  assert(seen==1 && !memcmp(before,power,sizeof(power)));++count;
 }
 for(unsigned bad=0;bad<6;++bad){
  state.adlsDisplaySelected=bad!=0;state.adlsPowerReady=bad!=1;state.adlsWiring.valid=bad!=2;
  state.adlsWiring.hdmi=bad==3?0:8;state.adlsWiring.dp=0;at<void*>(ctl,0xC8)=bad==4?nullptr:power;at<void*>(power,0x78)=bad==5?nullptr:access;
  assert(!XE::adlsPowerTarget(power));calls.clear();XE::wrapADLSPowerPG(power,false,2);assert(calls.size()==1&&calls[0].target==2);
  at<uint32_t>(power,0x38)=0;at<uint32_t>(power,0x3C)=1;
  calls.clear();XE::wrapTGLPowerDDIState(power,false,0xA0);assert(calls.size()==1&&calls[0].kind==1&&calls[0].target==0xA0);
 }
 printf("PASS %u lane/pipe conditions; acquire/release order; unchanged refs; 6 owner/profile rejections\n",count);
}
'''.replace('FUNCTIONS',s[a:b])
with tempfile.TemporaryDirectory(prefix='adls-lane-power-') as d:
 d=Path(d);(d/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(d/'test.cpp'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
