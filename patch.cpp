/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "patch.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <limits>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

namespace romhackplaza {
namespace {
bool Fail(std::string& e, const char* message) { e = message; return false; }
uint32_t LE(const Bytes& b, size_t p) { return uint32_t(b[p]) | uint32_t(b[p+1])<<8 | uint32_t(b[p+2])<<16 | uint32_t(b[p+3])<<24; }
uint16_t LE16(const Bytes& b, size_t p) { return uint16_t(b[p]) | uint16_t(b[p+1])<<8; }
uint32_t CRC(const Bytes& b, size_t size) { return crc32(0, b.data(), static_cast<uInt>(size)); }
bool Tick(const PatchProgress& p, size_t n, size_t total, std::string& e) { return !p || p(n,total) || Fail(e,"Cancelled."); }
struct Reader {
 const Bytes& b; size_t p, end;
 bool Number(uint64_t& value) {
  value=0; uint64_t shift=1;
  for (unsigned i=0;i<10 && p<end;++i) {
   uint8_t x=b[p++]; uint64_t bits=x&127;
   if (bits && shift>(UINT64_MAX-value)/bits) return false;
   value+=bits*shift;
   if (x&128) return true;
   if (shift>UINT64_MAX/128) return false;
   shift*=128;
   if (value>UINT64_MAX-shift) return false;
   value+=shift;
  }
  return false;
 }
};
bool Relative(Reader& r, size_t& cursor, size_t limit) {
 uint64_t n;
 if (!r.Number(n)) return false;
 uint64_t delta=n>>1;
 if (n&1) { if (delta>cursor) return false; cursor-=size_t(delta); }
 else { if (delta>limit || cursor>limit-size_t(delta)) return false; cursor+=size_t(delta); }
 return cursor<=limit;
}
bool Delta(const Bytes& base,const Bytes& patch,Bytes& out,std::string& error,bool bps,const PatchProgress& progress) {
 if (patch.size()<18) return Fail(error,"Truncated patch.");
 size_t footer=patch.size()-12;
 if (CRC(patch,patch.size()-4)!=LE(patch,patch.size()-4)) return Fail(error,"Patch checksum mismatch.");
 if (CRC(base,base.size())!=LE(patch,footer)) return Fail(error,"The original ROM does not match this patch.");
 Reader r{patch,4,footer}; uint64_t source,target,metadata;
 if (!r.Number(source)||!r.Number(target)||source!=base.size()||target>MaxRomSize||target==0) return Fail(error,"Invalid patch ROM size.");
 out.assign(size_t(target),0);
 if (bps) {
  if (!r.Number(metadata)||metadata>r.end-r.p) return Fail(error,"Invalid patch metadata.");
  r.p+=size_t(metadata); size_t pos=0,src=0,dst=0;
  while (r.p<r.end) {
   uint64_t command;
   if (!r.Number(command)) return Fail(error,"Invalid patch instruction.");
   uint64_t length=(command>>2)+1;
   if (length>out.size()-pos) return Fail(error,"Patch writes beyond the output ROM.");
   size_t n=size_t(length);
   switch (command&3) {
   case 0:
    if (pos>base.size()||n>base.size()-pos) return Fail(error,"Patch reads beyond the original ROM.");
    std::copy(base.begin()+pos,base.begin()+pos+n,out.begin()+pos); pos+=n; break;
   case 1:
    if (n>r.end-r.p) return Fail(error,"Truncated patch data.");
    std::copy(patch.begin()+r.p,patch.begin()+r.p+n,out.begin()+pos); r.p+=n; pos+=n; break;
   case 2:
    if (!Relative(r,src,base.size())||n>base.size()-src) return Fail(error,"Invalid source copy.");
    std::copy(base.begin()+src,base.begin()+src+n,out.begin()+pos); src+=n; pos+=n; break;
   case 3:
    if (!Relative(r,dst,out.size())||dst>=pos) return Fail(error,"Invalid target copy.");
    for (size_t i=0;i<n;++i) { out[pos++]=out[dst++]; if (!(i&65535)&&!Tick(progress,pos,out.size(),error)) return false; }
    break;
   }
   if (!Tick(progress,pos,out.size(),error)) return false;
  }
  if (pos!=out.size()) return Fail(error,"Patch output is incomplete.");
 } else {
  std::copy(base.begin(),base.begin()+std::min(base.size(),out.size()),out.begin());
  size_t pos=0,limit=std::max(base.size(),out.size());
  while (r.p<r.end) {
   uint64_t delta;
   if (!r.Number(delta)||delta>limit||pos>limit-size_t(delta)) return Fail(error,"Invalid UPS offset.");
   pos+=size_t(delta); bool terminated=false;
   while (r.p<r.end) {
    uint8_t x=patch[r.p++];
    if (!x) { if (pos==SIZE_MAX) return Fail(error,"Invalid UPS offset."); ++pos; terminated=true; break; }
    if (pos>=limit) return Fail(error,"UPS writes beyond the ROM.");
    uint8_t value=(pos<base.size()?base[pos]:0)^x;
    if (pos<out.size()) out[pos]=value;
    else if (value) return Fail(error,"Invalid UPS truncated output.");
    ++pos;
    if (!(pos&65535)&&!Tick(progress,std::min(pos,out.size()),out.size(),error)) return false;
   }
   if (!terminated) return Fail(error,"Truncated UPS record.");
   if (!Tick(progress,std::min(pos,out.size()),out.size(),error)) return false;
  }
 }
 if (CRC(out,out.size())!=LE(patch,footer+4)) return Fail(error,"Patched ROM checksum mismatch.");
 return Tick(progress,out.size(),out.size(),error);
}
bool IPS(const Bytes& base,const Bytes& patch,Bytes& out,std::string& error,const PatchProgress& progress) {
 out=base; size_t pos=5;
 while (pos+3<=patch.size()) {
  if (!std::memcmp(patch.data()+pos,"EOF",3)) {
   pos+=3;
   if (patch.size()-pos==3) {
    size_t size=(size_t(patch[pos])<<16)|(size_t(patch[pos+1])<<8)|patch[pos+2];
    if (!size||size>MaxRomSize) return Fail(error,"Invalid IPS output size.");
    out.resize(size); pos+=3;
   }
   return pos==patch.size() ? Tick(progress,patch.size(),patch.size(),error) : Fail(error,"Unexpected IPS trailing data.");
  }
  size_t offset=(size_t(patch[pos])<<16)|(size_t(patch[pos+1])<<8)|patch[pos+2]; pos+=3;
  if (patch.size()-pos<2) return Fail(error,"Truncated IPS record.");
  size_t count=(size_t(patch[pos])<<8)|patch[pos+1]; pos+=2;
  bool rle=!count;
  if (rle) {
   if (patch.size()-pos<3) return Fail(error,"Truncated IPS repeat.");
   count=(size_t(patch[pos])<<8)|patch[pos+1]; pos+=2;
   if (!count) return Fail(error,"Empty IPS repeat.");
  }
  if (offset>MaxRomSize||count>MaxRomSize-offset||(!rle&&count>patch.size()-pos)) return Fail(error,"Invalid IPS range.");
  if (out.size()<offset+count) out.resize(offset+count,0);
  if (rle) std::fill(out.begin()+offset,out.begin()+offset+count,patch[pos++]);
  else { std::copy(patch.begin()+pos,patch.begin()+pos+count,out.begin()+offset); pos+=count; }
  if (!Tick(progress,pos,patch.size(),error)) return false;
 }
 return Fail(error,"IPS end marker is missing.");
}
bool IsPatch(const Bytes& b) { return b.size()>=5&&(!std::memcmp(b.data(),"PATCH",5)||!std::memcmp(b.data(),"BPS1",4)||!std::memcmp(b.data(),"UPS1",4)); }
bool PatchName(std::string n) {
 std::transform(n.begin(),n.end(),n.begin(),[](unsigned char c){return char(std::tolower(c));});
 size_t p=n.rfind('.'); return p!=std::string::npos&&(n.substr(p)==".ips"||n.substr(p)==".bps"||n.substr(p)==".ups");
}
}

bool ApplyPatch(const Bytes& base,const Bytes& patch,Bytes& output,std::string& error,bool verified,PatchProgress progress) {
 error.clear(); output.clear();
 if (base.empty()||base.size()>MaxRomSize||patch.size()>MaxRomSize||!IsPatch(patch)) return Fail(error,"Unsupported or oversized patch/ROM.");
 Bytes result; bool success;
 if (!std::memcmp(patch.data(),"PATCH",5)) success=verified ? IPS(base,patch,result,error,progress) : Fail(error,"IPS requires a verified original ROM hash.");
 else success=Delta(base,patch,result,error,!std::memcmp(patch.data(),"BPS1",4),progress);
 if (success) output.swap(result);
 return success;
}

bool ExtractPatch(const Bytes& bytes,Bytes& patch,std::string& error,PatchProgress progress) {
 patch.clear(); error.clear();
 if (bytes.size()>MaxRomSize) return Fail(error,"Patch download is too large.");
 if (IsPatch(bytes)) { patch=bytes; return true; }
 if (bytes.size()<22) return Fail(error,"Unsupported patch archive. Use IPS, BPS, UPS or ZIP.");
 size_t end=bytes.size()-22,lower=bytes.size()>65557?bytes.size()-65557:0;
 while (true) {
  if (LE(bytes,end)==0x06054b50 && end+22+LE16(bytes,end+20)==bytes.size()) break;
  if (end==lower) return Fail(error,"Unsupported or malformed ZIP archive.");
  --end;
 }
 if (LE16(bytes,end+4)||LE16(bytes,end+6)||LE16(bytes,end+8)!=LE16(bytes,end+10)) return Fail(error,"Multipart ZIP archives are unsupported.");
 size_t count=LE16(bytes,end+10),central=LE(bytes,end+16),centralSize=LE(bytes,end+12);
 if (count>4096||central>end||centralSize>end-central||central+centralSize!=end) return Fail(error,"Invalid ZIP directory.");
 size_t pos=central,chosen=0; unsigned matches=0;
 for (size_t i=0;i<count;++i) {
  if (pos>end||end-pos<46||LE(bytes,pos)!=0x02014b50) return Fail(error,"Invalid ZIP entry.");
  size_t n=LE16(bytes,pos+28),extra=LE16(bytes,pos+30),comment=LE16(bytes,pos+32);
  if (n+extra+comment>end-pos-46) return Fail(error,"Truncated ZIP directory.");
  std::string name(reinterpret_cast<const char*>(bytes.data()+pos+46),n);
  if (PatchName(name)) { ++matches; chosen=pos; }
  pos+=46+n+extra+comment;
 }
 if (pos!=end) return Fail(error,"Invalid ZIP directory size.");
 if (!matches) return Fail(error,"No IPS, BPS or UPS patch was found in the archive.");
 if (matches>1) return Fail(error,"Archive contains multiple patches; automatic installation cannot safely choose a variant.");
 unsigned flags=LE16(bytes,chosen+8),method=LE16(bytes,chosen+10);
 size_t packed=LE(bytes,chosen+20),size=LE(bytes,chosen+24),local=LE(bytes,chosen+42);
 if ((flags&1)||(flags&0x40)||(method!=0&&method!=8)||size>MaxRomSize||!size||local>central||central-local<30||LE(bytes,local)!=0x04034b50) return Fail(error,"Unsupported encrypted or oversized ZIP patch.");
 if (LE16(bytes,local+6)!=flags||LE16(bytes,local+8)!=method) return Fail(error,"Inconsistent ZIP header.");
 size_t name=LE16(bytes,local+26),extra=LE16(bytes,local+28);
 if (name+extra>central-local-30) return Fail(error,"Invalid ZIP data offset.");
 size_t start=local+30+name+extra;
 if (packed>central-start) return Fail(error,"Truncated ZIP patch.");
 Bytes result(size);
 if (method==0) {
  if (packed!=size) return Fail(error,"Invalid stored ZIP size.");
  std::copy(bytes.begin()+start,bytes.begin()+start+size,result.begin());
 } else {
  z_stream stream{}; stream.next_in=const_cast<Bytef*>(bytes.data()+start); stream.avail_in=static_cast<uInt>(packed);
  if (inflateInit2(&stream,-MAX_WBITS)!=Z_OK) return Fail(error,"Cannot open ZIP decoder.");
  int status=Z_OK;
  while (status==Z_OK && stream.total_out<size) {
   stream.next_out=result.data()+stream.total_out; stream.avail_out=static_cast<uInt>(std::min<size_t>(65536,size-stream.total_out));
   status=inflate(&stream,Z_NO_FLUSH);
   if (!Tick(progress,stream.total_out,size,error)) { inflateEnd(&stream); return false; }
  }
  bool valid=status==Z_STREAM_END&&stream.total_out==size&&stream.total_in==packed;
  inflateEnd(&stream);
  if (!valid) return Fail(error,"Corrupted ZIP patch data.");
 }
 if (CRC(result,result.size())!=LE(bytes,chosen+16)||!IsPatch(result)) return Fail(error,"ZIP patch checksum or format is invalid.");
 patch.swap(result); return true;
}

bool InstallPatch(const std::string& original,const Bytes& download,const std::string& directory,const std::string& title,uint64_t entryId,uint32_t expectedCRC32,std::string& installed,std::string& error,PatchProgress progress) {
 installed.clear(); error.clear();
 int source=open(original.c_str(),O_RDONLY);
 if (source<0) return Fail(error,"Cannot open the original ROM.");
 struct stat info;
 if (fstat(source,&info)||!S_ISREG(info.st_mode)||info.st_size<=0||uint64_t(info.st_size)>MaxRomSize) { close(source); return Fail(error,"Unsupported original ROM file."); }
 Bytes base(size_t(info.st_size)); size_t readBytes=0;
 while (readBytes<base.size()) {
  ssize_t n=read(source,base.data()+readBytes,std::min<size_t>(65536,base.size()-readBytes));
  if (n<=0) { close(source); return Fail(error,"Cannot read the original ROM."); }
  readBytes+=size_t(n);
  if (!Tick(progress,readBytes,base.size(),error)) { close(source); return false; }
 }
 close(source);
 if (CRC(base,base.size())!=expectedCRC32) return Fail(error,"The original ROM hash changed or does not match.");
 Bytes patch,output;
 if (!ExtractPatch(download,patch,error,progress)||!ApplyPatch(base,patch,output,error,true,progress)) return false;
 base.clear(); base.shrink_to_fit(); patch.clear(); patch.shrink_to_fit();
 std::string name;
 for (unsigned char c:title) { if (name.size()>=120) break; name+=c>=32&&c<127&&std::strchr("<>:\"/\\|?*",c)==nullptr?char(c):'_'; }
 while (!name.empty()&&(name.back()=='.'||name.back()==' ')) name.pop_back();
 if (name.empty()||name=="."||name=="..") name="ROM hack";
 size_t dot=original.rfind('.'); std::string ext=dot==std::string::npos?"":original.substr(dot);
 std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
 if (ext!=".gba"&&ext!=".gb"&&ext!=".gbc"&&ext!=".agb"&&ext!=".sgb"&&ext!=".dmg") return Fail(error,"Select an uncompressed Game Boy or Game Boy Advance ROM.");
 if (directory.empty()) return Fail(error,"ROM directory is not configured.");
 std::string destination=directory+(directory.back()=='/'?"":"/")+name+" [RHP "+std::to_string(entryId)+"]"+ext;
 // The reservation prevents replacing a user's existing ROM, even if its name matches.
 int reserved=open(destination.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
 if (reserved<0) return Fail(error,errno==EEXIST?"This ROM hack is already installed or the filename is in use.":"Cannot create the patched ROM in this directory.");
 close(reserved);
 std::string temporary; int file=-1;
 for (unsigned i=0;i<100;++i) {
  temporary=destination+".part-"+std::to_string(i);
  file=open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
  if (file>=0||errno!=EEXIST) break;
 }
 if (file<0) { unlink(destination.c_str()); return Fail(error,"Cannot create a temporary patched ROM."); }
 bool ok=true; size_t written=0;
 while (written<output.size()) {
  if (!Tick(progress,written,output.size(),error)) { ok=false; break; }
  ssize_t n=write(file,output.data()+written,std::min<size_t>(65536,output.size()-written));
  if (n<=0) { Fail(error,"Cannot write patched ROM. Check free SD card space."); ok=false; break; }
  written+=size_t(n);
 }
 if (ok&&fsync(file)) { Fail(error,"Cannot flush the patched ROM to storage."); ok=false; }
 if (close(file)&&ok) { Fail(error,"Cannot close patched ROM."); ok=false; }
 if (ok&&!Tick(progress,output.size(),output.size(),error)) ok=false;
 if (ok&&rename(temporary.c_str(),destination.c_str())) { Fail(error,"Cannot finalize the patched ROM."); ok=false; }
 if (!ok) { unlink(temporary.c_str()); unlink(destination.c_str()); return false; }
 installed=destination; return true;
}
}
