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
console.log(`Auditing thread safety across ${files.length} files...`);

const cocosOrGlCalls = [
    'addChild', 'removeChild', 'removeFromParent', 'setPosition', 'setContentSize', 'setVisible',
    'setTexture', 'setScale', 'setRotation', 'setOpacity', 'runAction', 'stopAction',
    'CCSprite::create', 'CCNode::create', 'CCLabelBMFont::create', 'CCMenu::create', 'CCMenuItem::create',
    'CCTextureCache::sharedTextureCache', 'CCDirector::sharedDirector', 'CCDirector::get',
    'glBindTexture', 'glTexImage2D', 'glGenTextures', 'glDeleteTextures', 'glUseProgram',
    'glBindFramebuffer', 'glViewport', 'glClear', 'glDrawArrays', 'glDrawElements'
];

const findings = [];

for (const file of files) {
    const relPath = path.relative(ROOT, file);
    const text = fs.readFileSync(file, 'utf8');
    
    // Look for std::thread or ThreadPool or async
    const threadRegex = /(?:std::thread\s*\(\s*\[|m_threadPool->push\s*\(\s*\[|ThreadPool::get\(\)\.?push\s*\(\s*\[|pushTask\s*\(\s*\[|std::async\s*\(\s*\[)/g;
    let match;
    while ((match = threadRegex.exec(text)) !== null) {
        const start = match.index;
        // find matching closing brace/paren for the lambda
        let depth = 0;
        let bodyStart = text.indexOf('{', start);
        if (bodyStart === -1 || bodyStart - start > 100) continue;
        
        let inString = false;
        let inComment = false;
        let i = bodyStart;
        depth = 1;
        while (++i < text.length && depth > 0) {
            const ch = text[i];
            const next = text[i+1];
            if (inComment) {
                if (ch === '\n') inComment = false;
                continue;
            }
            if (ch === '/' && next === '/') {
                inComment = true;
                continue;
            }
            if (ch === '"' && text[i-1] !== '\\') inString = !inString;
            if (inString) continue;

            if (ch === '{') depth++;
            else if (ch === '}') depth--;
        }

        const lambdaBody = text.slice(bodyStart, i);
        // Now check if lambdaBody contains cocos or GL calls OUTSIDE of queueInMainThread
        // Split by queueInMainThread to see if there are calls outside
        const outsideMainThread = lambdaBody.replace(/queueInMainThread\s*\(\s*\[[\s\S]*?\}\s*\)/g, '/* QUEUED_IN_MAIN_THREAD */');
        
        for (const call of cocosOrGlCalls) {
            const callIdx = outsideMainThread.indexOf(call);
            if (callIdx !== -1) {
                // Find line number
                const line = text.slice(0, start).split('\n').length;
                findings.push({
                    file: relPath,
                    line: line,
                    call: call,
                    context: outsideMainThread.slice(Math.max(0, callIdx - 40), callIdx + 60).replace(/\s+/g, ' ')
                });
            }
        }
    }
}

console.log(`Found ${findings.length} potential thread-safety concerns:`);
for (const f of findings) {
    console.log(`- ${f.file}:${f.line} -> call '${f.call}': ${f.context}`);
}
fs.writeFileSync('thread_safety_findings.json', JSON.stringify(findings, null, 2));
