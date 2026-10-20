"""Byte provenance and Windows/COFF donor isolation gate; no engine headers."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def symbols(nm, paths):
    result = subprocess.run([nm, '--extern-only', '--format=posix', *map(str, paths)],
                            capture_output=True, text=True, check=True)
    rows = []
    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 2 and len(parts[1]) == 1:
            rows.append((parts[0], parts[1]))
    return rows


def private(name):
    if name.startswith('.refptr.'):
        name = name[len('.refptr.'):]
    return name.startswith(('ixdc2_', '_ZN5ixdc2', '_ZNK5ixdc2',
                            '_ZTVN5ixdc2', '_ZTIN5ixdc2', '_ZTSN5ixdc2'))


def strong(rows):
    return {name for name, kind in rows if kind not in ('U', 'w', 'v', 'W', 'V')}


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--nm', required=True)
    p.add_argument('--archive', required=True)
    p.add_argument('--donor')
    p.add_argument('--helpers')
    p.add_argument('--binary')
    p.add_argument('--host-objects')
    p.add_argument('--module', action='store_true')
    args = p.parse_args()
    rows = symbols(args.nm, [args.archive])
    definitions = {name for name, kind in rows if kind != 'U'}
    runtime = {'atan2f', 'sinf', 'cosf', 'sqrtf', 'sincosf', 'memcpy', 'memset', 'strcpy', 'fmaxf',
               '__stack_chk_guard', '__stack_chk_fail', '__chkstk'}
    leaks = sorted(name for name in strong(rows) if not private(name))
    unresolved = {name for name, kind in rows if kind == 'U'} - definitions
    bad_refs = sorted(name for name in unresolved if name not in runtime)
    if leaks or bad_refs:
        raise SystemExit(f'FAIL donor symbols: leaks={leaks}, foreign references={bad_refs}')
    if args.donor:
        manifest = json.loads(Path(args.helpers).read_text())
        copied = []
        for item in manifest['snippets']:
            path = Path(args.donor) / item['path']
            data = path.read_bytes()
            # Hash the repository blob with its Git object header, independently
            # of the donor's current branch name. Handle checkout CRLF as Git does.
            normalized = data.replace(b'\r\n', b'\n')
            blob = hashlib.sha1(f'blob {len(normalized)}\0'.encode() + normalized).hexdigest()
            if blob != item['blob']:
                raise SystemExit(f'FAIL donor blob drift: {item["path"]}')
            snippet = b''.join(data.splitlines(keepends=True)[item['start'] - 1:item['end']]).rstrip(b'\r\n')
            if hashlib.sha256(snippet).hexdigest() != item['sha256']:
                raise SystemExit(f'FAIL donor helper drift: {item["path"]}')
            copied.append(snippet)
        if Path(args.helpers).with_name('helpers.inc').read_bytes() != b'\n\n'.join(copied) + b'\n':
            raise SystemExit('FAIL copied helper bytes differ from donor')
        print('PASS provenance: 2 helper bodies byte-identical; pinned DC2 blobs verified')
    if args.binary:
        host_paths = Path(args.host_objects).read_text().splitlines()
        host_defs = set()
        for i in range(0, len(host_paths), 40):
            host_defs.update(strong(symbols(args.nm, host_paths[i:i + 40])))
        collisions = sorted(strong(rows) & host_defs)
        if collisions:
            raise SystemExit(f'FAIL donor/host object collisions: {collisions}')
        names = {name for name, kind in symbols(args.nm, [args.binary]) if kind != 'U'}
        for required in ('ixdc2_robo_walk_step', 'ixdc2_robo_gravity', 'RoboWalkMoveIF', 'unitRotation'):
            if not any(required in name and private(name) for name in names):
                raise SystemExit(f'FAIL linked donor missing: {required}')
        readobj = Path(args.nm).with_name('llvm-readobj.exe' if Path(args.nm).suffix == '.exe' else 'llvm-readobj')
        output = subprocess.check_output([str(readobj), '--coff-exports', args.binary], text=True)
        exports = [line.split(':', 1)[1].strip() for line in output.splitlines() if line.strip().startswith('Name:')]
        expected = ['ixion_get_api'] if args.module else []
        if exports != expected:
            raise SystemExit(f'FAIL binary exports: {exports}, expected={expected}')
        print(f'PASS linked binary: {len(host_paths)} host objects; 0 donor collisions; donor bodies retained; exports={exports}')
    print(f'PASS donor archive: {len(strong(rows))} private strong symbols; 0 leaked definitions; 0 foreign game references')


if __name__ == '__main__':
    main()
