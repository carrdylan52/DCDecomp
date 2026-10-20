"""Focused rejection tests use real compiled COFF objects."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def run(command):
    return subprocess.run(list(map(str, command)), capture_output=True, text=True)


def main():
    p = argparse.ArgumentParser()
    for name in ('compiler', 'nm', 'ar', 'python', 'build', 'archive', 'binary'):
        p.add_argument('--' + name, required=True)
    args = p.parse_args()
    checker = Path(__file__).with_name('check_symbols.py')
    with tempfile.TemporaryDirectory(prefix='ixdc2-gate-', dir=args.build) as temporary:
        root = Path(temporary)

        def compile_object(name, source):
            cpp, obj = root / (name + '.cpp'), root / (name + '.obj')
            cpp.write_text(source)
            result = run([args.compiler, '-c', cpp, '-o', obj])
            if result.returncode:
                raise SystemExit(result.stderr)
            return obj

        for name, source, reason in (
            ('global', 'extern "C" { int GamePad__2 = 42; }', "leaks=['GamePad__2']"),
            ('binding', 'extern "C" void MoveCheck(); extern "C" void ixdc2_probe() { MoveCheck(); }',
             "foreign references=['MoveCheck']"),
            ('rate', 'extern "C" float mgFrameRate; extern "C" float ixdc2_probe() { return mgFrameRate; }',
             "foreign references=['mgFrameRate']"),
        ):
            archive = root / (name + '.a')
            result = run([args.ar, 'rcs', archive, compile_object(name, source)])
            if result.returncode:
                raise SystemExit(result.stderr)
            result = run([args.python, '-B', checker, '--nm', args.nm, '--archive', archive])
            if result.returncode == 0 or reason not in result.stderr:
                raise SystemExit(f'FAIL {name} was not rejected: {result.stdout}{result.stderr}')
            print(f'PASS gate rejects {name}: {reason}')
        collision = compile_object('collision', 'extern "C" float ixdc2_robo_gravity(float, int) { return 0; }')
        objects = root / 'host.txt'
        objects.write_text(str(collision) + '\n')
        result = run([args.python, '-B', checker, '--nm', args.nm, '--archive', args.archive,
                      '--binary', args.binary, '--host-objects', objects])
        if result.returncode == 0 or 'donor/host object collisions' not in result.stderr:
            raise SystemExit(f'FAIL collision was not rejected: {result.stdout}{result.stderr}')
        print('PASS gate rejects prefixed host collision: ixdc2_robo_gravity')
        extra = compile_object('export', 'extern "C" __declspec(dllexport) int accidental_export() { return 1; }')
        binary = root / 'extra.dll'
        result = run([args.compiler, '-shared', extra, '-Wl,--whole-archive', args.archive,
                      '-Wl,--no-whole-archive', '-Wl,--exclude-all-symbols', '-o', binary])
        if result.returncode:
            raise SystemExit(result.stderr)
        objects.write_text('')
        result = run([args.python, '-B', checker, '--nm', args.nm, '--archive', args.archive,
                      '--binary', binary, '--host-objects', objects])
        if result.returncode == 0 or 'FAIL binary exports' not in result.stderr:
            raise SystemExit(f'FAIL export was not rejected: {result.stdout}{result.stderr}')
        print('PASS gate rejects accidental DLL export')
    print('PASS symbol gate negative tests: 5/5')


if __name__ == '__main__':
    main()
