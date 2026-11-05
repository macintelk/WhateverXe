#!/usr/bin/env python3
"""Check that built Compat bundles still accept the shipped framebuffer ABI."""
import argparse
import plistlib
import re
from pathlib import Path


def version(value):
    if not isinstance(value, str) or not re.fullmatch(r'\d+\.\d+\.\d+', value):
        raise ValueError(f'unsupported Compat version: {value!r}')
    return tuple(map(int, value.split('.')))


def check(path):
    info = plistlib.loads(path.read_bytes())
    if info['CFBundleIdentifier'] != 'org.b00t0x.AppleIntelTGLCompat':
        raise ValueError('wrong Compat bundle identifier')
    # Existing Graphics framebuffer payloads request this ABI independently of
    # the provider's release version. Changing it requires coordinated clients.
    required = (0, 5, 0)
    if not version(info['OSBundleCompatibleVersion']) <= required <= version(info['CFBundleVersion']):
        raise ValueError('Compat no longer accepts the shipped framebuffer dependency 0.5.0')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('builds', type=Path, nargs='+')
    args = parser.parse_args()
    for build in args.builds:
        check(build / 'AppleIntelTGLCompat.kext/Contents/Info.plist')
    print('PASS: built Compat bundles accept the shipped framebuffer dependency')
