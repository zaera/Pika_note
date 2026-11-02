#pragma once
#include <cstdint>
inline bool pikaDailyClockDue(int64_t now,int64_t lastSuccess,int64_t lastAttempt){
 if(now<1700000000)return true;
 const int64_t last=lastSuccess>lastAttempt?lastSuccess:lastAttempt;
 return last<1700000000||now<last||now-last>=86400;
}
