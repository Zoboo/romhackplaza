/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "romhackplaza/client.h"
#include <cassert>
#include <stdexcept>
using namespace romhackplaza;
template<class F> void rejects(F f) { bool failed=false; try {f();} catch(const std::exception&) {failed=true;} assert(failed); }
int main() {
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
