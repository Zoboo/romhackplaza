# Romhack Plaza

A small C++11 library for emulator frontends. It searches Romhack Plaza, reads entry metadata and signed patch links, and applies supported patches to separate output files. It does not include a ROM collection or download base ROMs.

Link the `romhackplaza` CMake target. Dependencies are libcurl and zlib. Call network and patch functions from a worker thread; progress callbacks return false to cancel. Initialize libcurl once before starting workers. API keys belong in application settings, never in source code. Use a key with `entries:read` and `entries:download` scopes.

Call `ReadApiKeyFile(emulatorDirectory, token)` to load `<emulatorDirectory>/romhackingplaza/api-key.txt`. It creates the `romhackingplaza` directory and an empty key file if they are missing, without changing existing files. It accepts a single printable ASCII key with an optional UTF-8 BOM and trailing whitespace. Unavailable, unreadable, empty or malformed files return false so the frontend can keep its manual key setting. Never package this file or its contents with source or releases.

The documented limits are 30 API requests per minute and 5 download requests per minute. The client shares a conservative process-wide limiter, reports cooldowns, honors HTTP 429 and Retry-After, and never retries automatically. Downloads accept only HTTPS URLs on romhackplaza.org, without redirects or credentials sent to other hosts. Other origins require an explicitly reviewed adapter.

Discovery uses the documented title search and then entry details. The API documents no hash search parameter or base/output role for entry hashes. Title matches and artwork do not prove patch compatibility. Frontends must verify the actual patch source checksum or another authoritative base-ROM requirement before allowing installation; IPS alone has no source checksum. Search returns at most 100 candidates because a page request parameter is not documented. Metadata images may be screenshots, not box art or banners; preserve that distinction.

Enable `ROMHACKPLAZA_BUILD_TESTS` to build offline tests. No API key or network access is required.

Official API documentation:
- https://community.romhackplaza.org/threads/api-usage.4782/
- https://community.romhackplaza.org/threads/getentriessearch-entries-search.4779/
- https://community.romhackplaza.org/threads/getentry-entries-entry.4777/
- https://community.romhackplaza.org/threads/getentryfiles-entries-entry-download.4778/
