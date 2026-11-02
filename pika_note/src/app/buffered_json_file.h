#pragma once
#include <ArduinoJson.h>
#include <cstring>
#include <algorithm>
// ArduinoJson otherwise invokes File.read/write for individual JSON bytes.
// Buffer only filesystem I/O; preserve streaming limits and atomic commit checks.
// File::read(buffer,size) returns the bytes actually remaining. Stream's
// readBytes can wait for its timeout when a JSON file ends before 1 KB.
// Keep a fallback for stream-only adapters and host fixtures.
template<class FileType> auto pikaJsonReadBlock(FileType& file,char* out,size_t n,int)->decltype(file.read(reinterpret_cast<uint8_t*>(out),n)){return file.read(reinterpret_cast<uint8_t*>(out),n);}
template<class FileType> size_t pikaJsonReadBlock(FileType& file,char* out,size_t n,long){return file.readBytes(out,n);}
template<class FileType> struct PikaJsonReader {
 FileType& file;char bytes[1024];size_t at=0,size=0;
 int read(){if(at==size){size=pikaJsonReadBlock(file,bytes,sizeof(bytes),0);at=0;if(!size)return -1;}return static_cast<unsigned char>(bytes[at++]);}
 size_t readBytes(char* out,size_t wanted){size_t done=0;while(done<wanted){int c=read();if(c<0)break;out[done++]=char(c);}return done;}
};
template<class FileType> struct PikaJsonWriter {
 FileType& file;bool (*cancel)()=nullptr;uint8_t bytes[1024];size_t size=0,committed=0;bool failed=false;
 bool flush(){if(cancel&&cancel()){failed=true;size=0;return false;}if(size){size_t n=file.write(bytes,size);committed+=n;if(n!=size)failed=true;size=0;}return !failed;}
 size_t write(uint8_t c){return write(&c,1);}
 size_t write(const uint8_t* data,size_t count){if(failed)return 0;size_t at=0;while(at<count){size_t n=std::min(count-at,sizeof(bytes)-size);memcpy(bytes+size,data+at,n);size+=n;at+=n;if(size==sizeof(bytes)&&!flush())return 0;}return count;}
};
template<class Doc,class FileType> DeserializationError pikaReadJsonBuffered(Doc& doc,FileType& file){PikaJsonReader<FileType> reader{file};return deserializeJson(doc,reader);}
template<class Doc,class FileType> size_t pikaWriteJsonBuffered(const Doc& doc,FileType& file,bool (*cancel)()=nullptr){PikaJsonWriter<FileType> writer{file,cancel};serializeJson(doc,writer);writer.flush();return writer.failed?0:writer.committed;}
