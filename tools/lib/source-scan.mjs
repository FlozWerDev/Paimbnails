import fs from 'fs';
import path from 'path';

// Replace comments and string/char literal contents with spaces while preserving
// newlines and byte offsets, so regexes never match prose, URLs or example code.
export function stripCommentsAndStrings(source) {
    const out = source.split('');
    const n = source.length;
    let i = 0;
    let state = 'code';

    while (i < n) {
        const c = source[i];
        const d = source[i + 1];

        if (state === 'code') {
            if (c === '/' && d === '/') {
                out[i] = ' ';
                out[i + 1] = ' ';
                i += 2;
                state = 'line';
                continue;
            }
            if (c === '/' && d === '*') {
                out[i] = ' ';
                out[i + 1] = ' ';
                i += 2;
                state = 'block';
                continue;
            }
            if (c === '"') {
                out[i] = ' ';
                i++;
                state = 'string';
                continue;
            }
            if (c === "'") {
                out[i] = ' ';
                i++;
                state = 'char';
                continue;
            }
            if (c === 'R' && d === '"') {
                out[i] = ' ';
                out[i + 1] = ' ';
                i += 2;
                let delimiter = '';
                while (i < n && source[i] !== '(') {
                    delimiter += source[i];
                    out[i] = ' ';
                    i++;
                }
                if (i < n) {
                    out[i] = ' ';
                    i++;
                }
                const close = ')' + delimiter + '"';
                const found = source.indexOf(close, i);
                const end = found === -1 ? n : found + close.length;
                for (; i < end; i++) {
                    out[i] = source[i] === '\n' ? '\n' : ' ';
                }
                continue;
            }
            i++;
            continue;
        }

        if (state === 'line') {
            if (c === '\n') {
                out[i] = '\n';
                state = 'code';
            } else {
                out[i] = ' ';
            }
            i++;
            continue;
        }

        if (state === 'block') {
            if (c === '*' && d === '/') {
                out[i] = ' ';
                out[i + 1] = ' ';
                i += 2;
                state = 'code';
            } else {
                out[i] = c === '\n' ? '\n' : ' ';
                i++;
            }
            continue;
        }

        if (state === 'string' || state === 'char') {
            const quote = state === 'string' ? '"' : "'";
            if (c === '\\') {
                out[i] = ' ';
                if (i + 1 < n) {
                    out[i + 1] = source[i + 1] === '\n' ? '\n' : ' ';
                }
                i += 2;
                continue;
            }
            if (c === quote) {
                out[i] = ' ';
                i++;
                state = 'code';
                continue;
            }
            out[i] = c === '\n' ? '\n' : ' ';
            i++;
        }
    }

    return out.join('');
}

export function getAllFiles(dir, extensions = ['.cpp', '.hpp', '.h', '.mm']) {
    const results = [];
    if (!fs.existsSync(dir)) return results;
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const fullPath = path.join(dir, entry.name);
        if (entry.isDirectory()) {
            results.push(...getAllFiles(fullPath, extensions));
        } else if (extensions.includes(path.extname(entry.name).toLowerCase())) {
            results.push(fullPath);
        }
    }
    return results;
}

export function readSources(root, extensions) {
    return getAllFiles(path.join(root, 'src'), extensions).map((file) => {
        const raw = fs.readFileSync(file, 'utf8');
        const rel = path.relative(root, file).split(path.sep).join('/');
        return {
            file,
            rel,
            raw,
            code: stripCommentsAndStrings(raw),
            rawLines: raw.split('\n'),
            codeLines: stripCommentsAndStrings(raw).split('\n'),
        };
    });
}

export function parseOutputArgs(argv, defaults = {}) {
    const args = [...argv];
    const result = { ...defaults };
    for (let i = 0; i < args.length; i++) {
        if (args[i] === '--out' && args[i + 1]) {
            result.out = args[++i];
        } else if (args[i] === '--input' && args[i + 1]) {
            result.input = args[++i];
        } else if (args[i] === '--root' && args[i + 1]) {
            result.root = args[++i];
        }
    }
    return result;
}

export function writeJson(target, value) {
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.writeFileSync(target, JSON.stringify(value, null, 2) + '\n', 'utf8');
}

export function lineNumberAt(text, index) {
    let line = 1;
    for (let i = 0; i < index && i < text.length; i++) {
        if (text[i] === '\n') line++;
    }
    return line;
}

export function contextLines(lines, lineIndex, before = 6, after = 2) {
    const start = Math.max(0, lineIndex - before);
    const end = Math.min(lines.length, lineIndex + after + 1);
    return lines.slice(start, end).join('\n');
}
