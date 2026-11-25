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
const moduloFindings = [];

for (const file of files) {
    const relPath = path.relative(ROOT, file);
    const lines = fs.readFileSync(file, 'utf8').split('\n');

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        if (line.trim().startsWith('//')) continue;
        
        // Check for modulo by variable or .size()
        const match = line.match(/%\s*([a-zA-Z_]\w*(?:\.\w+\(\)|\.size\(\)|\->\w+\(\))*)/);
        if (match) {
            const divisor = match[1];
            if (divisor === '100' || divisor === '60' || divisor === '10' || divisor === '2' || divisor === '256' || divisor === '8') continue;
            
            const context = lines.slice(Math.max(0, i - 10), i + 1).join('\n');
            const hasCheck = context.includes('empty()') || context.includes('size() > 0') || context.includes('size() == 0') || context.includes('== 0') || context.includes('<= 0') || context.includes('> 0');
            if (!hasCheck) {
                moduloFindings.push({
                    file: relPath,
                    line: i + 1,
                    divisor: divisor,
                    text: line.trim()
                });
            }
        }
    }
}

console.log(`Found ${moduloFindings.length} potentially unsafe modulo operations:`);
for (const m of moduloFindings) {
    console.log(`- ${m.file}:${m.line} (% ${m.divisor}) -> ${m.text}`);
}
