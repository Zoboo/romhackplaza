/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "romhackplaza/client.h"
#include <curl/curl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <ctime>
#include <cstdlib>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
namespace romhackplaza {
namespace {
struct Value {
 enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
 std::string text; std::vector<Value> items; std::map<std::string, Value> fields;
 const Value& get(const std::string& name) const { static const Value empty; auto i = fields.find(name); return i == fields.end() ? empty : i->second; }
 std::string str() const { return kind == String ? text : std::string(); }
 uint64_t integer() const { if (kind != Number || text.empty() || text.find_first_not_of("0123456789") != std::string::npos) throw std::runtime_error("Invalid API integer"); uint64_t n = 0; for (char c : text) { if (n > (UINT64_MAX - (c-'0')) / 10) throw std::runtime_error("API integer overflow"); n=n*10+c-'0'; } return n; }
};
class Parser {
 const std::string& s; size_t p = 0, nodes = 0;
 void ws() { while (p<s.size() && (s[p]==' '||s[p]=='\r'||s[p]=='\n'||s[p]=='\t')) ++p; }
 char take() { if (p==s.size()) fail(); return s[p++]; }
 [[noreturn]] void fail() const { throw std::runtime_error("Malformed Romhack Plaza response"); }
 unsigned hex() { unsigned n=0; for(int i=0;i<4;++i) { char c=take(); unsigned v=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:99; if(v>15) fail(); n=n*16+v; } return n; }
 void utf8(std::string& out,unsigned c) { if(c<128) out+=char(c); else if(c<2048) {out+=char(192|(c>>6));out+=char(128|(c&63));} else if(c<65536) {out+=char(224|(c>>12));out+=char(128|((c>>6)&63));out+=char(128|(c&63));} else {out+=char(240|(c>>18));out+=char(128|((c>>12)&63));out+=char(128|((c>>6)&63));out+=char(128|(c&63));} }
 std::string string() { if(take()!='"') fail(); std::string out; for(;;) { unsigned char c=take(); if(c=='"') return out; if(c<32) fail(); if(c!='\\') {out+=char(c);continue;} c=take(); switch(c) {case '"':case '\\':case '/':out+=char(c);break;case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;case 'u': {unsigned u=hex(); if(u>=0xd800&&u<=0xdbff) {if(take()!='\\'||take()!='u') fail(); unsigned lo=hex();if(lo<0xdc00||lo>0xdfff)fail();u=0x10000+((u-0xd800)<<10)+lo-0xdc00;} else if(u>=0xdc00&&u<=0xdfff)fail();utf8(out,u);break;} default:fail();} } }
 Value value(unsigned depth) { if(depth>32||++nodes>100000) fail(); ws(); Value v; char c=p<s.size()?s[p]:0; if(c=='"'){v.kind=Value::String;v.text=string();} else if(c=='{'||c=='[') {++p;v.kind=c=='{'?Value::Object:Value::Array;ws();char end=c=='{'?'}':']';if(p<s.size()&&s[p]==end){++p;return v;}for(;;){ws();if(c=='{'){std::string key=string();ws();if(take()!=':')fail();if(!v.fields.emplace(key,value(depth+1)).second)fail();}else v.items.push_back(value(depth+1));ws();char sep=take();if(sep==end)break;if(sep!=',')fail();}} else if(c=='t'||c=='f'||c=='n') {const char* word=c=='t'?"true":c=='f'?"false":"null";size_t n=std::char_traits<char>::length(word);if(s.compare(p,n,word))fail();p+=n;v.kind=c=='n'?Value::Null:Value::Bool;v.text=word;} else {size_t start=p;if(c=='-')++p;if(p==s.size())fail();if(s[p]=='0')++p;else {if(s[p]<'1'||s[p]>'9')fail();while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;}if(p<s.size()&&s[p]=='.'){++p;size_t a=p;while(p<s.size()&&isdigit(static_cast<unsigned char>(s[p])))++p;if(a==p)fail();}if(p<s.size()&&(s[p]=='e'||s[p]=='E')){++p;if(p<s.size()&&(s[p]=='+'||s[p]=='-'))++p;size_t a=p;while(p<s.size()&&isdigit(static_cast<unsigned char>(s[p])))++p;if(a==p)fail();}v.kind=Value::Number;v.text=s.substr(start,p-start);}return v; }
public: explicit Parser(const std::string& input):s(input){} Value parse(){if(s.size()>4*1024*1024)fail();Value v=value(0);ws();if(p!=s.size())fail();return v;}
};
Value data(const std::string& json) { Value root=Parser(json).parse(); if(root.kind!=Value::Object||root.get("ok").kind!=Value::Bool||root.get("ok").text!="true")throw std::runtime_error("Romhack Plaza rejected the request");return root.get("data"); }
Entry entry(const Value& v) { if(v.kind!=Value::Object)throw std::runtime_error("Invalid entry response");Entry e;e.id=v.get("id").integer();e.type=v.get("type").str();e.title=v.get("title").str();if(!e.id||e.title.empty())throw std::runtime_error("Incomplete entry response");e.description=v.get("description").str();e.mainImage=v.get("main_image").str();for(const Value& h:v.get("hashes").items){Hash x;x.filename=h.get("filename").str();x.crc32=h.get("hash_crc32").str();x.sha1=h.get("hash_sha1").str();x.verified=h.get("verified").str();e.hashes.push_back(x);}for(const Value& g:v.get("galleries").items){std::string u=g.get("image").str();if(!u.empty())e.images.push_back(u);}return e; }
using Clock=std::chrono::steady_clock;
std::mutex rateMutex;std::deque<Clock::time_point> requests,downloads,fileTransfers;Clock::time_point blocked;
void reserve(bool downloading) {std::lock_guard<std::mutex> lock(rateMutex);auto now=Clock::now();if(now<blocked)throw RateLimitError(unsigned(std::chrono::duration_cast<std::chrono::seconds>(blocked-now).count()+1));for(auto* q:{&requests,&downloads})while(!q->empty()&&now-q->front()>=std::chrono::seconds(60))q->pop_front();auto* q=downloading&&downloads.size()>=5?&downloads:requests.size()>=30?&requests:nullptr;if(q)throw RateLimitError(unsigned(60-std::chrono::duration_cast<std::chrono::seconds>(now-q->front()).count()));requests.push_back(now);if(downloading)downloads.push_back(now);}
struct Transfer {std::vector<uint8_t> bytes;size_t limit;Progress progress;unsigned retry=60;bool overflow=false;};
size_t write(void* p,size_t a,size_t b,void* ctx){Transfer& t=*static_cast<Transfer*>(ctx);if(b&&a>SIZE_MAX/b)return 0;size_t n=a*b;if(n>t.limit-t.bytes.size()){t.overflow=true;return 0;}try{t.bytes.insert(t.bytes.end(),static_cast<uint8_t*>(p),static_cast<uint8_t*>(p)+n);}catch(...){return 0;}return n;}
int progress(void* ctx,curl_off_t total,curl_off_t now,curl_off_t,curl_off_t){auto& t=*static_cast<Transfer*>(ctx);try{return t.progress&&!t.progress(now>0?uint64_t(now):0,total>0?uint64_t(total):0)?1:0;}catch(...){return 1;}}
size_t header(char* p,size_t a,size_t b,void* ctx){size_t n=a*b;std::string h(p,n);std::string lower=h;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(tolower(c));});if(lower.compare(0,12,"retry-after:")==0){std::string val=h.substr(12);char* end=nullptr;unsigned long sec=strtoul(val.c_str(),&end,10);if(end!=val.c_str()&&sec>0)static_cast<Transfer*>(ctx)->retry=unsigned(std::min<unsigned long>(sec,86400));else {time_t date=curl_getdate(val.c_str(),nullptr);time_t now=time(nullptr);if(date>now)static_cast<Transfer*>(ctx)->retry=unsigned(std::min<time_t>(date-now,86400));}}return n;}
std::vector<uint8_t> transfer(const std::string& url,const std::string& token,size_t limit,Progress cb,bool fileDownload=false){if(!trustedUrl(url))throw std::runtime_error("Unsupported download origin; only HTTPS romhackplaza.org is permitted");{std::lock_guard<std::mutex> lock(rateMutex);auto now=Clock::now();if(now<blocked)throw RateLimitError(unsigned(std::chrono::duration_cast<std::chrono::seconds>(blocked-now).count()+1));if(fileDownload){while(!fileTransfers.empty()&&now-fileTransfers.front()>=std::chrono::seconds(60))fileTransfers.pop_front();if(fileTransfers.size()>=5)throw RateLimitError(unsigned(60-std::chrono::duration_cast<std::chrono::seconds>(now-fileTransfers.front()).count()));fileTransfers.push_back(now);}}CURL* c=curl_easy_init();if(!c)throw std::runtime_error("Could not initialize HTTP client");Transfer t;t.limit=limit;t.progress=cb;curl_slist* headers=nullptr;if(!token.empty())headers=curl_slist_append(headers,("Authorization: Bearer "+token).c_str());curl_easy_setopt(c,CURLOPT_URL,url.c_str());curl_easy_setopt(c,CURLOPT_HTTPHEADER,headers);curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,0L);
#if LIBCURL_VERSION_NUM >= 0x075500
curl_easy_setopt(c,CURLOPT_PROTOCOLS_STR,"https");
#else
curl_easy_setopt(c,CURLOPT_PROTOCOLS,CURLPROTO_HTTPS);
#endif
curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,15L);curl_easy_setopt(c,CURLOPT_TIMEOUT,180L);curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);curl_easy_setopt(c,CURLOPT_USERAGENT,"romhackplaza-client/1.0");curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,write);curl_easy_setopt(c,CURLOPT_WRITEDATA,&t);curl_easy_setopt(c,CURLOPT_HEADERFUNCTION,header);curl_easy_setopt(c,CURLOPT_HEADERDATA,&t);curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);curl_easy_setopt(c,CURLOPT_XFERINFOFUNCTION,progress);curl_easy_setopt(c,CURLOPT_XFERINFODATA,&t);CURLcode result=curl_easy_perform(c);long status=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&status);curl_slist_free_all(headers);curl_easy_cleanup(c);if(status==429){std::lock_guard<std::mutex> lock(rateMutex);blocked=std::max(blocked,Clock::now()+std::chrono::seconds(t.retry));throw RateLimitError(t.retry);}if(t.overflow)throw std::runtime_error("Download exceeds size limit");if(result==CURLE_ABORTED_BY_CALLBACK)throw std::runtime_error("Cancelled");if(result!=CURLE_OK)throw std::runtime_error("Network transfer failed");if(status==401||status==403)throw std::runtime_error("API key missing, expired, or lacks required scope");if(status<200||status>=300)throw std::runtime_error("Romhack Plaza HTTP "+std::to_string(status));return t.bytes;}
}
RateLimitError::RateLimitError(unsigned s):std::runtime_error("Romhack Plaza limit reached (30 API requests and 5 downloads per minute). Retry in "+std::to_string(s)+" seconds."),retrySeconds(s){}
bool trustedUrl(const std::string& u){const std::string prefix="https://romhackplaza.org/";if(u.compare(0,prefix.size(),prefix))return false;for (unsigned char c : u) if (c <= 32 || c == 127 || c == '\\') return false; return true;}
Client::Client(const std::string& token):token_(token){for (unsigned char c : token) if(c <= 32 || c == 127) throw std::runtime_error("Invalid API key");}
std::string Client::request(const std::string& path,bool downloading,Progress cb)const{if(token_.empty())throw std::runtime_error("Configure a Romhack Plaza API key with entries:read and entries:download scopes");reserve(downloading);auto bytes=transfer("https://romhackplaza.org/api/v1/"+path,token_,4*1024*1024,cb);return std::string(bytes.begin(),bytes.end());}
std::vector<Entry> Client::search(const std::string& query,Progress cb)const{if(query.empty()||query.size()>256)throw std::runtime_error("Search title must contain 1 to 256 bytes");CURL* c=curl_easy_init();if(!c)throw std::runtime_error("Could not initialize HTTP client");char* escaped=curl_easy_escape(c,query.c_str(),int(query.size()));std::string q=escaped?escaped:"";curl_free(escaped);curl_easy_cleanup(c);if(q.empty())throw std::runtime_error("Could not encode search");return parseEntries(request("entries/search?types%5B%5D=romhacks&types%5B%5D=translations&per_page=100&search="+q,false,cb));}
Entry Client::details(uint64_t id,Progress cb)const{if(!id)throw std::runtime_error("Invalid entry ID");return parseEntry(request("entries/"+std::to_string(id),false,cb));}
std::vector<File> Client::files(uint64_t id,Progress cb)const{if(!id)throw std::runtime_error("Invalid entry ID");return parseFiles(request("entries/"+std::to_string(id)+"/download",true,cb));}
std::vector<uint8_t> Client::download(const std::string& url,size_t limit,Progress cb,bool auth)const{if(!limit||limit>128*1024*1024)throw std::runtime_error("Invalid download limit");if(auth&&token_.empty())throw std::runtime_error("Configure a Romhack Plaza API key");return transfer(url,auth?token_:"",limit,cb,auth);}
std::vector<Entry> parseEntries(const std::string& json){Value v=data(json);if(v.kind!=Value::Array||v.items.size()>100)throw std::runtime_error("Invalid search response");std::vector<Entry> out;for(const Value& x:v.items)out.push_back(entry(x));return out;}
Entry parseEntry(const std::string& json){return entry(data(json));}
std::vector<File> parseFiles(const std::string& json){Value v=data(json);if(v.kind!=Value::Array||v.items.size()>1024)throw std::runtime_error("Invalid files response");std::vector<File> out;for(const Value& x:v.items){File f;f.filename=x.get("filename").str();f.uuid=x.get("file_uuid").str();f.url=x.get("download").str();f.expires=x.get("download_expires_at").str();f.size=x.get("filesize").integer();if(f.filename.empty()||f.url.empty())throw std::runtime_error("Incomplete download response");out.push_back(f);}return out;}
}
