#!/usr/bin/env python3
"""Compile the actual power/boot MMIO adapters with the DP bootstrap helpers."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'WhateverXe/kern_xe.cpp').read_text()
start=s.index('struct Display13FBPowerIO {')
end=s.index('struct Display13FBTCIO',start)
# This fixture exercises Combo boot/power paths; TC AUX uses its own core tests.
stubs='\nstatic bool display13TCAuxSupported(){return false;}\nstatic bool display13TCPrepareAUX(IOMemoryMap *,unsigned){return false;}\nstatic bool display13TCHealthyAUX(IOMemoryMap *,unsigned){return false;}\n'
code=(r/'tests/display13_boot_aux_fixture.cpp').read_text().replace('// PRODUCTION_IO',stubs+s[start:end])
with tempfile.TemporaryDirectory(prefix='display13-boot-aux-') as d:
    p=Path(d);(p/'test.cpp').write_text(code)
    subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
