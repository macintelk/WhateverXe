#!/usr/bin/env python3
"""Replay production DP-alt FIA resolution and lane access on both FIA layouts."""
from pathlib import Path
import re, subprocess, tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'WhateverXe/kern_xe.cpp').read_text()
patterns=[r'bool XE::tglLegacyFia\(.*?\n\}',r'bool XE::tglDPAltFia\(.*?\n\}',r'uint32_t XE::wrapTGLConfiguredLanes\(.*?\n\}',r'void XE::wrapTGLFIALaneCount\(.*?\n\}',r'uint32_t XE::wrapTGLLaneMask\(.*?\n\}']
production='\n'.join(re.search(p,s,re.S).group() for p in patterns)
f=(r/'tests/tgl_dp_alt_fia_fixture.cpp').read_text().replace('// PRODUCTION',production)
with tempfile.TemporaryDirectory(prefix='tgl-dp-alt-fia-') as d:
 d=Path(d);(d/'check.cpp').write_text(f)
 subprocess.run(['clang++','-std=c++14','-O1','-g','-fsanitize=address,undefined','-I',str(r/'WhateverXe'),str(d/'check.cpp'),'-o',str(d/'check')],check=True)
 subprocess.run([str(d/'check')],check=True)
