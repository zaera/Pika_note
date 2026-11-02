#include "phone_link.h"
#include "Arduino.h"
#include "../../globals.h"
#include "../../types.h"
#include "rtc.h"
#include "wake_policy.h"
#include "date_validation.h"
#include "WiFi.h"
#include <time.h>
#include <Preferences.h>
#include <esp_sntp.h>
#include <atomic>
#include <sys/time.h>

extern "C" {
#include "../../src/i2c_bsp/i2c_bsp.h"
}

namespace {std::atomic<int64_t> phoneEpoch{0};std::atomic<uint32_t> phoneAt{0};std::atomic<bool> phoneClock{false};bool phoneChipPending=false;uint32_t phoneChipAttempt=0;}
void pikaRtcQueuePhoneTime(int64_t epoch){if(epoch<1700000000||epoch>=4102444800LL)return;phoneAt=millis();timeval tv{time_t(epoch),0};settimeofday(&tv,nullptr);timeReady=true;phoneClock=true;phoneEpoch=epoch;}
bool pikaRtcHasPhoneTime(){return phoneClock.load()||phoneEpoch.load()>=1700000000;}
void pikaRtcServicePhoneTime(){const int64_t epoch=phoneEpoch.exchange(0);if(epoch){timeval tv{time_t(epoch+(millis()-phoneAt.load())/1000),0};settimeofday(&tv,nullptr);timeReady=true;phoneClock=true;phoneChipPending=true;phoneChipAttempt=0;Serial.println("PIKA_CLOCK phone time applied");}if(phoneChipPending&&rtc_dev_handle&&(!phoneChipAttempt||millis()-phoneChipAttempt>=1000)){phoneChipAttempt=millis();if(rtcSyncChipFromSystem()){phoneChipPending=false;Serial.println("PIKA_CLOCK RTC saved");}}}

uint8_t bcdToDec(uint8_t v) { return ((v >> 4) * 10) + (v & 0x0F); }
uint8_t decToBcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

bool rtcReadRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
  if (!rtc_dev_handle) return false;
  return i2c_read_buff(rtc_dev_handle, reg, buf, len) == 0;
}

bool rtcWriteRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
  if (!rtc_dev_handle) return false;
  return i2c_write_buff(rtc_dev_handle, reg, buf, len) == 0;
}

bool rtcReadUtcTm(struct tm* out) {
  if (!out) return false;
  uint8_t data[7] = {0};
  if (!rtcReadRegs(PCF85063_REG_SECONDS, data, 7)) return false;

  // OS flag: oscillator stopped
  if (data[0] & 0x80) return false;
  for (int i : {0, 1, 2, 3, 5, 6}) {
    const uint8_t masks[] = {0x7f, 0x7f, 0x3f, 0x3f, 0x07, 0x1f, 0xff};
    if (!validBcdByte(data[i] & masks[i])) return false;
  }

  int sec   = bcdToDec(data[0] & 0x7F);
  int min   = bcdToDec(data[1] & 0x7F);
  int hour  = bcdToDec(data[2] & 0x3F);
  int day   = bcdToDec(data[3] & 0x3F);
  int wday  = bcdToDec(data[4] & 0x07);
  int month = bcdToDec(data[5] & 0x1F);
  int year  = bcdToDec(data[6]) + 2000;

  if (year < 2024 || year > 2099) return false;
  if (!validCalendarDate(year, month, day) || wday > 6) return false;
  if (hour > 23 || min > 59 || sec > 59) return false;

  memset(out, 0, sizeof(struct tm));
  out->tm_year = year - 1900;
  out->tm_mon  = month - 1;
  out->tm_mday = day;
  out->tm_hour = hour;
  out->tm_min  = min;
  out->tm_sec  = sec;
  out->tm_wday = wday;
  return true;
}

