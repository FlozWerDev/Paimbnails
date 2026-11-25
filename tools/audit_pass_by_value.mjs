import fs from 'fs';
import path from 'path';

const ROOT = path.resolve('.');
const SRC_DIR = path.join(ROOT, 'src');

function getAllFiles(dir) {
    let results = [];
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const fullPath = path.join(dir, entry.name);
        if (entry.isDirectory()) results = results.concat(getAllFiles(fullPath));
        else if (['.cpp', '.hpp'].includes(path.extname(entry.name).toLowerCase())) results.push(fullPath);
    }
    return results;
}

const files = getAllFiles(SRC_DIR);
const passByValueVectors = [];

for (const file of files) {
    const relPath = path.relative(ROOT, file);
    const lines = fs.readFileSync(file, 'utf8').split('\n');

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        if (line.trim().startsWith('//')) continue;

        // Check for (std::vector<...> name) without &
        const match = line.match(/\(\s*(?:const\s+)?std::vector<[^>]+>\s+([a-zA-Z_]\w*)\s*[,)]/);
        if (match) {
            // make sure it doesn't have & or &&
            const matchedFull = match[0];
            if (!matchedFull.includes('&') && !matchedFull.includes('std::move')) {
                passByValueVectors.push({
                    file: relPath,
                    line: i + 1,
                    text: line.trim()
                });
            }
        }
    }
}

console.log(`Found ${passByValueVectors.length} vector pass-by-value candidates:`);
for (const p of passByValueVectors.slice(0, 20)) {
    console.log(`- ${p.file}:${p.line} -> ${p.text}`);
}
