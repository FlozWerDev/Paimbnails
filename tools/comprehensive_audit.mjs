import path from 'path';
import { readSources, parseOutputArgs, writeJson } from './lib/source-scan.mjs';

const options = parseOutputArgs(process.argv.slice(2), {
    root: path.resolve('.'),
    out: path.resolve('audit_results.json'),
});

const sources = readSources(options.root, ['.cpp', '.hpp', '.h', '.mm']);

const issues = {
    stoi_stod_without_try: [],
    result_unwrap_unchecked: [],
    matjson_unwrap_unchecked: [],
    getchild_unchecked: [],
    typeinfo_cast_unchecked: [],
    running_scene_unchecked: [],
    playlayer_get_unchecked: [],
    retain_without_release: [],
    queue_main_thread_raw_this: [],
    new_cocos_node_leaks: [],
    hook_missing_base_call: [],
};

function context(lines, index, before, after) {
    return lines.slice(Math.max(0, index - before), Math.min(lines.length, index + after + 1)).join('\n');
}

for (const source of sources) {
    let tryDepth = 0;
    let hasRetain = false;
    let hasRelease = false;

    for (let i = 0; i < source.codeLines.length; i++) {
        const code = source.codeLines[i];
        const raw = source.rawLines[i];

        if (/\btry\b/.test(code)) tryDepth++;
        if (/\bcatch\b/.test(code)) tryDepth = Math.max(0, tryDepth - 1);

        const stoi = code.match(/\bstd::(stoi|stol|stoll|stod|stof)\s*\(([^)]*)\)/);
        if (stoi && tryDepth === 0) {
            const near = context(source.codeLines, i, 5, 0);
            if (!/all_of|isdigit|empty\s*\(\)|find_first_not_of|regex|canParse|isNumber|is_digit|validInt|from_chars/i.test(near)) {
                issues.stoi_stod_without_try.push({ file: source.rel, line: i + 1, text: raw.trim() });
            }
        }

        const unwrap = code.match(/(\w+)\s*\.\s*unwrap\s*\(\s*\)/);
        if (unwrap) {
            const near = context(source.codeLines, i, 15, 1);
            const variable = unwrap[1];
            const escaped = variable.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
            const guarded = new RegExp(`${escaped}\\s*\\.\\s*isOk\\s*\\(`).test(near)
                || new RegExp(`${escaped}\\s*\\.\\s*has_value\\s*\\(`).test(near)
                || new RegExp(`!\\s*${escaped}\\s*\\.\\s*isErr\\s*\\(`).test(near)
                || new RegExp(`if\\s*\\(\\s*!?\\s*${escaped}\\s*(\\.isOk\\(\\))?\\s*\\)`).test(near)
                || new RegExp(`if\\s*\\(\\s*auto\\s+\\w+\\s*=\\s*${escaped}`).test(near)
                || new RegExp(`${escaped}\\s*\\?`).test(near)
                || /(?:GEODE_)?[Aa]ssert\s*\(/.test(near)
                || code.includes('unwrapOr(');
            if (!guarded) {
                issues.result_unwrap_unchecked.push({
                    file: source.rel,
                    line: i + 1,
                    text: raw.trim(),
                    var: variable,
                });
            }
        }

        if (/\bgetChildBy(?:ID|Tag|Type)\s*\(/.test(code)) {
            const near = context(source.codeLines, i, 3, 2);
            if (!/\bif\s*\(|\bauto\s+\w+\s*=|nullptr|assert|static_cast/.test(near)) {
                issues.getchild_unchecked.push({ file: source.rel, line: i + 1, text: raw.trim() });
            }
        }

        if (/\b(typeinfo_cast|geode::cast::typeinfo_cast)\s*<[^>]+>\s*\(/.test(code)) {
            const near = context(source.codeLines, i, 3, 2);
            if (!/\bif\s*\(|\bauto\s+\w+\s*=|nullptr|assert/.test(near)) {
                issues.typeinfo_cast_unchecked.push({ file: source.rel, line: i + 1, text: raw.trim() });
            }
        }

        if (code.includes('getRunningScene()->') || code.includes('CCDirector::get()->getRunningScene()->')) {
            const near = context(source.codeLines, i, 4, 1);
            if (!/\bif\s*\(|\bauto\s+\w+\s*=|nullptr|assert/.test(near)) {
                issues.running_scene_unchecked.push({ file: source.rel, line: i + 1, text: raw.trim() });
            }
        }

        if (code.includes('PlayLayer::get()->')) {
            const near = context(source.codeLines, i, 4, 1);
            if (!/\bif\s*\(|\bauto\s+\w+\s*=|nullptr|assert/.test(near)) {
                issues.playlayer_get_unchecked.push({ file: source.rel, line: i + 1, text: raw.trim() });
            }
        }

        if (/\bretain\s*\(\s*\)/.test(code)) hasRetain = true;
        if (/\brelease\s*\(\s*\)/.test(code)) hasRelease = true;

        if (/queueInMainThread\s*\(\s*\[/.test(code)) {
            const near = context(source.codeLines, i, 0, 3);
            const capture = near.match(/queueInMainThread\s*\(\s*\[([^\]]*)\]/);
            const captureText = capture ? capture[1] : '';
            const rawThis = /(?:^|,)\s*(?:this|self\s*=\s*this)\s*(?:,|$)/.test(captureText)
                || captureText.trim() === 'this'
                || /^\s*[=&]\s*$/.test(captureText);
            if (rawThis && !/WeakRef|Ref\s*</.test(near)) {
                issues.queue_main_thread_raw_this.push({ file: source.rel, line: i + 1, text: raw.trim() });
            }
        }

        if (/\bnew\s+(?:CC|geode::|cocos2d::)?(?:Sprite|Node|Label|Menu|MenuItem|Layer|Scene|DrawNode|Particle)\w*\s*\(/.test(code)) {
            const near = context(source.codeLines, i, 2, 3);
            if (!/addChild|addTo|autorelease|Ref\s*<|retain\s*\(|std::unique_ptr|std::shared_ptr/.test(near)) {
                issues.new_cocos_node_leaks.push({ file: source.rel, line: i + 1, text: raw.trim() });
            }
        }
    }

    if (hasRetain && !hasRelease) {
        issues.retain_without_release.push({ file: source.rel, line: 1, text: 'file calls retain() without release()' });
    }
}

const summary = Object.fromEntries(Object.entries(issues).map(([key, value]) => [key, value.length]));
writeJson(options.out, issues);
console.log('Scan summary:', summary);
console.log(`Written to ${options.out}`);
