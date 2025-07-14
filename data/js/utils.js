// File: data/js/utils.js
// Description: Contains utility functions shared across the application.

/**
 * Fetches a resource with a specified number of retries on failure.
 * @param {string} url - The URL to fetch.
 * @param {object} options - The options for the fetch request.
 * @param {number} retries - The number of times to retry the fetch.
 * @param {number} delay - The delay between retries in milliseconds.
 * @returns {Promise<Response>} - A promise that resolves with the fetch response.
 */
async function fetchWithRetry(url, options = {}, retries = 3, delay = 1000) {
    for (let i = 0; i < retries; i++) {
        try {
            const response = await fetch(url, options);
            if (!response.ok) {
                throw new Error(`HTTP error! status: ${response.status}`);
            }
            return response;
        } catch (error) {
            if (i < retries - 1) {
                await new Promise(resolve => setTimeout(resolve, delay));
            } else {
                throw error; // Rethrow the last error
            }
        }
    }
}
