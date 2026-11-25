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
const nullRisks = [];

for (const file of files) {
    const relPath = path.relative(ROOT, file);
    const lines = fs.readFileSync(file, 'utf8').split('\n');

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        if (line.trim().startsWith('//')) continue;

        // Check for PlayLayer::get()->
        if (line.includes('PlayLayer::get()->')) {
            const context = lines.slice(Math.max(0, i - 4), i + 1).join('\n');
            if (!context.includes('if (PlayLayer::get())') && !context.includes('if (auto pl = PlayLayer::get())')) {
                nullRisks.push({
                    file: relPath,
                    line: i + 1,
                    text: line.trim(),
                    kind: 'PlayLayer::get()'
                });
            }
        }

        // Check for GameManager::get()-> or GameManager::sharedState()-> without check
        if (line.includes('GameManager::get()->') || line.includes('GameManager::sharedState()->')) {
            const context = lines.slice(Math.max(0, i - 4), i + 1).join('\n');
            if (!context.includes('if (GameManager::get())') && !context.includes('if (auto gm = GameManager::get())') && !context.includes('if (GameManager::sharedState())')) {
                nullRisks.push({
                    file: relPath,
                    line: i + 1,
                    text: line.trim(),
                    kind: 'GameManager::get()'
                });
            }
        }

        // Check for CCDirector::get()->getRunningScene()-> without null check
        if (line.includes('getRunningScene()->')) {
            const context = lines.slice(Math.max(0, i - 4), i + 1).join('\n');
            if (!context.includes('if (scene)') && !context.includes('if (auto scene') && !context.includes('if (!scene)')) {
                nullRisks.push({
                    file: relPath,
                    line: i + 1,
                    text: line.trim(),
                    kind: 'getRunningScene()'
                });
            }
        }
    }
}

console.log(`Found ${nullRisks.length} null dereference risks:`);
for (const r of nullRisks) {
    console.log(`- [${r.kind}] ${r.file}:${r.line} -> ${r.text}`);
}
