/* Bik'air — minimal service worker
 * Caches the shell so the dashboard works as installable PWA.
 * Live data still goes via WebSocket (network only).
 */
const CACHE = 'bikair-v1';
const SHELL = [
    '/',
    '/index.html',
    '/files.html',
    '/style.css',
    '/script.js',
    '/manifest.json',
    '/icon.svg',
    '/favicon.ico',
];

self.addEventListener('install', (event) => {
    event.waitUntil(
        caches.open(CACHE).then((c) => c.addAll(SHELL)).catch(() => {})
    );
    self.skipWaiting();
});

self.addEventListener('activate', (event) => {
    event.waitUntil(
        caches.keys().then((keys) => Promise.all(
            keys.filter((k) => k !== CACHE).map((k) => caches.delete(k))
        ))
    );
    self.clients.claim();
});

self.addEventListener('fetch', (event) => {
    const req = event.request;
    if (req.method !== 'GET') return;

    const url = new URL(req.url);
    // Never cache live API / WebSocket / control endpoints
    if (url.pathname.startsWith('/api/') ||
        url.pathname.startsWith('/ws') ||
        ['/status', '/get-interval', '/set-interval', '/set-time',
         '/startstopmeas', '/sleep', '/download', '/delete', '/history',
         '/list-files'].includes(url.pathname)) {
        return; // Let the network handle it
    }

    // Cache-first for shell, fallback to network
    event.respondWith(
        caches.match(req).then((cached) => {
            if (cached) return cached;
            return fetch(req).then((resp) => {
                if (resp && resp.status === 200 && resp.type === 'basic') {
                    const copy = resp.clone();
                    caches.open(CACHE).then((c) => c.put(req, copy)).catch(() => {});
                }
                return resp;
            }).catch(() => caches.match('/index.html'));
        })
    );
});
