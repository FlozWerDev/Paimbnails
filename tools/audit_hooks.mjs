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
const hooks = [];

for (const file of files) {
    const relPath = path.relative(ROOT, file);
    const content = fs.readFileSync(file, 'utf8');

    const modifyRegex = /(?:class|struct)\s+\$modify\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)\s*\{/g;
    let match;
    while ((match = modifyRegex.exec(content)) !== null) {
        const hookName = match[1];
        const targetClass = match[2];
        const hookStart = match.index;

        // find closing brace of class
        let depth = 1;
        let i = content.indexOf('{', hookStart) + 1;
        while (i < content.length && depth > 0) {
            if (content[i] === '{') depth++;
            else if (content[i] === '}') depth--;
            i++;
        }
        const hookBody = content.slice(hookStart, i);

        // find methods
        const methodRegex = /(?:(\$override|virtual|override)\s+)?(?:(bool|void|int|float|double|auto|cocos2d::\w+\*|\w+\*)\s+)?(\w+)\s*\(([^)]*)\)\s*(?:override\s*)?\{/g;
        let mMatch;
        const methods = [];
        while ((mMatch = methodRegex.exec(hookBody)) !== null) {
            const isOverride = !!mMatch[1] || hookBody.slice(mMatch.index, mMatch.index + mMatch[0].length).includes('override');
            const returnType = mMatch[2] || '';
            const methodName = mMatch[3];
            const params = mMatch[4];

            if (methodName === 'onModify' || methodName === 'Fields' || methodName === 'if' || methodName === 'while') continue;

            // check if method body calls BaseClass::methodName
            let mDepth = 1;
            let mBodyStart = hookBody.indexOf('{', mMatch.index) + 1;
            let j = mBodyStart;
            while (j < hookBody.length && mDepth > 0) {
                if (hookBody[j] === '{') mDepth++;
                else if (hookBody[j] === '}') mDepth--;
                j++;
            }
            const methodBody = hookBody.slice(mBodyStart, j);
            const callsBase = methodBody.includes(`${targetClass}::${methodName}`);

            methods.push({
                name: methodName,
                returnType: returnType,
                callsBase: callsBase,
                hasOverride: isOverride
            });
        }

        hooks.push({
            file: relPath,
            hookName,
            targetClass,
            methods
        });
    }
}

console.log(`Audited ${hooks.length} $modify hooks across the codebase.`);
const overridesWithoutBaseCall = [];
for (const h of hooks) {
    for (const m of h.methods) {
        if (!m.callsBase && (m.name === 'init' || m.name === 'onEnter' || m.name === 'onExit' || m.name === 'customSetup' || m.name === 'update')) {
            overridesWithoutBaseCall.push({
                file: h.file,
                hook: h.hookName,
                target: h.targetClass,
                method: m.name
            });
        }
    }
}

console.log(`Lifecycle methods without calling base: ${overridesWithoutBaseCall.length}`);
for (const o of overridesWithoutBaseCall) {
    console.log(`- ${o.file} in $modify(${o.hook}, ${o.target}): ${o.method}() does not call ${o.target}::${o.method}()`);
}
