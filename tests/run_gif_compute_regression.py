"""Compile and compare the real CPU/DirectCompute importer on Windows hardware."""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build-clang')
    parser.add_argument('--cxx', default=shutil.which('clang-cl'))
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('DirectCompute tests require Windows and a hardware D3D11 adapter')
    if not args.cxx:
        parser.error('Pass --cxx with the clang-cl compiler path')
    output = args.build_dir.resolve() / 'gif-compute-tests'
    output.mkdir(parents=True, exist_ok=True)
    sdk = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Windows Kits/10/bin'
    compilers = sorted(sdk.glob('*/x64/fxc.exe'), reverse=True)
    fxc = shutil.which('fxc') or (str(compilers[0]) if compilers else None)
    if not fxc:
        parser.error('FXC from the Windows SDK is required')
    environment = os.environ.copy()
    if args.sanitize:
        llvm = Path(args.cxx).resolve().parent.parent
        for runtime in sorted((llvm / 'lib/clang').glob('*/lib/windows'), reverse=True):
            if list(runtime.glob('clang_rt.asan_dynamic-*.dll')):
                environment['PATH'] = str(runtime) + os.pathsep + environment.get('PATH', '')
                break

    def run(command):
        result = subprocess.run(command, cwd=output, env=environment, text=True,
                                capture_output=True, errors='replace', timeout=240)
        print(result.stdout, end='', flush=True)
        if result.stderr:
            print(result.stderr, end='', flush=True)
        result.check_returncode()
        return result.stdout

    service = ROOT / 'src/features/gif-import/services'
    for kernel in ('Coverage', 'Quantize'):
        run([fxc, '/nologo', '/T', 'cs_5_0', '/E', 'main', '/O3', '/Gis',
             '/Vn', f'k{kernel}ShaderBytecode', '/Fh', str(output / f'Gif{kernel}Shader.hpp'),
             str(service / f'shaders/Gif{kernel}.hlsl')])
    hashes = []
    for backend in ('cpu', 'gpu'):
        executable = output / f'gif-compute-{backend}.exe'
        command = [args.cxx, '/nologo', '/std:c++20', '/EHsc', '/W4', '/O2',
                   '/I' + str(ROOT / 'tests/stubs'), '/I' + str(output),
                   str(ROOT / 'tests/gif_compute_regression.cpp'), '/Fe:' + str(executable)]
        if args.sanitize:
            command += ['/Zi', '/fsanitize=address', '/clang:-fsanitize=undefined']
        if backend == 'gpu':
            command += ['/DPAIM_GIF_COMPUTE=1', str(service / 'GifCompute.cpp'), 'd3d11.lib']
        run(command)
        print(f'{backend.upper()}:', flush=True)
        log = run([str(executable)])
        (output / f'{backend}.log').write_text(log, encoding='utf-8')
        hashes.append(re.findall(r'\bhash=(\d+)', log))
    if len(hashes[0]) != 3 or hashes[0] != hashes[1]:
        raise RuntimeError(f'CPU/GPU frame or final geometry mismatch: {hashes}')
    print('PASS: identical animation labels and complete paint geometry; real GPU coverage matches CPU')


if __name__ == '__main__':
    main()
