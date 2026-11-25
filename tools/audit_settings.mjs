import fs from 'fs';
import path from 'path';

const ROOT = path.resolve('.');
const SRC_DIR = path.join(ROOT, 'src');

const modJson = JSON.parse(fs.readFileSync(path.join(ROOT, 'mod.json'), 'utf8'));
const declaredSettings = new Set(Object.keys(modJson.settings || {}));

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
const missingFromModJson = new Map();
const foundInCpp = new Set();

const settingRegex = /(?:getSettingValue|setSettingValue|listenForSettingChanges|hasSetting)\s*<[^>]*>\s*\(\s*"([^"]+)"\s*\)|listenForSettingChanges\s*\(\s*"([^"]+)"\s*,/g;

for (const file of files) {
    const relPath = path.relative(ROOT, file);
    const text = fs.readFileSync(file, 'utf8');
    let match;
    while ((match = settingRegex.exec(text)) !== null) {
        const key = match[1] || match[2];
        foundInCpp.add(key);
        if (!declaredSettings.has(key)) {
            if (!missingFromModJson.has(key)) missingFromModJson.set(key, []);
            missingFromModJson.get(key).push(relPath);
        }
    }
}

console.log(`Declared settings in mod.json: ${declaredSettings.size}`);
console.log(`Unique settings referenced in C++: ${foundInCpp.size}`);
console.log(`Settings in C++ that are NOT in mod.json: ${missingFromModJson.size}`);
for (const [k, flist] of missingFromModJson.entries()) {
    console.log(`- "${k}" (used in ${flist[0]})`);
}
