import fs from 'fs';
import path from 'path';

const ROOT = path.resolve('.');
const SRC_DIR = path.join(ROOT, 'src');

function getAllFiles(dir) {
    let results = [];
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const fullPath = path.join(dir, entry.name);
        if (entry.isDirectory()) results = results.concat(getAllFiles(fullPath));
        else if (['.cpp', '.hpp', '.h'].includes(path.extname(entry.name).toLowerCase())) results.push(fullPath);
    }
    return results;
}

const files = getAllFiles(SRC_DIR);
const savedValueCalls = [];

for (const file of files) {
    const relPath = path.relative(ROOT, file);
    const text = fs.readFileSync(file, 'utf8');
    const lines = text.split('\n');

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        const match = line.match(/getSavedValue\s*<([^>]+)>\s*\(\s*"([^"]+)"\s*([,)])/);
        if (match) {
            const hasDefault = match[3] === ',';
            savedValueCalls.push({
                file: relPath,
                line: i + 1,
                type: match[1],
                key: match[2],
                hasDefault: hasDefault,
                text: line.trim()
            });
        }
    }
}

const withoutDefault = savedValueCalls.filter(c => !c.hasDefault);
console.log(`Total getSavedValue calls: ${savedValueCalls.length}`);
console.log(`Calls without default: ${withoutDefault.length}`);
for (const c of withoutDefault) {
    console.log(`- ${c.file}:${c.line} <${c.type}>("${c.key}")`);
}
