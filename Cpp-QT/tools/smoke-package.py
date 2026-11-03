#!/usr/bin/env python3
"""Run a packaged binary with temporary data and no Conda paths in its environment."""
import os
import plistlib
from pathlib import Path
import subprocess
import sys
import tempfile
import time

def native_windows_smoke(gui, env):
    # Exercise the shipped executable's default backend, not the offscreen
    # plugin. Catch a return of the hundreds-of-MB font database allocation.
    import ctypes
    from ctypes import wintypes
    class Counters(ctypes.Structure):
        _fields_=[('cb',wintypes.DWORD),('PageFaultCount',wintypes.DWORD)]+[
            (name,ctypes.c_size_t) for name in (
                'PeakWorkingSetSize','WorkingSetSize','QuotaPeakPagedPoolUsage',
                'QuotaPagedPoolUsage','QuotaPeakNonPagedPoolUsage',
                'QuotaNonPagedPoolUsage','PagefileUsage','PeakPagefileUsage','PrivateUsage')]
    get_memory=ctypes.WinDLL('psapi',use_last_error=True).GetProcessMemoryInfo
    get_memory.argtypes=[wintypes.HANDLE,ctypes.POINTER(Counters),wintypes.DWORD]
    get_memory.restype=wintypes.BOOL
    native_env=env.copy()
    native_env.pop('QT_QPA_PLATFORM',None)
    with tempfile.TemporaryDirectory(prefix='mydict-native-packaged-') as data:
        process=subprocess.Popen([str(gui),'--data-dir',data,'--smoke-test'],env=native_env)
        peak_private=peak_working=0
        deadline=time.monotonic()+30
        try:
            while process.poll() is None:
                counters=Counters();counters.cb=ctypes.sizeof(counters)
                if not get_memory(int(process._handle),ctypes.byref(counters),ctypes.sizeof(counters)):
                    if process.poll() is not None:break
                    raise ctypes.WinError(ctypes.get_last_error())
                peak_private=max(peak_private,counters.PrivateUsage)
                peak_working=max(peak_working,counters.WorkingSetSize)
                if time.monotonic()>deadline:raise TimeoutError('Native packaged GUI startup timed out')
                time.sleep(0.05)
            assert process.returncode==0, f'Native packaged GUI exited with {process.returncode}'
            assert list((Path(data)/'profiles').rglob('words-shared.sqlite')), 'Native GUI did not initialize SQLite'
            assert peak_private>0, 'Native GUI memory was not sampled'
            print(f'Native Windows startup: peak private {peak_private/1048576:.1f} MB; working set {peak_working/1048576:.1f} MB')
            assert peak_private<384*1048576, 'Native startup exceeded the 384 MB regression budget'
        finally:
            if process.poll() is None:process.kill();process.wait()

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
if sys.platform=='win32':
    native_windows_smoke(gui,env)
