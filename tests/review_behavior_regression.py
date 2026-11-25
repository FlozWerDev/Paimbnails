"""Exercise production callbacks with deterministic network/engine test doubles.

Fixtures contain stubs and assertions. Function placeholders are expanded from
the current source, so the tests cannot silently keep testing an old copy.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from run_regressions import add_sanitizer_runtime, fmt_include_path
from dependency_paths import package_source, json_compile_inputs

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = Path(__file__).with_name('review_fixtures')


def extract_function(path, marker):
    source = (ROOT / path).read_text(encoding='utf-8-sig')
    start = source.index(marker)
    opening = source.index('{', start)
    depth, index = 1, opening + 1
    while depth:
        if source.startswith('//', index):
            index = source.index('\n', index) + 1
            continue
        if source.startswith('/*', index):
            index = source.index('*/', index + 2) + 2
            continue
        # Decimal digit separators belong to C++ numbers, not character literals.
        if source[index] == "'" and source[index - 1].isdigit() and source[index + 1].isdigit():
            index += 1
            continue
        if source[index] in ('"', "'"):
            quote = source[index]
            index += 1
            while source[index] != quote:
                index += 2 if source[index] == '\\' else 1
        elif source[index] == '{':
            depth += 1
        elif source[index] == '}':
            depth -= 1
        index += 1
    return source[start:index]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path,
                        default=Path(os.environ.get('PAIMBNAILS_BUILD_DIR', ROOT / 'build-regressions')))
    parser.add_argument('--case', action='append', default=[])
    args = parser.parse_args()
    build = args.build_dir.resolve()
    output = build / 'review-behavior'
    output.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise RuntimeError('The callback regression fixtures require Clang++ or G++')
    fmt = fmt_include_path(build)
    if not fmt:
        raise RuntimeError('Configure the project first: the callback fixtures require fmt headers')
    env = os.environ.copy()
    add_sanitizer_runtime(env, compiler)
    results = []
    for fixture in sorted(FIXTURES.glob('*.cpp.in')):
        name = fixture.name.removesuffix('.cpp.in')
        if args.case and name not in args.case:
            continue
        source = re.sub(r'@@function\|([^|]+)\|([^\n]+?)@@',
                        lambda m: extract_function(m[1], m[2]), fixture.read_text(encoding='utf-8'))
        source = re.sub(r'@@source\|([^\n]+?)@@',
                        lambda m: re.sub(r'^\s*#(?:include[^\n]*|pragma once[^\n]*)', '',
                            (ROOT / m[1]).read_text(encoding='utf-8-sig'), flags=re.MULTILINE), source)
        if 'paimon::paths::' in source:
            source = '#include "src/utils/FilesystemPath.hpp"\n' + source
        cpp = output / (name + '.cpp')
        cpp.write_text(source, encoding='utf-8')
        exe = output / (name + ('.exe' if os.name == 'nt' else ''))
        command = [compiler, '-std=c++23', '-g', '-fsanitize=address,undefined',
                   '-fno-omit-frame-pointer', '-I' + str(fmt), '-I' + str(ROOT),
                   str(cpp), '-o', str(exe)]
        if '#include "src/utils/AtomicFileWrite.hpp"' in source:
            command += ['-I' + str(ROOT / 'tests/stubs'), str(ROOT / 'src/utils/AtomicFileWrite.cpp')]
        if '#include <matjson.hpp>' in source or '#include <Geode/Result.hpp>' in source:
            result_root = package_source(build, ROOT, 'result', 'include/Geode/Result.hpp')
            if not result_root:
                raise RuntimeError('Geode Result headers missing from configured dependency sources')
            command += ['-I' + str(result_root / 'include')]
            if '#include <matjson.hpp>' in source:
                json_root = package_source(build, ROOT, 'json', 'include/matjson.hpp')
                if not json_root:
                    raise RuntimeError('matjson sources missing from configured dependency sources')
                json_includes, json_sources = json_compile_inputs(build, ROOT)
                command += ['-I' + str(include) for include in json_includes]
                command += [str(path) for path in json_sources]
        compiled = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True,
                                  errors='replace', timeout=120)
        log = compiled.stdout + compiled.stderr
        code = compiled.returncode
        if code == 0:
            executed = subprocess.run([str(exe)], cwd=output, env=env, capture_output=True,
                                      text=True, errors='replace', timeout=15)
            log += executed.stdout + executed.stderr
            code = executed.returncode
        (output / (name + '.log')).write_text(log, encoding='utf-8')
        results.append({'case': name, 'exit': code})
        print(('PASS' if code == 0 else 'FAIL') + ': ' + name, flush=True)
        if code:
            print(log[-6000:], flush=True)
    if not results:
        raise RuntimeError('No review regression fixtures selected')
    (output / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    return int(any(r['exit'] != 0 for r in results))


if __name__ == '__main__':
    raise SystemExit(main())
