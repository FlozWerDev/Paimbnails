(() => {
    'use strict';
    const audioListeners = new Set();
    const deliverAudio = values => {
        if (!Array.isArray(values) || values.length === 0) return;
        for (const listener of audioListeners) {
            try { listener(values); } catch (error) { console.error(error); }
        }
    };
    window.wallpaperRegisterAudioListener = listener => {
        if (typeof listener === 'function') audioListeners.add(listener);
    };
    // Spectrum frames arrive as JSON web messages (no script compile per frame).
    window.chrome?.webview?.addEventListener('message', event => deliverAudio(event.data));
    // kept for pages that call it directly.
    window.__paimonWallpaperAudio = deliverAudio;

    // Wallpaper Engine media integration has no source here; registering must
    // still succeed so pages that call these during start-up keep running.
    const noop = () => {};
    for (const name of [
        'wallpaperRegisterMediaStatusListener',
        'wallpaperRegisterMediaPropertiesListener',
        'wallpaperRegisterMediaThumbnailListener',
        'wallpaperRegisterMediaPlaybackListener',
        'wallpaperRegisterMediaTimelineListener',
    ]) {
        if (typeof window[name] !== 'function') window[name] = noop;
    }
    if (typeof window.wallpaperRequestRandomFileForProperty !== 'function') {
        window.wallpaperRequestRandomFileForProperty = noop;
    }

    // Authors assign window.wallpaperPropertyListener at top level, in
    // DOMContentLoaded or in onload. Wallpaper Engine hands the project
    // defaults to whichever listener exists once the page is ready, so watch
    // the assignment instead of reading it at one fixed moment.
    let listener;
    let properties = null;
    let domReady = false;
    let flushQueued = false;
    const delivered = new WeakSet();
    const flush = () => {
        flushQueued = false;
        if (!domReady || !properties || !listener) return;
        if (typeof listener !== 'object' && typeof listener !== 'function') return;
        if (delivered.has(listener)) return;
        delivered.add(listener);
        try { listener.applyUserProperties?.(properties.user); } catch (error) { console.error('Wallpaper properties:', error); }
        // Authors read this to pace their own animation loop.
        try { listener.applyGeneralProperties?.(properties.general); } catch (error) { console.error('Wallpaper properties:', error); }
    };
    // never call back into the page from inside its own assignment or
    // DOMContentLoaded handler: it may still be initialising.
    const scheduleFlush = () => {
        if (flushQueued) return;
        flushQueued = true;
        setTimeout(flush, 0);
    };
    try {
        Object.defineProperty(window, 'wallpaperPropertyListener', {
            configurable: true,
            enumerable: true,
            get: () => listener,
            set: value => { listener = value; scheduleFlush(); },
        });
    } catch (error) { console.error('Wallpaper properties:', error); }

    const markReady = () => { domReady = true; scheduleFlush(); };
    if (document.readyState === 'loading') {
        window.addEventListener('DOMContentLoaded', markReady, {once: true});
    } else {
        markReady();
    }
    fetch('/project.json')
        .then(response => (response.ok ? response.json() : null))
        .then(project => {
            properties = {user: project?.general?.properties ?? {}, general: {fps: 60}};
            scheduleFlush();
        })
        .catch(error => {
            console.error('Wallpaper properties:', error);
            properties = {user: {}, general: {fps: 60}};
            scheduleFlush();
        });
})();
