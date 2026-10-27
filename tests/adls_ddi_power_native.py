#!/usr/bin/env python3
"""Replay exact donor DDI accounting and ACK polling through the production wrapper."""
from pathlib import Path
import hashlib, subprocess, sys, tempfile
r=Path(__file__).resolve().parents[1]
b=Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(b).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
s=(r/'WhateverXe/kern_xe.cpp').read_text()
a=s.index('uint8_t *XE::adlsPowerTarget');z=s.index('// Software-only transmitter boundary.',a)
fixture=(r/'tests/adls_ddi_power_native_fixture.cpp').read_text().replace('// PRODUCTION_WRAPPERS',s[a:z])
with tempfile.TemporaryDirectory(prefix='adls-ddi-native-') as d:
 d=Path(d);(d/'fixture.cpp').write_text(fixture)
 subprocess.run(['clang++','-arch','x86_64','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(r/'WhateverXe'),str(d/'fixture.cpp'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test'),str(Path(sys.argv[1]).resolve())],check=True)
