#!/usr/bin/env python3
"""Package native Qt binaries. Copies only public assets and runtime dependencies."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def run(*args, env=None):
    subprocess.run([str(a) for a in args], check=True, env=env)

def pe_imports(path):
    """Read PE32/PE32+ normal and delay import DLL names, without extra modules."""
    data = path.read_bytes()
    if data[:2] != b'MZ':
        return []
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe+4] != b'PE\0\0':
        return []
    count, optional_size = struct.unpack_from('<H', data, pe+6)[0], struct.unpack_from('<H', data, pe+20)[0]
    opt = pe+24
    directory = opt + (112 if struct.unpack_from('<H', data, opt)[0] == 0x20b else 96)
    sections = []
    for i in range(count):
        offset = opt+optional_size+i*40
        vsize, rva, rawsize, raw = struct.unpack_from('<IIII', data, offset+8)
        sections.append((rva, max(vsize, rawsize), raw))
    def file_offset(rva):
        for base, size, raw in sections:
            if base <= rva < base+size:
                return raw+rva-base
        return rva
    def name(rva):
        at = file_offset(rva)
        return data[at:data.index(b'\0', at)].decode('ascii')
    result = []
    for index, stride, name_at in ((1,20,12), (13,32,4)):
        rva, size = struct.unpack_from('<II', data, directory+index*8)
        if not rva or not size:
            continue
        at = file_offset(rva)
        for _ in range(min(size//stride+1, 65536)):
            record = data[at:at+stride]
            if not record or not any(record):
                break
            value = struct.unpack_from('<I', record, name_at)[0]
            if value:
                result.append(name(value))
            at += stride
    return result

def windows_runtime(stage, prefix):
    binary = stage/'bin'
    deploy = next((x for x in (prefix/'Library/bin/windeployqt6.exe',prefix/'Library/bin/windeployqt.exe') if x.exists()), None)
    env = os.environ.copy()
    env['PATH'] = str(prefix/'Library/bin')+os.pathsep+str(prefix)+os.pathsep+env.get('PATH','')
    if deploy is not None:
        try:
            run(deploy, '--release', '--no-translations', '--compiler-runtime', '--dir', binary, binary/'MyDictQt.exe', env=env)
        except subprocess.CalledProcessError:
            print('Conda windeployqt path query failed; using PE dependency closure and explicit Qt plugins.')
    plugin_root = next((p for p in (prefix/'Library/lib/qt6/plugins',prefix/'Library/plugins') if p.exists()), None)
    if plugin_root:
        required=[('platforms','qwindows.dll'),('platforms','qoffscreen.dll'),('platforms','qminimal.dll'),
                  ('sqldrivers','qsqlite.dll'),('tls','qopensslbackend.dll'),('tls','qschannelbackend.dll'),
                  ('tls','qcertonlybackend.dll'),('iconengines','qsvgicon.dll'),('imageformats','qsvg.dll'),
                  ('imageformats','qjpeg.dll'),('imageformats','qico.dll'),('styles','qmodernwindowsstyle.dll')]
        for category, file in required:
            source = plugin_root/category/file
            if source.exists():
                (binary/category).mkdir(exist_ok=True)
                shutil.copy2(source,binary/category/file)
    # Conda Qt relies on non-Qt DLLs which windeployqt intentionally does not copy.
    candidates = {}
    for folder in (prefix/'Library/bin',prefix, prefix/'DLLs'):
        if folder.is_dir():
            for item in folder.glob('*.dll'):
                candidates[item.name.lower()] = item
    pending = list(binary.rglob('*.dll'))+list(binary.glob('*.exe'))
    visited = set()
    while pending:
        current = pending.pop()
        if current in visited:
            continue
        visited.add(current)
        for dependency in pe_imports(current):
            source = candidates.get(dependency.lower())
            if source and not dependency.lower().startswith(('api-ms-', 'ext-ms-')):
                dest = binary/source.name
                if not dest.exists():
                    shutil.copy2(source,dest)
                if dest not in visited:
                    pending.append(dest)
    (binary/'qt.conf').write_text('[Paths]\nPrefix=.\nPlugins=.\n',encoding='utf-8')

def macho_files(folder):
    """Mach-O executables, dylibs and universal binaries, without symlink aliases."""
    magic = {b'\xfe\xed\xfa\xce', b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xcf',
             b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca',
             b'\xca\xfe\xba\xbf', b'\xbf\xba\xfe\xca'}
    for path in folder.rglob('*'):
        if path.is_file() and not path.is_symlink():
            with path.open('rb') as source:
                if source.read(4) in magic:
                    yield path


def mac_loads(path):
    output = subprocess.check_output(['otool', '-L', str(path)], text=True)
    # Whitespace and spaces in paths are both significant; only strip the
    # trailing ABI annotation rather than splitting at the first space.
    return [match.group(1) for line in output.splitlines()[1:]
            if (match := re.match(r'\s+(.+?)\s+\(compatibility version', line))]


def mac_rpaths(path):
    output = subprocess.check_output(['otool', '-l', str(path)], text=True)
    return re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset', output)


def macos_runtime(stage, prefix):
    """Deploy Qt, then close Conda's additional dylib dependencies recursively."""
    app = stage/'MyDictQt.app'
    executable = app/'Contents/MacOS'
    frameworks = app/'Contents/Frameworks'
    plugins = app/'Contents/PlugIns'
    frameworks.mkdir(parents=True, exist_ok=True)
    tool = stage/'bin/MyDictTool'
    cli = executable/'MyDictTool'
    if tool.exists():
        shutil.copy2(tool, cli)
    deploy = prefix/'bin/macdeployqt'
    arguments = [deploy, app, '-always-overwrite']
    if cli.exists():
        arguments.append(f'-executable={cli}')
    if deploy.exists():
        try:
            run(*arguments)
        except subprocess.CalledProcessError:
            # Conda's relocatable Qt metadata can defeat macdeployqt's prefix
            # queries. The explicit plugin list and Mach-O closure below cover
            # the same Widgets dependencies without relying on those queries.
            print('Conda macdeployqt query failed; using Mach-O dependency closure and explicit Qt plugins.')

    plugin_root = next((p for p in (prefix/'lib/qt6/plugins', prefix/'plugins',
                                   prefix/'lib/qt/plugins') if p.is_dir()), None)
    origins = {}
    if plugin_root:
        for category, names in (('platforms', ('libqcocoa.dylib', 'libqoffscreen.dylib', 'libqminimal.dylib')),
                                ('sqldrivers', ('libqsqlite.dylib',)),
                                ('tls', ('libqopensslbackend.dylib', 'libqsecuretransportbackend.dylib', 'libqcertonlybackend.dylib')),
                                ('imageformats', ('libqsvg.dylib',)),
                                ('iconengines', ('libqsvgicon.dylib',))):
            for name in names:
                source = plugin_root/category/name
                if source.exists():
                    dest = plugins/category/name
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(source, dest)
                    origins[dest.resolve()] = source

    # Some Qt builds resolve OpenSSL at runtime instead of linking it into the
    # TLS plugin. Include these explicitly, then inspect their transitive loads.
    for pattern in ('libssl.*.dylib', 'libcrypto.*.dylib'):
        for source in (prefix/'lib').glob(pattern):
            dest = frameworks/source.name
            if not dest.exists():
                shutil.copy2(source, dest, follow_symlinks=True)
            origins[dest.resolve()] = source

    def expand(path, origin):
        return Path(path.replace('@loader_path', str(origin.parent))
                    .replace('@executable_path', str(executable)))

    def origin_of(path):
        resolved = path.resolve()
        if resolved in origins:
            return origins[resolved]
        for deployed, original in ((frameworks, prefix/'lib'), (plugins, plugin_root)):
            if original and path.is_relative_to(deployed):
                candidate = original/path.relative_to(deployed)
                if candidate.exists():
                    return candidate
        return path

    def resolve_dependency(load, current, origin, rpaths):
        candidates = []
        if load.startswith('@rpath/'):
            relative = load[len('@rpath/'):]
            candidates += [frameworks/relative, prefix/'lib'/relative]
            for rpath in rpaths:
                candidates += [expand(rpath, current)/relative,
                               expand(rpath, origin)/relative]
        elif load.startswith('@loader_path/') or load.startswith('@executable_path/'):
            candidates += [expand(load, current), expand(load, origin)]
        else:
            candidates.append(Path(load))
        candidates.append(prefix/'lib'/Path(load).name)
        return next((p for p in candidates if p.is_file()), None)

    pending = list(macho_files(app))
    visited = set()
    while pending:
        current = pending.pop().resolve()
        if current in visited:
            continue
        if not current.is_relative_to(app.resolve()):
            raise RuntimeError(f'Runtime symlink escapes the app bundle: {current}')
        visited.add(current)
        origin = origin_of(current)
        rpaths = mac_rpaths(current)
        id_output = subprocess.check_output(['otool', '-D', str(current)], text=True).splitlines()
        own_id = id_output[1].strip() if len(id_output) > 1 else None
        for load in mac_loads(current):
            if load == own_id or load.startswith(('/System/Library/', '/usr/lib/')):
                continue
            source = resolve_dependency(load, current, origin, rpaths)
            if source is None:
                raise RuntimeError(f'Unresolved macOS runtime dependency: {load} from {current.name}')
            if source.resolve().is_relative_to(app.resolve()):
                dest = source.resolve()
            elif '.framework/' in str(source):
                parts = source.parts
                index = next(i for i, part in enumerate(parts) if part.endswith('.framework'))
                framework = Path(*parts[:index+1])
                destination = frameworks/framework.name
                if not destination.exists():
                    shutil.copytree(framework, destination, symlinks=True)
                    pending.extend(macho_files(destination))
                dest = destination/Path(*parts[index+1:])
                origins[dest.resolve()] = source
            else:
                dest = frameworks/source.name
                if not dest.exists():
                    shutil.copy2(source, dest, follow_symlinks=True)
                    origins[dest.resolve()] = source
                    pending.append(dest)
            relative_load = '@loader_path/'+os.path.relpath(dest, current.parent).replace(os.sep, '/')
            if load != relative_load:
                run('install_name_tool', '-change', load, relative_load, current)
        if own_id:
            relative_id = '@rpath/'+os.path.relpath(current, frameworks).replace(os.sep, '/')
            run('install_name_tool', '-id', relative_id, current)
        for rpath in rpaths:
            if str(prefix) in rpath:
                run('install_name_tool', '-delete_rpath', rpath, current)
        loader_rpath = '@loader_path/'+os.path.relpath(frameworks, current.parent).replace(os.sep, '/')
        if loader_rpath not in mac_rpaths(current):
            run('install_name_tool', '-add_rpath', loader_rpath, current)

    # Qt's plugin lookup is independent of the dynamic loader's dylib lookup.
    resources = app/'Contents/Resources'
    resources.mkdir(parents=True, exist_ok=True)
    (resources/'qt.conf').write_text('[Paths]\nPlugins=PlugIns\n', encoding='utf-8')
    if cli.exists():
        # The public CLI entry point delegates to the bundled binary, sharing
        # the same dependency graph and signature without a Conda activation.
        tool.write_text('#!/bin/sh\nroot="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"\n'
                        'exec "$root/MyDictQt.app/Contents/MacOS/MyDictTool" "$@"\n', encoding='utf-8')
        tool.chmod(0o755)
    # Refuse to ship dependencies that still point to this machine's Conda env.
    for current in macho_files(app):
        if any(str(prefix) in load for load in mac_loads(current)+mac_rpaths(current)):
            raise RuntimeError(f'Non-relocatable Conda reference remains in {current}')


