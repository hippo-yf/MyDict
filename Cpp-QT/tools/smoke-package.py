#!/usr/bin/env python3
"""Run a packaged binary with temporary data and no Conda paths in its environment."""
import os
import plistlib
from pathlib import Path
import subprocess
import sys
import tempfile

dist=Path(sys.argv[1]).resolve()
stage=next(p for p in dist.iterdir() if p.is_dir() and p.name.startswith('MyDictQt-v'))
if sys.platform=='win32':
    gui=stage/'bin/MyDictQt.exe'
elif sys.platform=='darwin':
    gui=stage/'MyDictQt.app/Contents/MacOS/MyDictQt'
    bundle=stage/'MyDictQt.app/Contents'
    with (bundle/'Info.plist').open('rb') as file:
        info=plistlib.load(file)
    icon=info.get('CFBundleIconFile','')
    assert icon and (bundle/'Resources'/icon).exists(), 'macOS application icon is missing'
else:
    gui=stage/'MyDictQt'
env=os.environ.copy()
for key in list(env):
    if key.startswith(('CONDA','QT_PLUGIN','QT_QPA_PLATFORM_PLUGIN','LD_LIBRARY_PATH','DYLD_')):
        env.pop(key,None)
if sys.platform=='win32':
    env['PATH']=os.environ.get('SystemRoot','C:\\Windows')+'\\System32;'+os.environ.get('SystemRoot','C:\\Windows')
else:
    env['PATH']='/usr/bin:/bin:/usr/sbin:/sbin'
env['QT_QPA_PLATFORM']='offscreen'
with tempfile.TemporaryDirectory(prefix='mydict-packaged-') as data:
    subprocess.run([str(gui),'--data-dir',data,'--smoke-test'],env=env,check=True,timeout=30)
    settings=Path(data)/'settings.json'
    assert settings.exists(), 'packaged application did not initialize settings'
    databases=list((Path(data)/'profiles').rglob('words-shared.sqlite'))
    assert len(databases)==1, 'packaged application did not initialize SQLite'
print('Packaged GUI/SQLite smoke test passed without Conda activation.')
