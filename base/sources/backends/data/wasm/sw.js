
const ASSETS_TO_CACHE = [ '/index.html', '/start.js', '/start.wasm', '/worker.js', '/manifest.json', '/icon.png' ];

self.addEventListener('install', (event) => { event.waitUntil(caches.open('armorpaint-1').then((cache) => { return cache.addAll(ASSETS_TO_CACHE); })); });

self.addEventListener('fetch', (event) => { event.respondWith(fetch(event.request).catch(() => { return caches.match(event.request); })); });
