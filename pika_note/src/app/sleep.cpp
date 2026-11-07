#include "error_log.h"
#include "home_buttons.h"
#include "phone_sync.h"
#include "phone_link.h"
#include "phone_lan.h"
#include "phone_hash.h"
#include "activity_led.h"
#include "Arduino.h"
#include "../../config.h"
#include "../../globals.h"
#include "../../types.h"
#include "sleep.h"
#include "ui.h"
#include "device_extras.h"
#include "network.h"
#include "brain.h"
#include "battery.h"
#include "../../sounds.h"
#include "WiFi.h"
#include "SD_MMC.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

extern "C" {
#include "../../src/audio/audio_bsp.h"
}

namespace { volatile bool sleepLowerEdge=false; }
void IRAM_ATTR pikaSleepLowerEdge(){sleepLowerEdge=true;}

void resetActivity() {
  lastActivityMs = millis();
}

void releasePeripheralPowerHolds() {
  // GPIO holds survive deep sleep. Release them before the normal startup
  // sequence powers the display and codec again.
  // Preload the live output latch BEFORE releasing the retained sleep value.
  // Releasing a held HIGH while the reset output latch is LOW drops VBAT.
  gpio_set_level(VBAT_PWR_PIN,1);
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(VBAT_PWR_PIN);
  gpio_set_level(VBAT_PWR_PIN,1);
  // EXT1 uses the RTC mux: restore the physical wake key before pinMode/ISRs.
  rtc_gpio_hold_dis(static_cast<gpio_num_t>(BTN_PWR));
  rtc_gpio_deinit(static_cast<gpio_num_t>(BTN_PWR));
  gpio_hold_dis(EPD_PWR_PIN);
  gpio_hold_dis(Audio_PWR_PIN);
}

void enterUltraSleep() {
  if(pikaPhoneSyncActive())return;
  // Never shut peripherals down while the level-triggered wake key is held.
  // Defer to the main loop instead of sleeping and immediately waking again.
  if (digitalRead(BTN_PWR) == LOW) { resetActivity(); return; }
  sleepLowerEdge=false;
  delay(50);
  if (digitalRead(BTN_PWR) == LOW || sleepLowerEdge) { resetActivity(); return; }
  const esp_err_t wakeResult = esp_sleep_enable_ext1_wakeup(
      (1ULL << BTN_PWR), ESP_EXT1_WAKEUP_ANY_LOW);
  if (wakeResult != ESP_OK) {
    Serial.printf("[Sleep] Wake configuration failed: %s\n", esp_err_to_name(wakeResult));
    resetActivity();
    return;
  }
  // Join the LAN worker before any final display/SD/power teardown. It owns
  // socket and crypto buffers and must not race WiFi shutdown or a sleep frame.
  if(!pikaLanStopAndWait(1200)){Serial.println("PIKA_SLEEP_DEFER lan_shutdown");resetActivity();return;}
  pikaPhoneForeground(true);
  // Preserve Today as white text on black while asleep. The ordinary frame
  // is retained in RTC memory for early restoration on the next key wake.
  if(!pikaClockMode()&&!showSleepLocked()){pikaPhoneForeground(false);resetActivity();showIdle(pendingBrainMemoryCount(),readBattery().percent);return;}
  pikaPlayNotify(6);
  pikaVibrationWait();
  // A press during the panel refresh must still cancel sleep safely.
  if (digitalRead(BTN_PWR) == LOW || sleepLowerEdge) {
    Serial.printf("PIKA_SLEEP_CANCEL edge=%d lower=%d\n",sleepLowerEdge,digitalRead(BTN_PWR));pikaPhoneForeground(false);pikaHomeButtonsReset();resetActivity();
    showIdle(pendingBrainMemoryCount(), readBattery().percent);
    return;
  }
  uint64_t timer=pikaNextAlertWakeUs();
  if(pikaClockMode()){
    const time_t now=time(nullptr);const uint64_t minute=(60-(now%60))*1000000ULL;
    if(!timer||minute<timer)timer=minute;
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  if(timer)esp_sleep_enable_timer_wakeup(timer);
  stopTransferMode();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  audio_playback_set_vol(0);
  pikaSoundSetEnabled(false);
  audio_prepare_sleep();

  // Flush/unmount storage before power domains are reduced. A wake is a full
  // boot, so the card and audio codec will be initialized again in setup().
  pikaPhoneHashCancel();for(unsigned i=0;i<200;i++)pikaErrorService();SD_MMC.end();

  board.POWEER_Audio_OFF();
  board.POWEER_EPD_OFF();

  // Keep the battery latch on for ESP32 EXT1 wake. Hardware power-off is a
  // different mode and requires the board's power-on gesture instead.
  board.VBAT_POWER_ON();

  pikaVibrationPrepareSleep();
  pikaActivityLedSleep();
  gpio_hold_en(EPD_PWR_PIN);
  gpio_hold_en(Audio_PWR_PIN);
  gpio_hold_en(VBAT_PWR_PIN);
  gpio_deep_sleep_hold_en();

  // PWR is the board's dedicated hardware wake button. Keeping BOOT out of
  // the wake mask gives the device one unambiguous wake gesture and avoids
  // involving the ESP32-S3 strapping/download pin while the device sleeps.
  Serial.printf("[Sleep] Entering deep sleep; wake=PWR GPIO%d; idle=%lu ms\n",
                BTN_PWR, static_cast<unsigned long>(millis() - lastActivityMs));
  Serial.flush();
  pikaPhoneSleep();
  // Keep the digital key ISR alive through BLE shutdown. A short wake tap
  // during teardown must not disappear before EXT1 is armed by deep sleep.
  if(sleepLowerEdge||digitalRead(BTN_PWR)==LOW){Serial.println("[Sleep] Lower press during shutdown; returning to Home");Serial.flush();ESP.restart();return;}
  // Configure the RTC pull-up explicitly; digital INPUT_PULLUP alone does
  // not describe the pin's sleep-domain bias. Keep RTC peripherals powered
  // so the active-low key has a stable idle level throughout deep sleep.
  const gpio_num_t wakePin=static_cast<gpio_num_t>(BTN_PWR);
  const esp_err_t rtcInit=rtc_gpio_init(wakePin);
  const esp_err_t rtcDirection=rtc_gpio_set_direction(wakePin,RTC_GPIO_MODE_INPUT_ONLY);
  const esp_err_t rtcPullup=rtc_gpio_pullup_en(wakePin);
  const esp_err_t rtcPulldown=rtc_gpio_pulldown_dis(wakePin);
  const esp_err_t rtcPower=esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,ESP_PD_OPTION_ON);
  if(rtcInit!=ESP_OK||rtcDirection!=ESP_OK||rtcPullup!=ESP_OK||rtcPulldown!=ESP_OK||rtcPower!=ESP_OK){
    // Peripherals are already shut down. Restart cleanly instead of leaving
    // an apparently sleeping screen with an unconfigured wake button.
    Serial.println("[Sleep] RTC wake setup failed; restarting safely");
    ESP.restart();return;
  }
  esp_deep_sleep_start();
}
