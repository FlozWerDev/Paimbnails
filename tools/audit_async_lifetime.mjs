import path from 'path';
import { readSources, parseOutputArgs, writeJson, lineNumberAt } from './lib/source-scan.mjs';

const options = parseOutputArgs(process.argv.slice(2), {
    root: path.resolve('.'),
    out: path.resolve('async_lifetime_findings.json'),
});

const sources = readSources(options.root, ['.cpp', '.hpp', '.h', '.mm']);

const APIS = [
    { name: 'queueInMainThread', regex: /queueInMainThread\s*\(/g },
    { name: 'std::thread', regex: /std::thread\s*\(/g },
    { name: 'std::async', regex: /std::async\s*\(/g },
    { name: 'ThreadPool', regex: /(?:ThreadPool|threadPool|m_pool|m_threadPool)[\w.:>-]*\.?\s*push\w*\s*\(/g },
    { name: 'pushTask', regex: /\bpushTask\w*\s*\(/g },
    { name: 'async::', regex: /\basync::\w+\s*\(/g },
    { name: 'scheduleOnce', regex: /\bscheduleOnce\s*\(/g },
    { name: 'runInBackground', regex: /\brunInBackground\w*\s*\(/g },
];

function findMatchingBrace(text, openIndex) {
    let depth = 0;
    for (let i = openIndex; i < text.length; i++) {
        if (text[i] === '{') depth++;
        else if (text[i] === '}') {
            depth--;
            if (depth === 0) return i;
        }
    }
    return -1;
}

const findings = [];

for (const source of sources) {
    for (const api of APIS) {
        api.regex.lastIndex = 0;
        let call;
        while ((call = api.regex.exec(source.code)) !== null) {
            const searchStart = call.index + call[0].length;
            const captureStart = source.code.indexOf('[', searchStart);
            if (captureStart === -1 || captureStart - searchStart > 100) continue;

            const captureEnd = source.code.indexOf(']', captureStart);
            if (captureEnd === -1 || captureEnd - captureStart > 160) continue;

            const capture = source.code.slice(captureStart + 1, captureEnd).trim();
            const bodyStart = source.code.indexOf('{', captureEnd);
            if (bodyStart === -1 || bodyStart - captureEnd > 120) continue;

            const bodyEnd = findMatchingBrace(source.code, bodyStart);
            if (bodyEnd === -1) continue;

            const body = source.code.slice(bodyStart, bodyEnd + 1);
            const hasWeakRef = /WeakRef\s*[<(]/.test(body.slice(0, 300));
            const rawThis = /(?:^|,)\s*this\s*(?:,|$)/.test(capture)
                || /self\s*=\s*this/.test(capture)
                || capture === 'this';
            const referenceCapture = capture === '&'
                || capture === '='
                || /(?:^|,)\s*&(?!\s*\w+\s*=)/.test(capture);
            const copyCapture = capture === '=';

            if (!rawThis && !referenceCapture && !copyCapture) continue;

            const line = lineNumberAt(source.code, call.index);
            const rawLine = source.rawLines[line - 1] || '';
            const bodyPreview = source.raw.slice(bodyStart, Math.min(bodyEnd + 1, bodyStart + 260)).replace(/\s+/g, ' ');

            findings.push({
                file: source.rel,
                line,
                api: api.name,
                capture,
                rawThis,
                referenceCapture,
                copyCapture,
                hasWeakRef,
                severityHint: rawThis && !hasWeakRef ? 'high' : (referenceCapture ? 'high' : 'medium'),
                text: rawLine.trim(),
                bodyPreview,
            });
        }
    }
}

findings.sort((a, b) => a.file.localeCompare(b.file) || a.line - b.line);
const summary = findings.reduce((acc, finding) => {
    const key = `${finding.api}:${finding.severityHint}`;
    acc[key] = (acc[key] || 0) + 1;
    return acc;
}, {});

writeJson(options.out, { summary, findings });
console.log('Async lifetime candidates:', summary);
console.log(`Written to ${options.out}`);
