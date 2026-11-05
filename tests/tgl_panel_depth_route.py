#!/usr/bin/env python3
"""Check the exact timing-copy boundary used by the panel depth route."""
from pathlib import Path
import re,sys,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'WhateverXe/kern_xe.cpp').read_text()
a=s.index('// The donor copies a complete timing')
b=s.index('// TGL/Xe-LP has no dedicated eDP transcoder',a)
install=s[a:b]
if len(sys.argv)>1:
 donor=Path(sys.argv[1]).read_bytes()
 for name,off in [('entry',0x50142),('call',0x4fa13)]:
  raw=bytes(int(v,0) for v in re.search(rf'{name}\[\]=\{{([^}}]+)',install).group(1).split(','))
  assert donor[off:off+len(raw)]==raw
 # The caller copies all 160 bytes, then maps the clamped depth at +0x8A.
 assert donor[0x4f9ff:0x4fa04]==bytes.fromhex('ba a0 00 00 00')
 assert donor[0x4fa54:0x4fa58]==bytes.fromhex('0f b7 75 82')
 assert donor[0x50179:0x50180]==bytes.fromhex('83 bb d8 3f 00 00 ff')
 assert donor[0x50190:0x50197]==bytes.fromhex('48 8d 83 08 4a 00 00')
 assert donor[0x501a0:0x501a7]==bytes.fromhex('48 8d 83 f0 49 00 00')
 assert donor[0x4fa25:0x4fa2c]==bytes.fromhex('49 8b 9f 48 36 00 00')
 print('PASS: exact donor timing-copy, depth-map and active-path ABI')
else:
 print('PASS: panel depth route source available; exact donor replay is optional')

# Execute the production owner/port guards, including the donor's unused RDI.
start=s.index('uint32_t XE::tglPanelDepthPort(')
end=s.index('uint32_t XE::wrapTGLPortDDC(',start)
code=r'''
#include "kern_tgl_panel_depth.hpp"
#include "kern_alderlake_s.hpp"
#include "kern_display13_fb_topology.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
using mach_vm_address_t=uintptr_t;

unsigned calls=0,published=0;uint64_t record[5];
struct Root {void setProperty(const char *,void *p,size_t n){assert(n==40);memcpy(record,p,n);++published;}} root;
struct IORegistryEntry {static Root *getRegistryRoot(){return &root;}};
void *tglAUXLock=&root;
void IOSimpleLockLock(void *){} void IOSimpleLockUnlock(void *){}
struct Ports {struct Port {bool internal(){return true;}bool dp(){return true;}} port[2];unsigned present=1;bool valid(){return true;}};
struct XE {
 bool display13FramebufferRequested=false;
 Display13OutputWiring adlnOutputWiring;void *adlnPlatform=nullptr;
 static void cachePanelDepth(void*,uint32_t,const uint8_t*,uint32_t);

 bool tglActive=true,tglPanelDepthReady=true,adlsDisplaySelected=false;
 AlderLakeS::Wiring adlsWiring;alignas(8) uint8_t tglPlatformCopy[0x1c0]{};
 void *tglObservationController=nullptr;
 struct {XELP::Family family=XELP::Family::TigerLake;} xeProfile;
 Ports firmwarePorts;mach_vm_address_t orgTGLModeTiming;
 static uint32_t tglPanelDepthPort(void *);
 static void wrapTGLModeTiming(void *,void *,void *);
} state,*callbackXE=&state;
struct TGLPanelDepthCache {void *port;uint32_t maximum;};
TGLPanelDepthCache tglPanelDepthCache[2];
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
PRODUCTION
void native(void *,void *,void *){++calls;}
template<class T> void put(uint8_t *p,unsigned off,T v){memcpy(p+off,&v,sizeof(v));}
int main(){
 alignas(8) uint8_t fb[0x4A20]{},ctl[0xC48]{},path[0x3660]{},port[0x600]{},timing[160]{};
 uint32_t cfg[4]{};void *access=ctl+8;
 state.tglObservationController=ctl;state.orgTGLModeTiming=reinterpret_cast<uintptr_t>(native);
 put(fb,0x1D0,static_cast<void *>(ctl));put(fb,0x3FD8,int32_t(-1));put(fb,0x49F0,static_cast<void *>(path));
 put(path,0x3648,static_cast<void *>(port));put(port,0x548,static_cast<void *>(cfg));
 put(port,0x578,access);put(ctl,0xC40,access);tglPanelDepthCache[0]={port,6};
 put(timing,0x88,uint16_t(1));put(timing,0x8A,uint16_t(2));
 // A stale/unspecified RDI is expected, not a reason to reject the valid FB.
 XE::wrapTGLModeTiming(nullptr,fb,timing);
 assert(calls==1 && published==1 && record[1]==6 && record[3]==2 && record[4]==1);
 for(unsigned test=0;test<7;++test){
  put(timing,0x8A,uint16_t(2));unsigned before=published;
  state.tglActive=test!=0;state.tglPanelDepthReady=test!=1;
  state.xeProfile.family=test==2?XELP::Family::Unknown:XELP::Family::TigerLake;
  cfg[2]=test==3?2:0;cfg[0]=test==4?2:0;port[0x117]=test==5;
  put(port,0x578,test==6?nullptr:access);
  XE::wrapTGLModeTiming(ctl,fb,timing);
  assert(published==before && *reinterpret_cast<uint16_t *>(timing+0x8A)==2);
 }
 // Same depth contract on qualified ADL-S panel; retained record is required.
 state.tglActive=state.tglPanelDepthReady=true;state.adlsDisplaySelected=true;
 state.xeProfile.family=XELP::Family::AlderLakeS;port[0x117]=0;put(port,0x578,access);
 state.adlsWiring.valid=true;state.adlsWiring.panel=1;
 const uint32_t panelRecord[]={0,0,0,0,2,0x18};memcpy(state.tglPlatformCopy+0x20,panelRecord,sizeof(panelRecord));
 put(port,0x548,static_cast<void*>(state.tglPlatformCopy+0x20));
 put(timing,0x8A,uint16_t(2));auto before=published;
 XE::wrapTGLModeTiming(nullptr,fb,timing);assert(published==before+1 && *reinterpret_cast<uint16_t*>(timing+0x8A)==1);
 state.adlsWiring.panel=0;put(timing,0x8A,uint16_t(2));before=published;
 XE::wrapTGLModeTiming(nullptr,fb,timing);assert(published==before && *reinterpret_cast<uint16_t*>(timing+0x8A)==2);
 puts("PASS: production owner/port guards and unused-controller argument");
}
'''.replace('PRODUCTION',s[start:end])
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
