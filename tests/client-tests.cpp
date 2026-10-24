/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "romhackplaza/client.h"
#include <cassert>
#include <stdexcept>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
using namespace romhackplaza;
template<class F> void rejects(F f) { bool failed=false; try {f();} catch(const std::exception&) {failed=true;} assert(failed); }
void keyFileTests() {
 std::string directory = "client-key-test-" + std::to_string(static_cast<unsigned long>(getpid()));
 assert(!mkdir(directory.c_str(), 0700));
 std::string keyDirectory = directory + "/romhackingplaza";
 std::string path = keyDirectory + "/api-key.txt", token = "old";
 assert(!ReadApiKeyFile(directory, token) && token.empty());
 struct stat info;
 assert(!stat(keyDirectory.c_str(), &info) && S_ISDIR(info.st_mode));
 assert(!stat(path.c_str(), &info) && info.st_size == 0);
 assert(!std::remove(path.c_str()));
 assert(!ReadApiKeyFile(directory, token) && token.empty());
 assert(!stat(path.c_str(), &info) && info.st_size == 0);
 auto check = [&](const std::string& bytes, bool valid, const std::string& expected = std::string()) {
  std::FILE* file = std::fopen(path.c_str(), "wb"); assert(file);
  assert(std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size()); assert(!std::fclose(file));
  token = "old";
  assert(ReadApiKeyFile(directory, token) == valid);
  assert(token == expected);
  file = std::fopen(path.c_str(), "rb"); assert(file);
  std::string unchanged;
  char buffer[256]; size_t n;
  while ((n = std::fread(buffer, 1, sizeof(buffer), file))) unchanged.append(buffer, n);
  assert(!std::ferror(file)); assert(!std::fclose(file)); assert(unchanged == bytes);
 };
 check("", false);
 check("synthetic-token", true, "synthetic-token");
 assert(!chmod(path.c_str(), 0400));
 assert(ReadApiKeyFile(directory, token) && token == "synthetic-token");
 assert(!chmod(path.c_str(), 0600));
 check("synthetic-token\r\n", true, "synthetic-token");
 check("synthetic-token \t\r\n", true, "synthetic-token");
 check("\xef\xbb\xbfsynthetic-token\n", true, "synthetic-token");
 check("\xef\xbb\xbf", false);
 check(" leading-space", false);
 check("embedded space", false);
 check("first\nsecond", false);
 check(std::string("token\0suffix", 12), false);
 check("non-ascii-\xc3\xa9", false);
 check(std::string(1023, 'x'), true, std::string(1023, 'x'));
 check(std::string(1024, 'x'), false);
 check(std::string(5000, 'x'), false);
 assert(!ReadApiKeyFile("", token) && token.empty());
 assert(!ReadApiKeyFile(std::string("bad\0path", 8), token) && token.empty());
 assert(!std::remove(path.c_str())); assert(!mkdir(path.c_str(), 0700));
 assert(!ReadApiKeyFile(directory, token) && token.empty());
 assert(!rmdir(path.c_str())); assert(!rmdir(keyDirectory.c_str()));
 std::FILE* obstruction = std::fopen(keyDirectory.c_str(), "wb"); assert(obstruction); assert(!std::fclose(obstruction));
 assert(!ReadApiKeyFile(directory, token) && token.empty());
 assert(!std::remove(keyDirectory.c_str()));
 assert(!ReadApiKeyFile(directory + "/missing-parent", token) && token.empty());
 assert(!rmdir(directory.c_str()));
}
int main() {
 keyFileTests();
 auto e=parseEntries("{\"ok\":true,\"data\":[{\"id\":1,\"type\":\"romhacks\",\"title\":\"Pok\\u00e9mon \\ud83c\\udfae\"}]}");
 assert(e.size()==1 && e[0].id==1 && e[0].title=="Pok\xc3\xa9mon \xf0\x9f\x8e\xae");
 auto d=parseEntry("{\"ok\":true,\"data\":{\"id\":2,\"title\":\"Hack\",\"hashes\":[{\"filename\":\"base.gba\",\"hash_crc32\":\"12345678\",\"hash_sha1\":\"abcdef\",\"verified\":\"yes\"}],\"galleries\":[{\"image\":\"https://romhackplaza.org/a.png\",\"order\":1}]}}");
 assert(d.hashes.size()==1 && d.hashes[0].crc32=="12345678" && d.images.size()==1);
 rejects([]{parseEntries("{\"ok\":false,\"data\":[]}");});
 rejects([]{parseEntries("{\"ok\":true,\"data\":[]}junk");});
 rejects([]{parseEntries("{\"ok\":true,\"ok\":true,\"data\":[]}");});
 rejects([]{parseEntries("{\"ok\":true,\"data\":[{\"id\":18446744073709551616,\"title\":\"bad\"}]}");});
 rejects([]{parseEntries("{\"ok\":true,\"data\":[{\"id\":1,\"title\":\"\\ud800\"}]}");});
 assert(trustedUrl("https://romhackplaza.org/patch?signature=abc"));
 for(const char* u:{"http://romhackplaza.org/a","https://romhackplaza.org.evil/a","https://romhackplaza.org@evil/a","https://romhackplaza.org:444/a","https://romhackplaza.org/\\evil","https://romhackplaza.org/a\r\nX: bad"}) assert(!trustedUrl(u));
 rejects([]{Client c("bad\r\nheader");});
 rejects([]{Client c("");c.files(1);});
 RateLimitError r(42);assert(r.retrySeconds==42);
}