def sign_macos_bundle(stage):
    """Call only after all resources/notices have been copied into the bundle."""
    app = stage/'MyDictQt.app'
    # Ad-hoc signatures make the relocatable arm64 bundle runnable locally;
    # distribution notarization still requires the owner's Apple credentials.
    run('codesign', '--force', '--deep', '--sign', '-', app)
    run('codesign', '--verify', '--deep', '--strict', app)


def unix_runtime(stage, prefix):
    if sys.platform == 'darwin':
        macos_runtime(stage, prefix)
        return
    binary = stage/'bin'
    libs = stage/'lib'
    plugins = stage/'plugins'
    libs.mkdir(exist_ok=True)
    plugin_root = next((x for x in (prefix/'lib/qt6/plugins',prefix/'plugins') if x.exists()), None)
    if plugin_root:
        for category in ('platforms','sqldrivers','imageformats','iconengines','tls','platforminputcontexts','xcbglintegrations','wayland-shell-integration'):
            source = plugin_root/category
            if source.exists():
                shutil.copytree(source,plugins/category,dirs_exist_ok=True)
    pending = list(binary.glob('MyDict*'))+list(plugins.rglob('*.so'))
    visited = set()
    dependency_env=os.environ.copy()
    dependency_env['LD_LIBRARY_PATH']=str(prefix/'lib')+os.pathsep+dependency_env.get('LD_LIBRARY_PATH','')
    while pending:
        current=pending.pop()
        if current in visited:
            continue
        visited.add(current)
        output=subprocess.check_output(['ldd',str(current)],text=True,stderr=subprocess.DEVNULL,env=dependency_env)
        for line in output.splitlines():
            if '=> /' not in line:
                continue
            dependency=Path(line.split('=>',1)[1].strip().split(' ',1)[0])
            # Bundle Conda and most distribution libs; leave glibc/loader to host.
            name=dependency.name
            if name.startswith(('libc.so','libm.so','libpthread.so','libdl.so','librt.so','ld-linux','libresolv.so','libutil.so','libnss_')):
                continue
            # Prefer the environment's C++/ICU/OpenSSL runtime to an older host
            # library found by ldd while inspecting a copied plugin.
            conda_library=prefix/'lib'/name
            if conda_library.exists():
                dependency=conda_library
            dest=libs/name
            if not dest.exists():
                shutil.copy2(dependency,dest,follow_symlinks=True)
                pending.append(dest)
    for current in list(binary.glob('MyDict*'))+list(libs.glob('*'))+list(plugins.rglob('*.so')):
        if current.is_file():
            run('patchelf','--set-rpath','$ORIGIN:$ORIGIN/../lib:$ORIGIN/../../lib',current)
    (binary/'qt.conf').write_text('[Paths]\nPrefix=..\nPlugins=plugins\n',encoding='utf-8')
    for app in ('MyDictQt','MyDictTool'):
        launcher=stage/app
        launcher.write_text('#!/bin/sh\nroot="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"\nexport LD_LIBRARY_PATH="$root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\nexec "$root/bin/'+app+'" "$@"\n',encoding='utf-8')
        launcher.chmod(0o755)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--build',required=True,type=Path)
    parser.add_argument('--prefix',default=os.environ.get('CONDA_PREFIX'),type=Path)
    parser.add_argument('--config',default='Release')
    parser.add_argument('--output',default=ROOT/'dist',type=Path)
    args=parser.parse_args()
    if not args.prefix:
        parser.error('--prefix or CONDA_PREFIX is required')
    machine=platform.machine().lower()
    arch='arm64' if machine in ('arm64','aarch64') else 'x64'
    osname={'win32':'windows','darwin':'macos'}.get(sys.platform,'linux')
    version=re.search(r'project\(MyDictQt VERSION ([0-9.]+) ',(ROOT/'CMakeLists.txt').read_text(encoding='utf-8')).group(1)
    name=f'MyDictQt-v{version}-shared-{osname}-{arch}'
    stage=args.output/name
    if stage.exists():
        raise RuntimeError(f'Package directory already exists; use a new output directory: {stage}')
    run('cmake','--install',args.build,'--config',args.config,'--prefix',stage)
    # The install manifest only copies public files. Never sweep bin/profiles,
    # local fonts, personal settings, SSH credentials or an existing release.
    if sys.platform=='win32':
        windows_runtime(stage,args.prefix)
    else:
        unix_runtime(stage,args.prefix)
    notices=stage/('MyDictQt.app/Contents/Resources' if sys.platform=='darwin' else 'bin')/'third-party-licenses'
    notices.mkdir(parents=True,exist_ok=True)
    for source in (args.prefix/'info/licenses',args.prefix/'Library/share/licenses',args.prefix/'share/licenses'):
        if source.exists():
            shutil.copytree(source,notices/source.parent.name,dirs_exist_ok=True)
    # Conda package receipts preserve exact dependency/version/build attribution.
    receipts=notices/'conda-meta'
    receipts.mkdir(exist_ok=True)
    for source in (args.prefix/'conda-meta').glob('*.json'):
        shutil.copy2(source,receipts/source.name)
        receipt=json.loads(source.read_text(encoding='utf-8'))
        cached=Path(receipt.get('link',{}).get('source',''))/'info/licenses'
        if cached.is_dir():
            shutil.copytree(cached,notices/source.stem,dirs_exist_ok=True)
    if sys.platform=='darwin':
        sign_macos_bundle(stage)
    if sys.platform=='win32':
        archive=Path(shutil.make_archive(str(args.output/name),'zip',args.output,name))
    else:
        archive=args.output/(name+'.tar.gz')
        with tarfile.open(archive,'w:gz') as target:
            target.add(stage,arcname=name)
    digest=hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(archive.suffix+'.sha256').write_text(f'{digest}  {archive.name}\n',encoding='ascii')
    print(f'Package: {archive}')

if __name__=='__main__':
    main()
