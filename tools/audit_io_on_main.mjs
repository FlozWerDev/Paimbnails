import fs from 'fs';
import path from 'path';

const ROOT = path.resolve('.');
const HOOKS_DIR = path.join(ROOT, 'src', 'hooks');

function getAllFiles(dir) {
    let results = [];
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const fullPath = path.join(dir, entry.name);
        if (entry.isDirectory()) results = results.concat(getAllFiles(fullPath));
        else if (['.cpp', '.hpp'].includes(path.extname(entry.name).toLowerCase())) results.push(fullPath);
    }
    return results;
}

const hookFiles = getAllFiles(HOOKS_DIR);
const ioCalls = [];

const ioPatterns = [
    'std::ifstream', 'std::ofstream', 'readBinary', 'writeBinary', 'writeAtomically',
    'std::filesystem::copy', 'std::filesystem::remove', 'std::filesystem::create_directories',
    'std::filesystem::directory_iterator', 'std::filesystem::recursive_directory_iterator',
    'fopen', 'fwrite', 'fread'
];

for (const file of hookFiles) {
    const relPath = path.relative(ROOT, file);
    const lines = fs.readFileSync(file, 'utf8').split('\n');

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        if (line.trim().startsWith('//')) continue;
        for (const pat of ioPatterns) {
            if (line.includes(pat)) {
                // Check if this is inside a worker thread or queue
                const context = lines.slice(Math.max(0, i - 15), i + 1).join('\n');
                const isAsync = context.includes('std::thread') || context.includes('push(') || context.includes('spawn(');
                if (!isAsync) {
                    ioCalls.push({
                        file: relPath,
                        line: i + 1,
                        pattern: pat,
                        text: line.trim()
                    });
                }
            }
        }
    }
}

console.log(`Found ${ioCalls.length} file I/O operations in hooks/ :`);
for (const io of ioCalls) {
    console.log(`- ${io.file}:${io.line} [${io.pattern}] -> ${io.text}`);
}
