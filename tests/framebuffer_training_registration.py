#!/usr/bin/env python3
"""Replay the production common installer; hardware eligibility is separate."""
from pathlib import Path
import re,subprocess,tempfile,sys
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
f=re.search(r'bool XE::processFramebufferTraining\(.*?\n\}',s,re.S).group()
# Single owner for the native protocol routes and pool lock. Hardware adapters
# may set lease eligibility, but must not re-install the common protocol.
wrappers=re.findall(r'\{entries\[\w+\].symbol,(wrap\w+),',f)
assert len(wrappers)==9
for w in wrappers:
 assert len(re.findall(r'(?:,|\s)'+w+r',org',s))==1,w
assert s.count('tglPathPoolLock=IORecursiveLockAlloc()')==1
assert s.count('processFramebufferTraining(patcher,index,address,size)')==1
originals=re.findall(r',(?:wrap\w+),(org\w+)\}',f)+['orgTGLPathLookup','tglPortByDDI']
code=r'''
#include "kern_framebuffer_training_native.hpp"
#include <cassert>
#include <cstring>
#include <vector>
#include <fstream>
#include <iterator>
#include <cstdio>
using mach_vm_address_t=uintptr_t;
#define arrsize(x) (sizeof(x)/sizeof((x)[0]))
unsigned lockCalls;bool failLock;
void *IORecursiveLockAlloc(){++lockCalls;return failLock?nullptr:(void*)1;}
struct KernelPatcher {
 struct RouteRequest {const char *symbol;uintptr_t *original;
  template<class F>RouteRequest(const char*s,F,uintptr_t&o):symbol(s),original(&o){}};
 unsigned routed=0,failRoute=0;const char *badSymbol=nullptr;
 template<class T>T solveSymbol(size_t,const char *s,uintptr_t a,size_t){
  for(const auto&e:FramebufferTrainingNative::entries)if(!strcmp(s,e.symbol))return a+e.offset+(badSymbol==s?1:0);
  assert(false);return 0;
 }
 void clearError(){}
 bool routeMultipleShort(size_t,RouteRequest *rs,unsigned n,uintptr_t a,size_t size){
  for(unsigned i=0;i<n;++i){if(++routed==failRoute)return false;
   *rs[i].original=solveSymbol<uintptr_t>(0,rs[i].symbol,a,size);
   *reinterpret_cast<uint8_t *>(*rs[i].original)=0xcc;
  }return true;
 }
};
struct XE {
 static constexpr unsigned TGLPortCount=9;
 bool display13FramebufferRequested=false;void *tglPathPoolLock=nullptr;
 struct {unsigned ddi=99;} tglTrainingLeases[TGLPortCount];
 ORIGINALS
 WRAPPERS
 bool processFramebufferTraining(KernelPatcher&,size_t,uintptr_t,size_t);
};
FUNCTION
int main(int argc,char **argv){
 using namespace FramebufferTrainingNative;
 std::vector<uint8_t> image(0x150000);
 if(argc>1){std::ifstream f(argv[1],std::ios::binary);image.assign(std::istreambuf_iterator<char>(f),{});assert(image.size()>0x91da0);}
 for(const auto&e:entries){if(argc>1)assert(!memcmp(image.data()+e.offset,e.prefix,16));else memcpy(image.data()+e.offset,e.prefix,16);}
 for(const auto&t:spans){if(argc>1)assert(!memcmp(image.data()+t.offset,t.bytes,t.length));else memcpy(image.data()+t.offset,t.bytes,t.length);}
 for(bool d13:{false,true}) {
  for(unsigned failure=0;failure<=11;++failure){
   auto b=image;XE x;x.display13FramebufferRequested=d13;KernelPatcher p;lockCalls=0;failLock=failure==11;
   if(failure<=10)p.failRoute=failure;
   bool success=x.processFramebufferTraining(p,0,(uintptr_t)b.data(),b.size());
   assert(success==(failure==0||(d13&&failure==10)));
   assert(lockCalls==1);if(failLock)assert(p.routed==0);
   if(success){assert(p.routed==(d13?9U:10U));for(unsigned i=0;i<9;++i)assert(x.tglTrainingLeases[i].ddi==i);}
  }
  for(const auto&e:entries){
   auto b=image;XE x;x.display13FramebufferRequested=d13;KernelPatcher p;lockCalls=0;failLock=false;
   b[e.offset]^=1;assert(!x.processFramebufferTraining(p,0,(uintptr_t)b.data(),b.size()));assert(!lockCalls&&!p.routed);
  }
  for(const auto&t:spans){
   auto b=image;XE x;x.display13FramebufferRequested=d13;KernelPatcher p;lockCalls=0;
   b[t.offset]^=1;assert(!x.processFramebufferTraining(p,0,(uintptr_t)b.data(),b.size()));assert(!lockCalls&&!p.routed);
  }
 }
 puts("PASS shared training registration: both backends, exact entries/spans, partial routes and lock failure");
}
'''.replace('FUNCTION',f).replace('ORIGINALS','uintptr_t '+','.join(n+'=0' for n in originals)+';').replace('WRAPPERS','\n'.join('static void '+w+'(){}' for w in wrappers+['wrapTGLPathLookup']))
with tempfile.TemporaryDirectory(prefix='wex-training-registration-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')]+sys.argv[1:],check=True)
