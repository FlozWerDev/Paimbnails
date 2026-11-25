import path from 'path';
import { readSources, parseOutputArgs, writeJson } from './lib/source-scan.mjs';

const options = parseOutputArgs(process.argv.slice(2), {
    root: path.resolve('.'),
    out: path.resolve('fs_path_findings.json'),
});

const sources = readSources(options.root, ['.cpp', '.hpp', '.h', '.mm']);

const OPERATIONS = [
    { name: 'create_directories', regex: /\bcreate_directories\s*\(/g },
    { name: 'create_directory', regex: /\bcreate_directory\s*\(/g },
    { name: 'remove_all', regex: /\bremove_all\s*\(/g },
    { name: 'remove', regex: /\bremove\s*\(/g },
    { name: 'rename', regex: /\brename\s*\(/g },
    { name: 'copy_file', regex: /\bcopy_file\s*\(/g },
    { name: 'copy', regex: /\bcopy\s*\(/g },
    { name: 'ofstream', regex: /\b(?:std::)?ofstream\b/g },
    { name: 'fopen', regex: /\bfopen\s*\(/g },
    { name: 'writeFile', regex: /\bwriteFile\w*\s*\(/g },
    { name: 'writeBinary', regex: /\bwriteBinary\w*\s*\(/g },
    { name: 'writeAtomically', regex: /\bwriteAtomically\w*\s*\(/g },
    { name: 'readBinary', regex: /\breadBinary\w*\s*\(/g },
    { name: 'readFromFile', regex: /\breadFromFile\w*\s*\(/g },
    { name: 'directory_iterator', regex: /\bdirectory_iterator\b/g },
];

const CONTAINMENT = /weakly_canonical|lexically_normal|isWithin|withinRoot|startsWith|safeJoin|sanitize|validatePath|checkedPath|ensureInside|canonicalPath|resolveSlot|projectPath|slotPath/i;

function argumentAt(code, openParen) {
    let depth = 0;
    for (let i = openParen; i < code.length; i++) {
        if (code[i] === '(') depth++;
        else if (code[i] === ')') {
            depth--;
            if (depth === 0) return code.slice(openParen + 1, i);
        }
    }
    return code.slice(openParen + 1, Math.min(code.length, openParen + 220));
}

const findings = [];

for (const source of sources) {
    for (let i = 0; i < source.codeLines.length; i++) {
        const code = source.codeLines[i];
        for (const operation of OPERATIONS) {
            operation.regex.lastIndex = 0;
            let match;
            while ((match = operation.regex.exec(code)) !== null) {
                if ((operation.name === 'remove' || operation.name === 'copy')
                    && /std::(?:remove|copy)\s*$/.test(code.slice(0, match.index))) {
                    continue;
                }
                const openParen = code.indexOf('(', match.index + match[0].length);
                if (openParen === -1) continue;

                const argument = argumentAt(code, openParen).trim();
                const dynamic = /[A-Za-z_]\w*/.test(argument)
                    && /\+|fmt::format|to_string|\.string\s*\(|path\s*\(|id|name|key|slot|project/i.test(argument);
                const destructive = operation.name === 'remove_all' || operation.name === 'remove'
                    || operation.name === 'rename' || operation.name === 'copy_file';
                if (!dynamic && !destructive) continue;

                const near = source.codeLines.slice(Math.max(0, i - 8), Math.min(source.codeLines.length, i + 3)).join('\n');
                const contained = CONTAINMENT.test(near);

                if (contained) continue;

                findings.push({
                    file: source.rel,
                    line: i + 1,
                    operation: operation.name,
                    argument,
                    destructive,
                    text: source.rawLines[i].trim(),
                    context: source.rawLines.slice(Math.max(0, i - 2), Math.min(source.rawLines.length, i + 3)).join('\n'),
                });
            }
        }
    }
}

const byOperation = findings.reduce((acc, finding) => {
    acc[finding.operation] = (acc[finding.operation] || 0) + 1;
    return acc;
}, {});

writeJson(options.out, { byOperation, findings });
console.log('Filesystem path candidates:', byOperation);
console.log(`Written to ${options.out}`);
