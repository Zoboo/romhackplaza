/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef ROMHACKPLAZA_CLIENT_H
#define ROMHACKPLAZA_CLIENT_H
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
namespace romhackplaza {
using Progress = std::function<bool(uint64_t, uint64_t)>;
struct Hash { std::string filename, crc32, sha1, verified; };
struct Entry { uint64_t id = 0; std::string type, title, description, mainImage; std::vector<Hash> hashes; std::vector<std::string> images; };
struct File { std::string filename, uuid, url, expires; uint64_t size = 0; };
struct RateLimitError : std::runtime_error { unsigned retrySeconds; explicit RateLimitError(unsigned seconds); };
class Client {
public:
 explicit Client(const std::string& token);
 std::vector<Entry> search(const std::string& query, Progress progress = Progress()) const;
 Entry details(uint64_t id, Progress progress = Progress()) const;
 std::vector<File> files(uint64_t id, Progress progress = Progress()) const;
 std::vector<uint8_t> download(const std::string& url, size_t maxBytes, Progress progress = Progress(), bool authenticated = true) const;
private:
 std::string token_;
 std::string request(const std::string& path, bool downloadRequest, Progress progress) const;
};
// Strict parsers are exposed for saved-response adapters and offline validation.
std::vector<Entry> parseEntries(const std::string& json);
Entry parseEntry(const std::string& json);
std::vector<File> parseFiles(const std::string& json);
bool trustedUrl(const std::string& url);
}
#endif
