/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "../src/client.cpp"
#include <cassert>
using namespace romhackplaza;
namespace {
void reset() { requests.clear(); downloads.clear(); fileTransfers.clear(); blocked = Clock::time_point(); }
template<class F> unsigned throttled(F f) { try { f(); } catch (const RateLimitError& e) { return e.retrySeconds; } assert(false); return 0; }
}
int main() {
 reset();
 for (int i=0;i<30;++i) reserve(false);
 assert(throttled([] { reserve(false); }) >= 59);
 assert(requests.size()==30);
 reset();
 for (int i=0;i<5;++i) reserve(true);
 assert(throttled([] { reserve(true); }) >= 59);
 assert(downloads.size()==5 && requests.size()==5);
 for(auto& stamp:downloads) stamp=Clock::now()-std::chrono::seconds(61);
 for(auto& stamp:requests) stamp=Clock::now()-std::chrono::seconds(61);
 reserve(true);assert(downloads.size()==1 && requests.size()==1);
 reset();blocked=Clock::now()+std::chrono::seconds(120);
 assert(throttled([] { reserve(false); }) >= 119);
 assert(requests.empty());
 Transfer t;t.limit=1024;
 std::string numeric="Retry-After: 42\r\n";header(&numeric[0],1,numeric.size(),&t);assert(t.retry==42);
 std::string cap="retry-after: 99999999\r\n";header(&cap[0],1,cap.size(),&t);assert(t.retry==86400);
 std::string invalid="Retry-After: nonsense\r\n";t.retry=60;header(&invalid[0],1,invalid.size(),&t);assert(t.retry==60);
 time_t future=time(nullptr)+120;tm* utc=gmtime(&future);char date[96];strftime(date,sizeof(date),"Retry-After: %a, %d %b %Y %H:%M:%S GMT\r\n",utc);std::string dated=date;header(&dated[0],1,dated.size(),&t);assert(t.retry>=118 && t.retry<=120);
 t.progress=[](uint64_t n,uint64_t total) { assert(n==8 && total==16);return false;};assert(progress(&t,16,8,0,0)==1);
 t.progress=[](uint64_t,uint64_t) -> bool { throw std::runtime_error("cancel"); };assert(progress(&t,16,8,0,0)==1);
 t.progress=[](uint64_t,uint64_t){return true;};assert(progress(&t,16,8,0,0)==0);
 reset();
}
