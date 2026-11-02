#pragma once
#include <stdint.h>
// A reset is latched after a continuous two-button hold, then emitted once
// both buttons have been released and remained up through debounce.
struct PikaRecoveryHold {
 uint32_t heldAt=0,releasedAt=0;bool timing=false,requested=false,releasing=false;
 bool update(uint32_t now,bool upper,bool lower){
  if(!requested){if(!(upper&&lower))timing=false;else if(!timing){timing=true;heldAt=now;}else if(uint32_t(now-heldAt)>=5000u)requested=true;}
  else if(!upper&&!lower){if(!releasing){releasing=true;releasedAt=now;}else if(uint32_t(now-releasedAt)>=50u)return true;}
  else releasing=false;
  return false;
 }
};