bool rtcWriteUtcTm(const struct tm& utc) {
  uint8_t data[7];
  int year = utc.tm_year + 1900;
  if (year < 2000 || year > 2099) return false;
  if (!validCalendarDate(year, utc.tm_mon + 1, utc.tm_mday) || utc.tm_hour < 0 || utc.tm_hour > 23 ||
      utc.tm_min < 0 || utc.tm_min > 59 || utc.tm_sec < 0 || utc.tm_sec > 59 ||
      utc.tm_wday < 0 || utc.tm_wday > 6) return false;
  data[0] = decToBcd(utc.tm_sec);
  data[1] = decToBcd(utc.tm_min);
  data[2] = decToBcd(utc.tm_hour);
  data[3] = decToBcd(utc.tm_mday);
  data[4] = decToBcd(utc.tm_wday);
  data[5] = decToBcd(utc.tm_mon + 1);
  data[6] = decToBcd(year - 2000);
  return rtcWriteRegs(PCF85063_REG_SECONDS, data, 7);
}

time_t utcTmToEpoch(struct tm utc) {
  // UTC conversion without changing the process timezone. ESP newlib does not
  // expose timegm(), so use the Gregorian civil-date conversion directly.
  int year = utc.tm_year + 1900;
  unsigned month = static_cast<unsigned>(utc.tm_mon + 1);
  const unsigned day = static_cast<unsigned>(utc.tm_mday);
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
  return static_cast<time_t>(days * 86400 + utc.tm_hour * 3600 + utc.tm_min * 60 + utc.tm_sec);
}

bool rtcSyncSystemFromChip() {
  if(pikaRtcHasPhoneTime()){pikaRtcServicePhoneTime();return true;}
  struct tm utc;
  if (!rtcReadUtcTm(&utc)) { timeReady = false; return false; }
  time_t epoch = utcTmToEpoch(utc);
  if (epoch < 1700000000) { timeReady = false; return false; }
  if(pikaRtcHasPhoneTime()){pikaRtcServicePhoneTime();return true;}
  timeval tv; tv.tv_sec = epoch; tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  timeReady = true;
  return true;
}

bool rtcSyncChipFromSystem() {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;
  struct tm utc;
  gmtime_r(&now, &utc);
  bool ok = rtcWriteUtcTm(utc);
  if (ok) timeReady = true;
  return ok;
}

String rtcUtcIso() {
  struct tm utc;
  if (!rtcReadUtcTm(&utc)) return "";
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return String(buf);
}

namespace {
std::atomic<bool> ntpConfirmed{false};
void ntpNotification(struct timeval*){ntpConfirmed=true;}
}
bool rtcNtpSyncDue(){Preferences prefs;prefs.begin("pikaClock",true);const auto last=prefs.getULong64("lastNtp",0),attempt=prefs.getULong64("ntpAttempt",0);prefs.end();return pikaDailyClockDue(time(nullptr),last,attempt);}
void rtcMarkNtpAttempt(){const auto now=time(nullptr);if(now<1700000000)return;Preferences prefs;prefs.begin("pikaClock",false);prefs.putULong64("ntpAttempt",static_cast<uint64_t>(now));prefs.end();}
bool syncTimeFromNTP(uint32_t timeoutMs) {
  return pikaPhoneOnline() && time(nullptr)>=1700000000;
  if (WiFi.status() != WL_CONNECTED) return false;
  rtcMarkNtpAttempt();
  ntpConfirmed=false;
  sntp_set_time_sync_notification_cb(ntpNotification);
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  const uint32_t start=millis();
  while(millis()-start<timeoutMs){
    if(ntpConfirmed.load()&&time(nullptr)>1700000000){
      timeReady=true;rtcSyncChipFromSystem();
      Preferences prefs;prefs.begin("pikaClock",false);prefs.putULong64("lastNtp",static_cast<uint64_t>(time(nullptr)));prefs.end();
      return true;
    }
    delay(25);
  }
  return false;
}
