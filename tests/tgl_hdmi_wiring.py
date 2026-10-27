#!/usr/bin/env python3
"""Test the actual HDMI loader/topology/GMBUS/lane entry points with IOKit mocks."""
from pathlib import Path
import hashlib,re,subprocess,sys,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text();h=(r/'WhateverXe/kern_xe.hpp').read_text()
fields=h[h.index(' TGLHDMIWiring tglHDMIWiring;'):h.index('\tbool tglGMBusReady')]
functions='\n'.join(re.search(p,s,re.S).group() for p in [r'bool XE::tglLoadHDMIWiring\(.*?\n\}',r'bool XE::wrapTGLGetOSInformation\(.*?\n\}',r'uint32_t XE::wrapTGLGMBus\(.*?\n\}',r'bool XE::wrapFramebufferComputeLanes\(.*?\n\}',r'bool XE::framebufferHDMIEligible\(.*?\n\}'])
observations=s[s.index('struct TGLGMBusObservation {'):s.index('struct TGLMEIRecord {')]
code=(r/'tests/tgl_hdmi_wiring_fixture.cpp').read_text().replace('// FIELDS',fields).replace('// OBSERVATIONS',observations).replace('// FUNCTIONS',functions)
with tempfile.TemporaryDirectory(prefix='tgl-hdmi-wiring-') as d:
 d=Path(d);(d/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++14','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(r/'WhateverXe'),str(d/'test.cpp'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)

if len(sys.argv)>1:
 donor=Path(sys.argv[1]).read_bytes()
 assert hashlib.sha256(donor).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
 # All three direct callers found in exact-donor disassembly. Only the
 # 0x304f8 validator is admitted to the HDMI correction/config reader.
 for at in [0x1fc07,0x304f3,0x3c4cf]:
  assert donor[at]==0xe8
  assert at+5+int.from_bytes(donor[at+1:at+5],'little',signed=True)==0x1fc22
 # At setupOptimalLaneCount, arg0 is the output pointer, not the object.
 assert donor[0x1fbfd:0x1fc07]==bytes.fromhex('4881c7240100004989f8')
 # The callee overwrites edi without reading arg0; it writes the output r8.
 assert donor[0x1fc2f:0x1fc38]==bytes.fromhex('89d741c70000000000')
 print('PASS exact donor lane callers: interior output pointer vs HDMI validator')
