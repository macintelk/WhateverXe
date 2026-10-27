#!/usr/bin/env python3
"""Run the production Display13 path/loop/return adapters with fault injection.

Optional exact c004fc1a replay preserves free/AGDC control flow. MMIO, memory
allocator and native path disposal are leaves; this does not prove hardware.
"""
from pathlib import Path
import hashlib,re,subprocess,sys,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'WhateverXe/kern_xe.cpp').read_text()
names=['uint32_t XE::releaseDisplay13Path(', 'uint32_t XE::retireDisplay13Paths(',
       'uint32_t XE::wrapDisplay13FreeResources(', 'bool XE::display13RetirementHealthy(',
       'bool XE::wrapDisplay13AllocateResources(', 'bool XE::retireDisplay13Controller(']
functions='\n'.join(re.search(re.escape(n)+r'.*?\n\}',s,re.S).group() for n in names)
code=(r/'tests/display13_retirement_fixture.cpp').read_text().replace('// FUNCTIONS',functions)
# Keep the tested adapter connected at the real memory-return boundary and
# at both allocation entrances; declarations alone cannot satisfy this check.
assert 'return freed;' in s[s.index('bool XE::adlnFreeStoppedPipe('):s.index('uint32_t XE::releaseDisplay13Path(')]
assert 'if(!retireDisplay13Controller(controller)){blocked(3);return;}' in s
assert 'if(!display13RetirementHealthy())return false;' in s[s.index('bool XE::wrapTGLAllocatePathResources('):]
install=s[s.index('bool XE::processDisplay13Retirement('):s.index('bool XE::display13RetirementHealthy(')]
assert 'freeAll,wrapDisplay13FreeResources,orgDisplay13FreeResources' in install
assert 'allocate,wrapDisplay13AllocateResources,orgDisplay13AllocateResources' in install
assert 'processDisplay13Retirement(patcher,index,address,size)' in s
hooks=''
if len(sys.argv)>1:
 donor=Path(sys.argv[1]).resolve();b=donor.read_bytes()
 assert hashlib.sha256(b).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
 symbols={line.split()[-1]:int(line.split()[0],16) for line in subprocess.check_output(['nm','-n',str(donor)],text=True).splitlines() if len(line.split())==3 and re.fullmatch('[0-9a-fA-F]+',line.split()[0])}
 for site,target in [(0x6780b,0x67e8e),(0x6ba3a,0x67e8e)]:
  assert b[site]==0xe8 and site+5+int.from_bytes(b[site+1:site+5],'little',signed=True)==target
 # Native allocation failure still returns false after cleanup; no new success.
 assert b[0x67810:0x67816]==bytes.fromhex('4531f64489f0')
 for symbol,fn in {
 '__ZN21AppleIntelFramebuffer16StopTransactionsEv':'transactions',
 '__ZN21AppleIntelFramebuffer14disableDisplayEb':'disable',
 '__ZN21AppleIntelFramebuffer17freeScanoutMemoryEv':'post',
 '__ZN21AppleIntelFramebuffer25cleanupLogicalFramebufferEv':'post',
 '__ZN21AppleIntelFramebuffer21setPortNumberPropertyEv':'post'}.items():
  hooks+='hook(image+'+hex(symbols[symbol])+',(uintptr_t)'+fn+');\n'
 code=code.replace('// NATIVE_HOOKS',hooks)
with tempfile.TemporaryDirectory(prefix='d13-retirement-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 cmd=['clang++','-std=c++17','-O0','-g','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')]
 if len(sys.argv)==1:cmd+=['-fsanitize=address,undefined','-fno-sanitize-recover=all']
 subprocess.run(cmd,check=True)
 subprocess.run([str(p/'test'),*([str(donor)] if len(sys.argv)>1 else [])],check=True)
