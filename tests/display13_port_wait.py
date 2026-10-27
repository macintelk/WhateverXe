#!/usr/bin/env python3
"""Real common installer/selector and exact native polling; MMIO/time are simulated."""
from pathlib import Path
import hashlib,re,subprocess,sys,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
functions='\n'.join(re.search(r'(?:bool|uint32_t|XELP::LinkRequirements) XE::'+n+r'\(.*?\n\}',s,re.S).group() for n in ['framebufferLinkRequirements','framebufferPortWaitPolls','processFramebufferPortWait'])
assert s.count('processFramebufferPortWait(patcher,index,address,size)')==1
runtime=re.search(r'bool XE::processFramebufferDonorRuntime\(.*?\n\}',s,re.S).group()
assert 'tglPanelPowerPropertyReady && meiReady && ddiRequestReady &&\n  processFramebufferPortWait' in runtime
assert 'while (delay < 3000' not in s
header=(r/'WhateverXe/kern_xe.hpp').read_text()
routing='\n'.join(re.search(pattern,header,re.S).group() for pattern in [
 r'bool tglHDMIEligible\(.*?\n',r'XELP::Display12SSTRoutes tglSSTRoutes\(.*?\n \}'])
h=(r/'tests/xelp_port_wait_harness.hpp').read_text().replace('// FUNCTIONS',functions).replace('// ROUTING',routing)
with tempfile.TemporaryDirectory(prefix='external-link-wait-') as d:
 p=Path(d)
 for native in [False]+([True] if len(sys.argv)>1 else []):
  if native:
   donor=Path(sys.argv[1]);assert hashlib.sha256(donor.read_bytes()).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
   code=(r/'tests/display13_port_wait_fixture.cpp').read_text().replace('// PRODUCTION_HARNESS',h)
  else:code=h+'\nint main(){checkInstallerAndSelection();}\n'
  (p/'test.cpp').write_text(code)
  subprocess.run(['clang++','-arch','x86_64','-std=c++17','-O1','-g','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')]+([str(donor)] if native else []),check=True)
