import fs from 'fs';
import path from 'path';
import { spawnSync } from 'child_process';

function parseArgs(argv) {
    const result = {
        root: path.resolve('.'),
        out: path.resolve('build-clang', 'review-completa-2026-10-24', 'scans'),
    };
    for (let i = 0; i < argv.length; i++) {
        if (argv[i] === '--root' && argv[i + 1]) result.root = path.resolve(argv[++i]);
        else if (argv[i] === '--out' && argv[i + 1]) result.out = path.resolve(argv[++i]);
    }
    return result;
}

const options = parseArgs(process.argv.slice(2));
fs.mkdirSync(options.out, { recursive: true });

const hardened = [
    { name: 'comprehensive', script: 'tools/comprehensive_audit.mjs', args: ['--out', path.join(options.out, 'audit_results.json')] },
    { name: 'unwraps', script: 'tools/check_unwraps.mjs', args: ['--input', path.join(options.out, 'audit_results.json'), '--out', path.join(options.out, 'unwraps_triaged.json')] },
    { name: 'division', script: 'tools/audit_division_by_zero.mjs', args: ['--out', path.join(options.out, 'division_findings.json')] },
    { name: 'async-lifetime', script: 'tools/audit_async_lifetime.mjs', args: ['--out', path.join(options.out, 'async_lifetime_findings.json')] },
    { name: 'fs-paths', script: 'tools/audit_fs_paths.mjs', args: ['--out', path.join(options.out, 'fs_path_findings.json')] },
    { name: 'net-inputs', script: 'tools/audit_net_inputs.mjs', args: ['--out', path.join(options.out, 'net_input_findings.json')] },
];

const legacy = [
    'audit_thread_safety.mjs',
    'audit_io_on_main.mjs',
    'audit_saved_values.mjs',
    'audit_retains.mjs',
    'audit_hooks.mjs',
    'audit_settings.mjs',
    'audit_modulo.mjs',
    'audit_null_checks.mjs',
    'audit_pass_by_value.mjs',
];

const index = [];
const started = Date.now();

function run(name, script, args) {
    const start = Date.now();
    const result = spawnSync(process.execPath, [script, ...args], {
        cwd: options.root,
        encoding: 'utf8',
        maxBuffer: 128 * 1024 * 1024,
    });
    const duration = Date.now() - start;
    const logPath = path.join(options.out, `${name}.log`);
    fs.writeFileSync(logPath, `$ node ${script} ${args.join(' ')}\n\n${result.stdout || ''}\n${result.stderr || ''}`, 'utf8');
    index.push({ name, script, args, exitCode: result.status, durationMs: duration, log: logPath });
    console.log(`[${result.status === 0 ? 'ok' : 'fail'}] ${name} (${duration} ms)${result.status === 0 ? '' : ` -> ${logPath}`}`);
}

for (const entry of hardened) {
    run(entry.name, entry.script, [...entry.args, '--root', options.root]);
}

for (const script of legacy) {
    const name = path.basename(script, '.mjs');
    run(name, path.join('tools', script), []);
    const artifact = path.join(options.root, script.replace(/^audit_/, '').replace(/\.mjs$/, '_findings.json'));
    if (fs.existsSync(artifact)) {
        fs.copyFileSync(artifact, path.join(options.out, path.basename(artifact)));
    }
}

const indexData = { generated: new Date().toISOString(), root: options.root, out: options.out, durationMs: Date.now() - started, scans: index };
fs.writeFileSync(path.join(options.out, 'index.json'), JSON.stringify(indexData, null, 2) + '\n', 'utf8');
console.log(`Scans written to ${options.out} (${Date.now() - started} ms)`);
