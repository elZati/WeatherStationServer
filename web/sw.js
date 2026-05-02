const CACHE = 'rxtx-v1';

const PRECACHE = [
    './graph.html',
    './weather.css',
    'https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js',
    'https://cdn.jsdelivr.net/npm/chartjs-adapter-date-fns@3.0.0/dist/chartjs-adapter-date-fns.bundle.min.js',
];

// Pre-cache static assets on install
self.addEventListener('install', event => {
    event.waitUntil(
        caches.open(CACHE)
            .then(cache => cache.addAll(PRECACHE))
            .then(() => self.skipWaiting())
    );
});

// Remove old caches on activate
self.addEventListener('activate', event => {
    event.waitUntil(
        caches.keys()
            .then(keys => Promise.all(
                keys.filter(k => k !== CACHE).map(k => caches.delete(k))
            ))
            .then(() => self.clients.claim())
    );
});

self.addEventListener('fetch', event => {
    const url = new URL(event.request.url);

    // API and live data: network-first, fall back to last cached response
    const isLive = url.pathname.includes('datafetcher.php')
                || url.hostname.includes('open-meteo.com')
                || url.hostname.includes('geocoding-api');

    if (isLive) {
        event.respondWith(
            fetch(event.request)
                .then(resp => {
                    caches.open(CACHE).then(c => c.put(event.request, resp.clone()));
                    return resp;
                })
                .catch(() => caches.match(event.request)
                    .then(cached => cached || new Response('{}',
                        { headers: { 'Content-Type': 'application/json' } }
                    ))
                )
        );
        return;
    }

    // Everything else: cache-first, update cache in background
    event.respondWith(
        caches.match(event.request).then(cached => {
            const network = fetch(event.request).then(resp => {
                caches.open(CACHE).then(c => c.put(event.request, resp.clone()));
                return resp;
            });
            return cached || network;
        })
    );
});
