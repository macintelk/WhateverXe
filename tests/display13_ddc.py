#!/usr/bin/env python3
"""Check the production TC pin guard and optional exact-donor pin-bound patch."""
from pathlib import Path
import hashlib,re,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'WhateverXe/kern_xe.cpp').read_text()
a=s.index('uint32_t XE::wrapDisplay13GMBus(')
guard=s[s.index('    if(bus>=8',a):s.index('    static uint32_t count',a)]
cpp=r'''
#include "kern_display13_output_wiring.hpp"
#include <cassert>
#include <cstdio>
struct IOService{};
constexpr unsigned kIOReturnBadArgument=0xe00002c2;
struct State {IOService *adlnTopologyController{};Display13OutputWiring adlnOutputWiring;}state,*callbackXE=&state;
unsigned request(IOService *controller,unsigned bus){GUARD return 0;}
int main(){
 IOService owner,other;state.adlnTopologyController=&owner;
 for(unsigned ddi=3;ddi<=6;++ddi) {
  auto &w=state.adlnOutputWiring;w={};w.valid=true;w.hdmi=1U<<ddi;w.ddc[ddi]=ddi+6;
  assert(w.described());
  for(unsigned bus=8;bus<=13;++bus){
   assert(request(&owner,bus)==(bus==ddi+6?0U:kIOReturnBadArgument));
   assert(request(&other,bus)==kIOReturnBadArgument);
  }
  w.valid=false;assert(request(&owner,ddi+6)==kIOReturnBadArgument);
 }
 for(unsigned bus=0;bus<8;++bus)assert(!request(&owner,bus));
 puts("PASS production Display13 TC DDC: selected pin, owner binding, invalid wiring and native legacy boundary");
}
'''.replace('GUARD',guard)
with tempfile.TemporaryDirectory(prefix='display13-ddc-') as tmp:
 d=Path(tmp);(d/'test.cpp').write_text(cpp)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(d/'test.cpp'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
if len(sys.argv)>1:
 donor=Path(sys.argv[1]).read_bytes()
 assert hashlib.sha256(donor).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
 def array(name):
  match=re.search(r'const uint8_t '+name+r'\[\] \{([^}]+)\}',s)
  return bytes(int(v.strip(),0) for v in match.group(1).split(','))
 before,after=array('pinLimit'),array('tcPinLimit')
 assert donor[0x53b66:0x53b66+len(before)]==before==bytes.fromhex('837dcc08722a')
 assert after==bytes.fromhex('837dcc0d722a')
 assert '{0x53B66,pinLimit,tcPinLimit,sizeof(pinLimit)}' in s
 print('PASS exact c004fc1a GMBUS pin-bound site: comparison 8 -> 13; branch and remaining bytes unchanged')
