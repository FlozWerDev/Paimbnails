import path from 'path';
import { readSources, parseOutputArgs, writeJson } from './lib/source-scan.mjs';

const options = parseOutputArgs(process.argv.slice(2), {
    root: path.resolve('.'),
    out: path.resolve('division_findings.json'),
});

const sources = readSources(options.root, ['.cpp', '.hpp', '.h', '.mm']);
const findings = [];

const DIVISION = /(?<![/*])\/(?![/*=])\s*([A-Za-z_]\w*(?:\s*(?:->|\.)\s*\w+)*(?:\s*\(\s*\))?)/g;

function guardPatterns(divisor) {
    const escaped = divisor.replace(/[.*+?^${}()|[\]\\]/g, '\\$&').replace(/\s+/g, '\\s*');
    return [
        `${escaped}\\s*<=\\s*0`,
        `${escaped}\\s*==\\s*0(?:\\.0+)?`,
        `${escaped}\\s*<\\s*1(?:\\.0+)?`,
        `${escaped}\\s*!=\\s*0`,
        `${escaped}\\s*>\\s*0`,
        `${escaped}\\.empty\\(\\)`,
        `if\\s*\\(\\s*!\\s*${escaped}\\s*\\)`,
        `if\\s*\\(\\s*${escaped}\\s*\\)`,
        `${escaped}\\s*\\?`,
        `std::max(?:<[^>]*>)?\\s*\\(\\s*1`,
        `std::max(?:<[^>]*>)?\\s*\\(\\s*0`,
        `std::clamp\\s*\\(`,
        `1e-\\d+`,
        `epsilon`,
    ].map((pattern) => new RegExp(pattern));
}

for (const source of sources) {
    for (let i = 0; i < source.codeLines.length; i++) {
        const code = source.codeLines[i];
        if (!code.includes('/')) continue;
        if (code.includes('operator/') || code.includes('operator /')) continue;
        if (/^\s*#\s*(include|import|define|if|elif)/.test(code)) continue;

        DIVISION.lastIndex = 0;
        let match;
        while ((match = DIVISION.exec(code)) !== null) {
            const divisor = match[1].replace(/\s+/g, ' ').trim();
            if (!divisor || /^\d/.test(divisor)) continue;
            if (divisor === 'this') continue;
            if (/^(static_cast|reinterpret_cast|const_cast|std|sizeof|alignof|decltype)$/.test(divisor)) continue;
            if (/^(std::)?(max|min)\b/.test(divisor)) continue;
            if (/^(nullptr|true|false|float|double|int|unsigned|size_t|auto|bool|long|short|char|uint\d+_t|int\d+_t|M_PI|M_E|M_PI_2)$/.test(divisor)) continue;
            if (/filesystem|fs::|getSaveDir|cacheDir\s*\(|\bpath\s*\(|directory|\.string\s*\(|fmt::format|::format\s*\(/.test(code)) continue;
            if (/chrono|duration|ratio/.test(code)) continue;

            const start = Math.max(0, i - 10);
            const end = Math.min(source.codeLines.length, i + 3);
            const context = source.codeLines.slice(start, end).join('\n');
            const guarded = guardPatterns(divisor).some((pattern) => pattern.test(context));
            if (guarded) continue;

            const containerSize = /\b(size|count|length|height|width)\s*\(\s*\)$/.test(divisor);
            const constantLike = /^(k[A-Z]\w*|s_[A-Z_]\w*|[A-Z_][A-Z0-9_]{1,})$/.test(divisor);
            findings.push({
                file: source.rel,
                line: i + 1,
                divisor,
                kind: containerSize ? 'container-size' : (constantLike ? 'constant-like' : 'variable'),
                text: source.rawLines[i].trim(),
                context: source.rawLines.slice(Math.max(0, i - 2), Math.min(source.rawLines.length, i + 2)).join('\n'),
            });
        }
    }
}

const byKind = findings.reduce((acc, finding) => {
    acc[finding.kind] = (acc[finding.kind] || 0) + 1;
    return acc;
}, {});

writeJson(options.out, findings);
console.log(`Division candidates: ${findings.length} (${Object.entries(byKind).map(([k, v]) => `${k}: ${v}`).join(', ')})`);
console.log(`Written to ${options.out}`);
