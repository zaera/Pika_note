#pragma once
#include <stdint.h>
// Emit once at five seconds of continuous overlap, without waiting for release.
struct PikaRecoveryHold {
 uint32_t heldAt=0;bool timing=false,requested=false;
 bool update(uint32_t now,bool upper,bool lower){
  if(requested)return false;
  if(!(upper&&lower)){timing=false;return false;}
  if(!timing){timing=true;heldAt=now;return false;}
  if(uint32_t(now-heldAt)<5000u)return false;
  requested=true;return true;
 }
};
