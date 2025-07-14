/**
 * Service Worker for Furnace Control System
 * Provides offline capabilities and resource caching
 */

const CACHE_NAME = 'furnace-control-v1';
const STATIC_CACHE = 'furnace-static-v1';
const DYNAMIC_CACHE = 'furnace-dynamic-v1';

// Files to cache immediately
const STATIC_FILES = [
    '/',
    '/index.html',
    '/optimized-index.html',
    '/css/optimized-theme.css',
    '/css/theme.css',
    '/js/optimized-app.js',
    '/js/optimized-utils.js',
    '/js/theme.js',
    '/js/nav.js',
    '/js/utils.js',
    '/js/app.js',
    '/js/hammerjs.min.js',
    '/js/chart.min.js',
    '/js/chartjs-plugin-zoom.min.js',
    '/js/chartjs-date-bundle.js',
    '/favicon.ico',
    '/setup.html',
    '/settings.html',
    '/programs.html',
    '/profiles.html',
    '/filemanager.html'
];

// API endpoints to cache
const API_ENDPOINTS = [
    '/api/status',
    '/api/templog'
];

// Install event - cache static files
self.addEventListener('install', event => {
    console.log('Service Worker installing...');
    
    event.waitUntil(
        caches.open(STATIC_CACHE)
            .then(cache => {
                console.log('Caching static files');
                return cache.addAll(STATIC_FILES);
            })
            .then(() => {
                console.log('Static files cached successfully');
                return self.skipWaiting();
            })
            .catch(error => {
                console.error('Error caching static files:', error);
            })
    );
});

// Activate event - clean up old caches
self.addEventListener('activate', event => {
    console.log('Service Worker activating...');
    
    event.waitUntil(
        caches.keys()
            .then(cacheNames => {
                return Promise.all(
                    cacheNames.map(cacheName => {
                        if (cacheName !== STATIC_CACHE && 
                            cacheName !== DYNAMIC_CACHE && 
                            cacheName !== CACHE_NAME) {
                            console.log('Deleting old cache:', cacheName);
                            return caches.delete(cacheName);
                        }
                    })
                );
            })
            .then(() => {
                console.log('Service Worker activated');
                return self.clients.claim();
            })
    );
});

// Fetch event - serve from cache or network
self.addEventListener('fetch', event => {
    const { request } = event;
    const url = new URL(request.url);
    
    // Skip non-GET requests
    if (request.method !== 'GET') {
        return;
    }
    
    // Handle API requests
    if (url.pathname.startsWith('/api/')) {
        event.respondWith(handleApiRequest(request));
        return;
    }
    
    // Handle static file requests
    if (url.pathname.startsWith('/css/') || 
        url.pathname.startsWith('/js/') || 
        url.pathname.endsWith('.html') ||
        url.pathname.endsWith('.ico')) {
        event.respondWith(handleStaticRequest(request));
        return;
    }
    
    // Handle other requests
    event.respondWith(handleOtherRequest(request));
});

// Handle API requests with network-first strategy
async function handleApiRequest(request) {
    try {
        // Try network first
        const networkResponse = await fetch(request);
        
        // Cache successful responses
        if (networkResponse.ok) {
            const cache = await caches.open(DYNAMIC_CACHE);
            cache.put(request, networkResponse.clone());
        }
        
        return networkResponse;
    } catch (error) {
        console.log('Network failed for API request, trying cache:', request.url);
        
        // Fall back to cache
        const cachedResponse = await caches.match(request);
        if (cachedResponse) {
            return cachedResponse;
        }
        
        // Return offline response for API requests
        return new Response(JSON.stringify({
            error: 'Offline - No cached data available',
            offline: true
        }), {
            status: 503,
            headers: { 'Content-Type': 'application/json' }
        });
    }
}

// Handle static file requests with cache-first strategy
async function handleStaticRequest(request) {
    try {
        // Try cache first
        const cachedResponse = await caches.match(request);
        if (cachedResponse) {
            return cachedResponse;
        }
        
        // Fall back to network
        const networkResponse = await fetch(request);
        
        // Cache successful responses
        if (networkResponse.ok) {
            const cache = await caches.open(STATIC_CACHE);
            cache.put(request, networkResponse.clone());
        }
        
        return networkResponse;
    } catch (error) {
        console.log('Network failed for static request:', request.url);
        
        // Return offline page for HTML requests
        if (request.url.endsWith('.html')) {
            return caches.match('/offline.html');
        }
        
        return new Response('Offline', { status: 503 });
    }
}

