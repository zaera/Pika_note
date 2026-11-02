#pragma once
#include <cstdint>
#include <cstring>
#include <algorithm>

// USB requests can span sectors or start at an unaligned byte offset.
template<class ReadSector>
int32_t pikaBackupRead(uint32_t lba,uint32_t offset,void* output,uint32_t size,
                      uint32_t sectors,uint16_t sectorSize,uint8_t* scratch,ReadSector read){
  const uint64_t start=uint64_t(lba)*sectorSize+offset;
  if(!sectorSize||!output||start+size>uint64_t(sectors)*sectorSize)return -1;
  auto* destination=static_cast<uint8_t*>(output);uint32_t done=0;
  while(done<size){
    const uint64_t position=start+done;const uint32_t skip=position%sectorSize;
    const uint32_t count=std::min<uint32_t>(size-done,sectorSize-skip);
    if(!read(scratch,position/sectorSize))return -1;
    memcpy(destination+done,scratch+skip,count);done+=count;
  }
  return static_cast<int32_t>(done);
}
