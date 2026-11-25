import fs from 'fs';
import path from 'path';

const ROOT = path.resolve('.');
const SRC_DIR = path.join(ROOT, 'src');

function getAllFiles(dir) {
    let results = [];
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const fullPath = path.join(dir, entry.name);
        if (entry.isDirectory()) results = results.concat(getAllFiles(fullPath));
        else if (['.cpp', '.hpp', '.h', '.mm'].includes(path.extname(entry.name).toLowerCase())) results.push(fullPath);
    }
    return results;
}

const files = getAllFiles(SRC_DIR);
const retainSites = [];

for (const file of files) {
    const relPath = path.relative(ROOT, file);
    const content = fs.readFileSync(file, 'utf8');
    const lines = content.split('\n');

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        if (line.includes('->retain()') || line.includes('.retain()') || line.trim() === 'retain();' || line.trim() === 'this->retain();') {
            // Check if file has release() or CC_SAFE_RELEASE
            const hasRelease = content.includes('release()') || content.includes('CC_SAFE_RELEASE');
            retainSites.push({
                file: relPath,
                line: i + 1,
                text: line.trim(),
                hasReleaseInFile: hasRelease
            });
        }
    }
}

console.log(`Found ${retainSites.length} retain() call sites.`);
const withoutRelease = retainSites.filter(r => !r.hasReleaseInFile);
console.log(`Retains in files WITHOUT any release(): ${withoutRelease.length}`);
for (const w of withoutRelease) {
    console.log(`- ${w.file}:${w.line}: ${w.text}`);
}
