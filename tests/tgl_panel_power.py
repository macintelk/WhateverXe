#!/usr/bin/env python3
"""Execute the production guarded metadata replacement with null/non-null FBs."""
from pathlib import Path
import re, subprocess, tempfile, sys
r=Path(__file__).resolve().parents[1]
s=(r/'WhateverXe/kern_xe.cpp').read_text()
begin=s.index('\t\t\t\t// Both power-on and power-off converge')
end=s.index('\n }\n // DONOR_PANEL_PROPERTY_END',begin)
install=s[begin:end]
wrapper=re.search(r'bool XE::wrapTGLPanelPowerProperty\(.*?\n\}',s,re.S).group()
if len(sys.argv)>1:
    # Native donor and locally re-signed variants have identical text offsets.
    b=Path(sys.argv[1]).read_bytes()
    values=re.search(r'expected\[\] \{([^}]+)\}',install).group(1)
    expected=bytes(int(x,0) for x in values.split(','))
    assert b[0x48aac:0x48abc]==expected
    assert b[0xa65d3:0xa65ea]==b'AAPL,LCD-PowerState-ON\0'
    # On and off paths converge at the replacement; native PPS writes remain.
    assert b[0x48ad2:0x48ad4]==bytes.fromhex('eb d2')
code=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <sys/mman.h>
using OSObject=unsigned;
using mach_vm_address_t=uintptr_t;
struct IORegistryEntry {
 unsigned calls=0; OSObject *last=nullptr; bool result=true;
 bool setProperty(const char *key,OSObject *value) {
  assert(!strcmp(key,"AAPL,LCD-PowerState-ON"));
  ++calls;last=value;return result;
 }
 void setProperty(const char *,bool) {}
 void setProperty(const char *,unsigned,unsigned) {}
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry root;return &root;}
};
struct KernelPatcher {
 enum class Error {NoError,Failed}; bool fail=false; unsigned writes=0; uintptr_t delta=0;
 uintptr_t solveSymbol(unsigned,const char *,uintptr_t a,size_t){return a+0x48652+delta;}
 void routeBlock(uintptr_t a,const uint8_t *b,size_t n){if(!fail){memcpy((void*)a,b,n);++writes;}}
 Error getError(){return fail?Error::Failed:Error::NoError;}
 void clearError(){}
};
struct XE {
 bool tglPanelPowerPropertyReady=false;
 static bool wrapTGLPanelPowerProperty(IORegistryEntry *,const char *,OSObject *);
 void install(KernelPatcher &patcher,unsigned index,uintptr_t address,size_t size) {
 INSTALL
 }
};
WRAPPER
int main(){
 const size_t n=0x49000;
 auto b=(uint8_t*)mmap(nullptr,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
 assert(b!=MAP_FAILED);
 const uint8_t original[]={ORIGINAL};
 auto site=b+0x48aac;
 memcpy(site,original,16);
 for(unsigned scenario=0;scenario<4;++scenario){
  XE xe;KernelPatcher p;
  size_t size=n;
  if(scenario==0)site[0]^=1;
  if(scenario==1)size=0x48abb;
  if(scenario==2)p.delta=1;
  if(scenario==3)p.fail=true;
  xe.install(p,0,(uintptr_t)b,size);
  assert(!xe.tglPanelPowerPropertyReady && p.writes==0);
  memcpy(site,original,16);
 }
 XE xe;KernelPatcher p;xe.install(p,0,(uintptr_t)b,n);
 assert(xe.tglPanelPowerPropertyReady&&p.writes==1);
 // Execute the exact replacement with the same call-site stack alignment.
 // rsi deliberately isn't the property key: the removed LEA supplied it.
 site[-1]=0x55;site[16]=0x5d;site[17]=0xc3;
 assert(mprotect(b,n,PROT_READ|PROT_EXEC)==0);
 auto call=reinterpret_cast<bool(*)(IORegistryEntry*,const char*,OSObject*)>(site-1);
 IORegistryEntry fb;OSObject on=1,off=0;
 assert(!call(nullptr,(const char*)0xc7200,&on));
 assert(call(&fb,(const char*)0xc7200,&on)&&fb.calls==1&&fb.last==&on);
 assert(call(&fb,nullptr,&off)&&fb.calls==2&&fb.last==&off);
 fb.result=false;assert(!call(&fb,nullptr,&on)&&fb.calls==3);
 munmap(b,n);
}
'''.replace('INSTALL',install).replace('WRAPPER',wrapper)
values=re.search(r'expected\[\] \{([^}]+)\}',install).group(1)
code=code.replace('ORIGINAL',values)
with tempfile.TemporaryDirectory(prefix='tgl-panel-power-') as d:
    d=Path(d);(d/'test.cpp').write_text(code)
    subprocess.run(['clang++','-arch','x86_64','-std=c++17','-O1','-g',
                    str(d/'test.cpp'),'-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True)
print('PASS: exact guarded call-site execution; null/on/off/property failure; bad bytes/size/symbol/write rejected')
