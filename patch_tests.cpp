/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "patch.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
using namespace romhackplaza;
static uint32_t CRC(const Bytes& b) { return crc32(0,b.data(),static_cast<uInt>(b.size())); }
static void LE(Bytes& b,uint32_t n) { for (unsigned i=0;i<4;++i) b.push_back(uint8_t(n>>(8*i))); }
static void Num(Bytes& b,uint64_t n) { while (true) { uint8_t x=n&127; n>>=7; if (!n) { b.push_back(x|128); return; } b.push_back(x); --n; } }
static void Seal(Bytes& p,const Bytes& base,const Bytes& output) { LE(p,CRC(base)); LE(p,CRC(output)); LE(p,CRC(p)); }
static Bytes Read(const std::string& p) { std::ifstream f(p,std::ios::binary); return Bytes(std::istreambuf_iterator<char>(f),{}); }
static void Write(const std::string& p,const Bytes& b) { std::ofstream f(p,std::ios::binary); f.write(reinterpret_cast<const char*>(b.data()),b.size()); }
int main() {
 Bytes base{'a','b','c'},wanted{'a','x','c'},out;
 Bytes ips{'P','A','T','C','H',0,0,1,0,1,'x','E','O','F'};
 std::string error;
 assert(!ApplyPatch(base,ips,out,error));
 assert(ApplyPatch(base,ips,out,error,true)&&out==wanted);
 Bytes bad=ips; bad.pop_back(); assert(!ApplyPatch(base,bad,out,error,true)&&out.empty());
 Bytes bps{'B','P','S','1'}; Num(bps,3); Num(bps,3); Num(bps,0); Num(bps,0); Num(bps,1); bps.push_back('x'); Num(bps,0); Seal(bps,base,wanted);
 assert(ApplyPatch(base,bps,out,error)&&out==wanted);
 bad=bps; bad.back()^=1; assert(!ApplyPatch(base,bad,out,error)&&out.empty());
 Bytes wrong{'z','b','c'}; assert(!ApplyPatch(wrong,bps,out,error));
 Bytes ups{'U','P','S','1'}; Num(ups,3); Num(ups,3); Num(ups,1); ups.push_back('b'^'x'); ups.push_back(0); Seal(ups,base,wanted);
 assert(ApplyPatch(base,ups,out,error)&&out==wanted);
 bad=ups; bad[bad.size()-8]^=1; bad.resize(bad.size()-4); LE(bad,CRC(bad)); assert(!ApplyPatch(base,bad,out,error));
 assert(!ApplyPatch(base,bps,out,error,false,[](size_t,size_t){return false;}));
 Bytes rle{'P','A','T','C','H',0,0,0,0,0,0,3,'z','E','O','F'};
 assert(ApplyPatch(base,rle,out,error,true)&&out==Bytes({'z','z','z'}));
 Bytes overlap{'B','P','S','1'}; Num(overlap,3); Num(overlap,8); Num(overlap,0); Num(overlap,1); overlap.push_back('a'); Num(overlap,((7-1)<<2)|3); Num(overlap,0); Bytes repeated(8,'a'); Seal(overlap,base,repeated);
 assert(ApplyPatch(base,overlap,out,error)&&out==repeated);
 Bytes invalid{'B','P','S','1'}; Num(invalid,3); Num(invalid,3); Num(invalid,0); Num(invalid,3); Num(invalid,0); Seal(invalid,base,base);
 assert(!ApplyPatch(base,invalid,out,error));
 Bytes grow{'U','P','S','1'}; Num(grow,3); Num(grow,5); Num(grow,3); grow.push_back('x'); grow.push_back('y'); grow.push_back(0); Bytes grown{'a','b','c','x','y'}; Seal(grow,base,grown);
 assert(ApplyPatch(base,grow,out,error)&&out==grown);
 for (size_t i=0;i<bps.size();++i) { bad.assign(bps.begin(),bps.begin()+i); assert(!ApplyPatch(base,bad,out,error)); }
 for (size_t i=0;i<ups.size();++i) { bad.assign(ups.begin(),ups.begin()+i); assert(!ApplyPatch(base,bad,out,error)); }
 Bytes zip{'P','K',3,4}; zip.resize(30,0); zip[8]=0; uint32_t crc=CRC(ips);
 for (unsigned i=0;i<4;++i) { zip[14+i]=uint8_t(crc>>(8*i)); zip[18+i]=uint8_t(ips.size()>>(8*i)); zip[22+i]=uint8_t(ips.size()>>(8*i)); }
 std::string name="../../hack.ips"; zip[26]=uint8_t(name.size()); zip.insert(zip.end(),name.begin(),name.end()); zip.insert(zip.end(),ips.begin(),ips.end());
 size_t central=zip.size(); Bytes directory{'P','K',1,2}; directory.resize(46,0); for (unsigned i=0;i<4;++i) { directory[16+i]=uint8_t(crc>>(8*i)); directory[20+i]=uint8_t(ips.size()>>(8*i)); directory[24+i]=uint8_t(ips.size()>>(8*i)); } directory[28]=uint8_t(name.size()); directory.insert(directory.end(),name.begin(),name.end()); zip.insert(zip.end(),directory.begin(),directory.end());
 Bytes end{'P','K',5,6}; end.resize(22,0); end[8]=end[10]=1; for (unsigned i=0;i<4;++i) { end[12+i]=uint8_t(directory.size()>>(8*i)); end[16+i]=uint8_t(central>>(8*i)); } zip.insert(zip.end(),end.begin(),end.end());
 assert(ExtractPatch(zip,out,error)&&out==ips); bad=zip; bad[30+name.size()]^=1; assert(!ExtractPatch(bad,out,error));
 Bytes compressed(compressBound(ips.size())); z_stream zs{};
 assert(deflateInit2(&zs,Z_DEFAULT_COMPRESSION,Z_DEFLATED,-MAX_WBITS,8,Z_DEFAULT_STRATEGY)==Z_OK);
 zs.next_in=ips.data(); zs.avail_in=static_cast<uInt>(ips.size()); zs.next_out=compressed.data(); zs.avail_out=static_cast<uInt>(compressed.size());
 assert(deflate(&zs,Z_FINISH)==Z_STREAM_END); compressed.resize(zs.total_out); deflateEnd(&zs);
 Bytes zipped(zip.begin(),zip.begin()+30+name.size()); zipped[8]=8;
 for (unsigned i=0;i<4;++i) zipped[18+i]=uint8_t(compressed.size()>>(8*i));
 zipped.insert(zipped.end(),compressed.begin(),compressed.end()); size_t compressedCentral=zipped.size();
 Bytes deflatedDirectory=directory; deflatedDirectory[10]=8;
 for (unsigned i=0;i<4;++i) deflatedDirectory[20+i]=uint8_t(compressed.size()>>(8*i));
 zipped.insert(zipped.end(),deflatedDirectory.begin(),deflatedDirectory.end()); Bytes deflatedEnd=end;
 for (unsigned i=0;i<4;++i) deflatedEnd[16+i]=uint8_t(compressedCentral>>(8*i));
 zipped.insert(zipped.end(),deflatedEnd.begin(),deflatedEnd.end()); assert(ExtractPatch(zipped,out,error)&&out==ips);
 Bytes multiple(zip.begin(),zip.begin()+central); multiple.insert(multiple.end(),directory.begin(),directory.end()); multiple.insert(multiple.end(),directory.begin(),directory.end()); Bytes multipleEnd=end; multipleEnd[8]=multipleEnd[10]=2;
 for (unsigned i=0;i<4;++i) multipleEnd[12+i]=uint8_t((directory.size()*2)>>(8*i));
 multiple.insert(multiple.end(),multipleEnd.begin(),multipleEnd.end()); assert(!ExtractPatch(multiple,out,error)&&error.find("multiple patches")!=std::string::npos);
 for (size_t i=0;i<zip.size();++i) { bad.assign(zip.begin(),zip.begin()+i); assert(!ExtractPatch(bad,out,error)); }
 std::string folder="patch-test-"+std::to_string(static_cast<unsigned long>(getpid())); assert(!mkdir(folder.c_str(),0700));
 std::string original=folder+"/original.gba",installed; Write(original,base);
 assert(InstallPatch(original,zip,folder,"../../Example",42,CRC(base),installed,error));
 assert(installed.find(folder+"/")==0&&installed.find("../",folder.size())==std::string::npos);
 assert(Read(installed)==wanted&&Read(original)==base);
 std::string duplicate; assert(!InstallPatch(original,zip,folder,"../../Example",42,CRC(base),duplicate,error));
 assert(Read(installed)==wanted&&Read(original)==base);
 assert(!InstallPatch(original,ips,folder,"Cancelled",43,CRC(base),duplicate,error,[](size_t,size_t){return false;}));
 assert(Read(original)==base);
 size_t calls=0;
 assert(!InstallPatch(original,ips,folder,"Cancel output",44,CRC(base),duplicate,error,[&calls](size_t,size_t){return ++calls<4;}));
 assert(Read(original)==base);
 std::string cancelled=folder+"/Cancel output [RHP 44].gba";
 assert(access(cancelled.c_str(),F_OK)!=0&&access((cancelled+".part-0").c_str(),F_OK)!=0);
 unlink(installed.c_str()); unlink(original.c_str()); rmdir(folder.c_str());
 std::cout<<"Patch module checks passed.\n";
}
