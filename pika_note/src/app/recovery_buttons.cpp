#include <Arduino.h>
#include <esp_system.h>
#include <atomic>
#include <esp_attr.h>
#include "../../config.h"
#include "recovery_buttons.h"
#include "recovery_hold.h"
namespace {
std::atomic<uint32_t> restartAt{0};
RTC_NOINIT_ATTR uint32_t resumeMagic,resumeCheck;
// Independent recovery input: no display, BLE, SD, or application-loop locks.
// Software restart is requested at the deadline even while both keys stay down.
void recoveryButtonsTask(void*) {
  PikaRecoveryHold hold;
  for (;;) {
    if(!restartAt&&hold.update(millis(),digitalRead(BTN_REC)==LOW,digitalRead(BTN_PWR)==LOW))restartAt=millis();
    // Recovery still works if the UI/main loop is stuck and cannot draw.
    if(restartAt&&uint32_t(millis()-restartAt.load())>2500){pikaRecoveryMarkResume();esp_restart();}
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

}
bool pikaRecoveryButtonsBegin(){return xTaskCreate(recoveryButtonsTask,"recovery-buttons",2048,nullptr,4,nullptr)==pdPASS;}

bool pikaRecoveryRestartRequested(){return restartAt.load()!=0;}
void pikaRecoveryMarkResume(){resumeMagic=0x52535438;resumeCheck=~resumeMagic;}
bool pikaRecoveryResume(){bool ready=resumeMagic==0x52535438&&resumeCheck==~resumeMagic&&esp_reset_reason()==ESP_RST_SW;resumeMagic=resumeCheck=0;return ready;}

void pikaRecoveryRequestRestart(){if(!restartAt)restartAt=millis();}
