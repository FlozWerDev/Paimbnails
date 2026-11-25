import fs from 'fs';
import path from 'path';
import { parseOutputArgs, stripCommentsAndStrings, writeJson } from './lib/source-scan.mjs';

const options = parseOutputArgs(process.argv.slice(2), {
    root: path.resolve('.'),
    input: path.resolve('audit_results.json'),
    out: path.resolve('unwraps_triaged.json'),
});

if (!fs.existsSync(options.input)) {
    console.error(`Input not found: ${options.input}`);
    console.error('Run tools/comprehensive_audit.mjs --out audit_results.json first.');
    process.exit(2);
}

const data = JSON.parse(fs.readFileSync(options.input, 'utf8'));
const candidates = data.result_unwrap_unchecked || [];
const cache = new Map();
const triaged = [];

function codeFor(file) {
    const absolute = path.isAbsolute(file) ? file : path.join(options.root, file);
    if (!cache.has(absolute)) {
        const raw = fs.readFileSync(absolute, 'utf8');
        cache.set(absolute, {
            rawLines: raw.split('\n'),
            codeLines: stripCommentsAndStrings(raw).split('\n'),
        });
    }
    return cache.get(absolute);
}

for (const item of candidates) {
    const lines = codeFor(item.file);
    const lineIndex = Math.max(0, item.line - 1);
    const start = Math.max(0, lineIndex - 25);
    const end = Math.min(lines.codeLines.length, lineIndex + 4);
    const contextCode = lines.codeLines.slice(start, end).join('\n');
    const contextRaw = lines.rawLines.slice(start, end).join('\n');
    const variable = item.var;
    const escaped = variable.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');

    const currentLine = lines.codeLines[lineIndex] || '';
    const safe = currentLine.includes('unwrapOr(')
        || new RegExp(`${escaped}\\s*\\.\\s*isOk\\s*\\(`).test(contextCode)
        || new RegExp(`${escaped}\\s*\\.\\s*has_value\\s*\\(`).test(contextCode)
        || new RegExp(`${escaped}\\s*\\.\\s*isErr\\s*\\(`).test(contextCode)
        || new RegExp(`!\\s*${escaped}\\s*\\.\\s*isErr\\s*\\(`).test(contextCode)
        || new RegExp(`if\\s*\\(\\s*!?\\s*${escaped}\\s*(?:\\.isOk\\(\\))?\\s*\\)`).test(contextCode)
        || new RegExp(`if\\s*\\(\\s*(?:const\\s+)?auto(?:\\s+const)?\\s*&?\\s*${escaped}\\s*=`).test(contextCode)
        || new RegExp(`;\\s*!?\\s*${escaped}\\s*\\)`).test(contextCode)
        || new RegExp(`!\\s*${escaped}\\s*\\|\\|`).test(contextCode)
        || new RegExp(`\\|\\|\\s*!\\s*${escaped}`).test(contextCode)
        || new RegExp(`${escaped}\\s*&&`).test(contextCode)
        || new RegExp(`&&\\s*${escaped}`).test(contextCode)
        || new RegExp(`${escaped}\\s*\\?`).test(contextCode)
        || new RegExp(`(?:GEODE_)?[Aa]ssert\\s*\\(`).test(contextCode);

    if (safe) continue;

    triaged.push({
        file: item.file,
        line: item.line,
        var: variable,
        text: item.text,
        reason: 'no guard found in preceding 25 lines',
        context: contextRaw,
    });
}

const byFile = triaged.reduce((acc, finding) => {
    acc[finding.file] = (acc[finding.file] || 0) + 1;
    return acc;
}, {});

writeJson(options.out, {
    input: options.input,
    candidates: candidates.length,
    unguarded: triaged.length,
    byFile,
    findings: triaged,
});

console.log(`Candidates: ${candidates.length}; unguarded after triage: ${triaged.length}`);
console.log(`Written to ${options.out}`);
for (const [file, count] of Object.entries(byFile).sort((a, b) => b[1] - a[1]).slice(0, 15)) {
    console.log(`  ${count.toString().padStart(3)}  ${file}`);
}
