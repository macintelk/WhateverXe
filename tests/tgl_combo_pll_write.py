#!/usr/bin/env python3
"""Exercise the production CFGCR0 write guard, with optional exact donor checks."""
from pathlib import Path
import re, subprocess, sys, tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'WhateverXe/kern_xe.cpp').read_text()
start=s.index('void XE::wrapTGLRegisterWrite(')
guard=s[s.index('\n',start)+1:s.index('    comboClockTranslateWrite(',start)]
guard=guard.replace('reinterpret_cast<uintptr_t>(__builtin_return_address(0))','caller')
install=s[s.index('// Qualify the reference-clock field producer'):s.index('if (tglHDMIB)',s.index('// Qualify the reference-clock field producer'))]
if len(sys.argv)>1:
 b=Path(sys.argv[1]).read_bytes()
 for name,offset in [('reference',0x6c658),('write',0x6d95d)]:
  text=re.search(rf'{name}\[\] \{{([^}}]+)\}}',install).group(1)
  expected=bytes(int(x,0) for x in text.split(','))
  assert b[offset:offset+len(expected)]==expected
code=r'''
#include "kern_tgl_port_map.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
struct State { bool adlsDisplaySelected=false; bool tglActive=true; uintptr_t tglComboPLLFractionCaller=0x1000; void *tglObservationController=nullptr; } state,*callbackXE=&state;
unsigned records=0;
void tglTransitionRecord(unsigned stage,bool done,void *,unsigned long reg,uint32_t nominal,uint32_t actual,const uint64_t *detail,unsigned size) {
 assert(stage==70 && done && reg==0x164284 && nominal==0x1c001a5 && actual==0xe001a5);
 assert(size==16 && detail[0]==2 && detail[1]==nominal); ++records;
}
uint32_t run(void *access,unsigned long reg,uint32_t value,uintptr_t caller) {
GUARD
 return value;
}
int main(){
 alignas(8) uint8_t controller[0xF00]{}; void *access=controller+8;
 state.tglObservationController=controller;
 memcpy(controller+0xC40,&access,sizeof(access));
 uint32_t ref=2;memcpy(controller+0xEA8,&ref,4);
 for(unsigned i=0;i<7;++i){
  state.tglActive=i!=0;state.tglComboPLLFractionCaller=i==1?0:0x1000;
  state.tglObservationController=i==2?nullptr:controller;
  ref=i==3?1:2;memcpy(controller+0xEA8,&ref,4);
  assert(run(i==4?nullptr:access,i==5?0x164288:0x164284,0x1c001a5,i==6?0x1001:0x1000)==0x1c001a5);
  assert(records==0);
 }
 state.tglActive=true;state.tglComboPLLFractionCaller=0x1000;state.tglObservationController=controller;
 ref=2;memcpy(controller+0xEA8,&ref,4);
 assert(run(access,0x164284,0x1c001a5,0x1000)==0xe001a5 && records==1);
 puts("PASS: production caller/controller/reference/target guards and applied-value record");
}
'''.replace('GUARD',guard)
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
