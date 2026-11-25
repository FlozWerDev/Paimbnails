import path from 'path';
import { readSources, parseOutputArgs, writeJson } from './lib/source-scan.mjs';

const options = parseOutputArgs(process.argv.slice(2), {
    root: path.resolve('.'),
    out: path.resolve('net_input_findings.json'),
});

const sources = readSources(options.root, ['.cpp', '.hpp', '.h', '.mm']);

const INPUT_PATTERNS = [
    { name: 'recv', regex: /\brecv\s*\(|\brecvfrom\s*\(|\bWSARecv\s*\(/ },
    { name: 'socket-read', regex: /\bread\s*\(\s*[A-Za-z_]\w*\s*,|\breadsome\s*\(/ },
    { name: 'accept', regex: /\baccept\s*\(/ },
    { name: 'bind', regex: /\bbind\s*\(/ },
    { name: 'listen', regex: /\blisten\s*\(/ },
    { name: 'body', regex: /\.body\s*\(|\.getBody\s*\(|response\.body|res\.body/ },
    { name: 'header', regex: /getHeader\s*\(|header\s*\(|Content-Length|content-length/i },
    { name: 'json-parse', regex: /matjson::parse|json::parse|\bparseJson\b|\.as<|\.asString\s*\(/ },
    { name: 'decompress', regex: /gzip|inflate|decompress|zstd|unzstd/i },
    { name: 'webrequest', regex: /WebRequest|web::WebRequest|WebSocket|HttpClient::get\(\)/ },
];

const GUARDS = /timeout|maxSize|max_size|maxBody|limit|size\s*\(\)|length\s*\(\)|isOk\s*\(\)|status\s*\(|validate|startsWith|allowlist|trusted|isTrusted|loopback|INADDR_LOOPBACK|MAX_|kMax|Content-Length|content-length|depth|schema|reject|400|413|431/i;

const findings = [];

for (const source of sources) {
    for (let i = 0; i < source.codeLines.length; i++) {
        const code = source.codeLines[i];
        for (const pattern of INPUT_PATTERNS) {
            if (!pattern.regex.test(code)) continue;
            const near = source.codeLines.slice(Math.max(0, i - 10), Math.min(source.codeLines.length, i + 6)).join('\n');
            if (GUARDS.test(near)) continue;
            findings.push({
                file: source.rel,
                line: i + 1,
                kind: pattern.name,
                text: source.rawLines[i].trim(),
                context: source.rawLines.slice(Math.max(0, i - 2), Math.min(source.rawLines.length, i + 3)).join('\n'),
            });
        }
    }
}

const byKind = findings.reduce((acc, finding) => {
    acc[finding.kind] = (acc[finding.kind] || 0) + 1;
    return acc;
}, {});

writeJson(options.out, { byKind, findings });
console.log('Network input candidates:', byKind);
console.log(`Written to ${options.out}`);