// Handle other requests with network-first strategy
async function handleOtherRequest(request) {
    try {
        // Try network first
        const networkResponse = await fetch(request);
        
        // Cache successful responses
        if (networkResponse.ok) {
            const cache = await caches.open(DYNAMIC_CACHE);
            cache.put(request, networkResponse.clone());
        }
        
        return networkResponse;
    } catch (error) {
        console.log('Network failed for other request:', request.url);
        
        // Fall back to cache
        const cachedResponse = await caches.match(request);
        if (cachedResponse) {
            return cachedResponse;
        }
        
        return new Response('Offline', { status: 503 });
    }
}

// Background sync for offline actions
self.addEventListener('sync', event => {
    console.log('Background sync triggered:', event.tag);
    
    if (event.tag === 'temperature-update') {
        event.waitUntil(syncTemperatureUpdates());
    }
});

// Sync temperature updates when back online
async function syncTemperatureUpdates() {
    try {
        // Get pending updates from IndexedDB
        const pendingUpdates = await getPendingUpdates();
        
        for (const update of pendingUpdates) {
            try {
                const response = await fetch('/api/updateTemp', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(update)
                });
                
                if (response.ok) {
                    // Remove from pending updates
                    await removePendingUpdate(update.id);
                }
            } catch (error) {
                console.error('Failed to sync temperature update:', error);
            }
        }
    } catch (error) {
        console.error('Error syncing temperature updates:', error);
    }
}

// Store pending updates in IndexedDB
async function storePendingUpdate(update) {
    const db = await openDB();
    const transaction = db.transaction(['pendingUpdates'], 'readwrite');
    const store = transaction.objectStore('pendingUpdates');
    
    await store.add({
        id: Date.now(),
        data: update,
        timestamp: Date.now()
    });
}

// Get pending updates from IndexedDB
async function getPendingUpdates() {
    const db = await openDB();
    const transaction = db.transaction(['pendingUpdates'], 'readonly');
    const store = transaction.objectStore('pendingUpdates');
    
    return await store.getAll();
}

// Remove pending update from IndexedDB
async function removePendingUpdate(id) {
    const db = await openDB();
    const transaction = db.transaction(['pendingUpdates'], 'readwrite');
    const store = transaction.objectStore('pendingUpdates');
    
    await store.delete(id);
}

// Open IndexedDB
async function openDB() {
    return new Promise((resolve, reject) => {
        const request = indexedDB.open('FurnaceControlDB', 1);
        
        request.onerror = () => reject(request.error);
        request.onsuccess = () => resolve(request.result);
        
        request.onupgradeneeded = (event) => {
            const db = event.target.result;
            
            // Create object store for pending updates
            if (!db.objectStoreNames.contains('pendingUpdates')) {
                const store = db.createObjectStore('pendingUpdates', { keyPath: 'id' });
                store.createIndex('timestamp', 'timestamp', { unique: false });
            }
        };
    });
}

// Message handling for communication with main thread
self.addEventListener('message', event => {
    if (event.data && event.data.type === 'SKIP_WAITING') {
        self.skipWaiting();
    }
    
    if (event.data && event.data.type === 'CACHE_API_RESPONSE') {
        event.waitUntil(
            caches.open(DYNAMIC_CACHE)
                .then(cache => cache.put(event.data.request, event.data.response))
        );
    }
    
    if (event.data && event.data.type === 'STORE_PENDING_UPDATE') {
        event.waitUntil(storePendingUpdate(event.data.update));
    }
});

// Push notification handling
self.addEventListener('push', event => {
    console.log('Push notification received:', event);
    
    const options = {
        body: event.data ? event.data.text() : 'Furnace Control System Update',
        icon: '/favicon.ico',
        badge: '/favicon.ico',
        vibrate: [100, 50, 100],
        data: {
            dateOfArrival: Date.now(),
            primaryKey: 1
        },
        actions: [
            {
                action: 'explore',
                title: 'View Details',
                icon: '/favicon.ico'
            },
            {
                action: 'close',
                title: 'Close',
                icon: '/favicon.ico'
            }
        ]
    };
    
    event.waitUntil(
        self.registration.showNotification('Furnace Control System', options)
    );
});

// Notification click handling
self.addEventListener('notificationclick', event => {
    console.log('Notification clicked:', event);
    
    event.notification.close();
    
    if (event.action === 'explore') {
        event.waitUntil(
            clients.openWindow('/')
        );
    }
});

// Periodic background sync (if supported)
self.addEventListener('periodicsync', event => {
    console.log('Periodic background sync:', event);
    
    if (event.tag === 'temperature-sync') {
        event.waitUntil(syncTemperatureData());
    }
});

// Sync temperature data periodically
async function syncTemperatureData() {
    try {
        const response = await fetch('/api/templog');
        if (response.ok) {
            const cache = await caches.open(DYNAMIC_CACHE);
            cache.put('/api/templog', response.clone());
        }
    } catch (error) {
        console.error('Error syncing temperature data:', error);
    }
} 