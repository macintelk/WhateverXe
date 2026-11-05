#!/usr/bin/env python3
"""Exercise the production fatal-log storage under simultaneous/reentrant claims."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'WhateverXe/kern_xe.cpp').read_text()
a=source.index('struct TGLFatalLogStorage {')
b=source.index('static TGLFatalLogStorage tglFatalLogStorage;',a)
code='''#include <cstdint>
#include <cassert>
#include <atomic>
#include <thread>
#include <vector>
#include <cstring>
#include <cstdio>
'''+source[a:b]+'''
int main() {
 TGLFatalLogStorage log;
 std::atomic<unsigned> winners{0},ready{0};std::atomic<bool> start{false};
 std::vector<std::thread> threads;
 for(unsigned i=0;i<16;++i)threads.emplace_back([&] {
  ++ready;while(!start.load())std::this_thread::yield();
  if(log.claim()) {
   ++winners;
   for(char c:log.bytes.csb.trail)assert(c==0);
   memset(log.bytes.csb.trail,'T',sizeof(log.bytes.csb.trail));
   memset(log.bytes.csb.vm,'V',sizeof(log.bytes.csb.vm));
   memset(log.bytes.csb.loads,'L',sizeof(log.bytes.csb.loads));
   assert(!log.claim()); // Same-thread reentry must never overwrite/wait.
  }
 });
 while(ready.load()!=16)std::this_thread::yield();start=true;
 for(auto &thread:threads)thread.join();
 assert(winners==1 && !log.claim());
 for(char c:log.bytes.csb.trail)assert(c=='T');
 for(char c:log.bytes.csb.vm)assert(c=='V');
 for(char c:log.bytes.csb.loads)assert(c=='L');
 TGLFatalLogStorage guc;assert(guc.claim());
 for(char c:guc.bytes.guc.detail)assert(c==0);
 for(char c:guc.bytes.guc.boundary)assert(c==0);
 for(char c:guc.bytes.guc.preStart)assert(c==0);
 static_assert(sizeof(log.bytes.csb)==4600 && sizeof(log.bytes.guc)==3440,"preserve diagnostic capacity");
 puts("PASS fatal storage: one of 16 concurrent owners, no reentry, preserved capacities and first record");
}
'''
with tempfile.TemporaryDirectory(prefix='wex-fatal-storage-') as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-pthread',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
