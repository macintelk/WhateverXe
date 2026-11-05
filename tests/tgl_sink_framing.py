#!/usr/bin/env python3
"""Production capability clamp: preserve both endpoints' negotiated framing."""
from pathlib import Path
import re,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'WhateverXe/kern_xe.cpp').read_text()
start=s.index('uint32_t XE::wrapTGLPortTraining(')
guard=s[s.index('\n',start)+1:s.index(' tglAUXRecord(port,10,false',start)]
install=s[s.index('// getDPCDInfo stores MAX_LANE_COUNT'):s.index('\n\t\t\t}',s.index('// getDPCDInfo stores MAX_LANE_COUNT'))]
if len(sys.argv)>1:
 b=Path(sys.argv[1]).read_bytes()
 for name,offset in [('capability',0x1e328),('selection',0x1b607),('alternateSR',0x1bc73)]:
  text=re.search(rf'{name}\[\] \{{([^}}]+)\}}',install).group(1)
  expected=bytes(int(x,0) for x in text.split(','));assert b[offset:offset+len(expected)]==expected
code=r'''
#include "kern_xelp_sst_route.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
struct State {
 bool tglActive=true,tglSinkFramingReady=true,eligible=true;
 void *tglObservationController=nullptr;
 XELP::Display12SSTRoutes tglSSTRoutes(){return kind==3?XELP::Display12SSTRoutes{3,0,0}:kind==2?XELP::Display12SSTRoutes{0,0,8}:XELP::Display12SSTRoutes{0,8,0};}
 unsigned kind=0;
} state,*callbackXE=&state;
unsigned records;
void tglTransitionRecord(unsigned stage,bool done,void *,uint32_t ddi,uint64_t before,uint8_t after,const uint64_t *d,unsigned size){
 assert(stage==71 && done && size==32 && d[0]==ddi && d[2]==before && after==(before & d[1]));++records;
}
void run(void *port){GUARD}
int main(){
 alignas(8) uint8_t p[0x600],ctl[0xD00];uint32_t config[4];
 for(unsigned kind=0;kind<4;++kind)for(unsigned cap=0;cap<3;++cap)for(unsigned requested=0;requested<3;++requested)
 for(unsigned bad=0;bad<10;++bad){
  memset(p,0,sizeof(p));memset(ctl,0,sizeof(ctl));memset(config,0,sizeof(config));
  state={};state.kind=kind;state.tglObservationController=ctl;
  config[0]=kind==3?1:kind?3:0;config[2]=kind?1:0;
  uint32_t *cp=config;memcpy(p+0x548,&cp,sizeof(cp));
  uint32_t backend=kind==3?1:kind==2?3:(kind?4:1);memcpy(p+0x550,&backend,4);
  void *access=ctl+8;memcpy(p+0x578,&access,sizeof(access));memcpy(ctl+0xC40,&access,sizeof(access));
  p[0x115]=1;p[0x10]=cap;p[0x11A]=requested;p[0xC]=0x11;
  if(bad==1)state.tglActive=false;
  if(bad==2)state.tglSinkFramingReady=false;
  if(bad==3)state.tglObservationController=nullptr;
  if(bad==4)p[0x115]=0;
  if(bad==5)p[0x117]=1;
  if(bad==6)config[2]=2;
  if(bad==7){access=nullptr;memcpy(p+0x578,&access,sizeof(access));}
  if(bad==8){access=p;memcpy(p+0x578,&access,sizeof(access));}
  if(bad==9){cp=nullptr;memcpy(p+0x548,&cp,sizeof(cp));}
  records=0;run(p);
  const bool valid=bad==0 && cap<2 && requested<2;
  assert(records==valid);assert(p[0x11A]==(valid?(requested&cap):requested));
 }
 run(nullptr);
 puts("PASS: production framing clamp, capable/incapable/unknown sinks, combo/TC, ownership and state guards");
}
'''.replace('GUARD',guard)
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
