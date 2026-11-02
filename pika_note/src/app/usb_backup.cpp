#include "usb_backup.h"
#include "usb_backup_read.h"
#include "draw.h"
#include "../../config.h"
#include <Arduino.h>
#include <esp_system.h>
#include <USBMSC.h>
#include <SD_MMC.h>
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>
#include <atomic>

namespace {
// Register MSC before Arduino starts TinyUSB. The media is absent until Mount SD.
USBMSC disk;
sdmmc_card_t card{};
SemaphoreHandle_t cardMutex=nullptr;
alignas(32) uint8_t sector[512];
std::atomic<bool> accepting{false},exitRequested{false};
bool requested=false;uint32_t requestedAt=0;
RTC_NOINIT_ATTR uint32_t resumeMagic,resumeCheck;
constexpr uint32_t kResumeMagic=0x55534237;
int32_t readDisk(uint32_t lba,uint32_t offset,void* output,uint32_t size){
  if(!accepting||!cardMutex||xSemaphoreTake(cardMutex,pdMS_TO_TICKS(5000))!=pdTRUE)return -1;
  int32_t result=-1;
  if(accepting)result=pikaBackupRead(lba,offset,output,size,card.csd.capacity,card.csd.sector_size,sector,
    [](uint8_t* buffer,uint32_t block){return sdmmc_read_sectors(&card,buffer,block,1)==ESP_OK;});
  xSemaphoreGive(cardMutex);return result;
}
int32_t rejectWrite(uint32_t,uint32_t,uint8_t*,uint32_t){return -1;}
bool startStop(uint8_t,bool start,bool eject){if(!start&&eject)exitRequested=true;return true;}
void backupScreen(bool ready){
  clearWhite();drawStr(14,12,"USB BACKUP",2,BLACK);hline(14,43,172,BLACK);
  drawStr(14,59,ready?"SD mounted":"SD unavailable",1,BLACK);
  drawStr(14,83,"Read-only",1,BLACK);
  drawStr(14,107,"Copy files on PC",1,BLACK);
  drawStr(14,136,"Eject to return",1,BLACK);
  hline(14,174,172,BLACK);drawStr(14,181,"LOWER: EXIT",1,BLACK);refresh();
}
}
bool pikaUsbBackupResume(){bool resume=resumeMagic==kResumeMagic&&resumeCheck==~kResumeMagic&&esp_reset_reason()!=ESP_RST_DEEPSLEEP;resumeMagic=resumeCheck=0;return resume;}
bool pikaUsbBackupRequest(){if(requested)return false;requested=true;requestedAt=millis();return true;}
bool pikaUsbBackupPending(){return requested;}
bool pikaUsbBackupDue(){return requested&&millis()-requestedAt>=700;}
void pikaUsbBackupRun(){
  requested=false;exitRequested=false;
  // Release FatFs and the Arduino SD host before initializing exclusive raw I/O.
  SD_MMC.end();
  sdmmc_host_t host=SDMMC_HOST_DEFAULT();host.flags=SDMMC_HOST_FLAG_1BIT;host.max_freq_khz=SDMMC_FREQ_DEFAULT;
  sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();slot.width=1;
  slot.clk=static_cast<gpio_num_t>(SD_CLK);slot.cmd=static_cast<gpio_num_t>(SD_CMD);slot.d0=static_cast<gpio_num_t>(SD_D0);
  slot.flags|=SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
  bool initialized=sdmmc_host_init()==ESP_OK;
  bool ready=initialized&&sdmmc_host_init_slot(host.slot,&slot)==ESP_OK&&sdmmc_card_init(&host,&card)==ESP_OK&&card.csd.sector_size==sizeof(sector);
  cardMutex=xSemaphoreCreateMutex();ready=ready&&cardMutex;
  if(ready){
    disk.vendorID("PIKA");disk.productID("SD Backup");disk.productRevision("1.0");
    disk.onRead(readDisk);disk.onWrite(rejectWrite);disk.onStartStop(startStop);disk.isWritable(false);
    ready=disk.begin(card.csd.capacity,card.csd.sector_size);
    accepting=ready;disk.mediaPresent(ready);
  }
  backupScreen(ready);Serial.println(ready?"PIKA_USB_BACKUP_READY":"PIKA_USB_BACKUP_FAILED");
  bool armed=digitalRead(BTN_PWR)==HIGH;bool buttonExit=false;
  while(!exitRequested){
    if(digitalRead(BTN_PWR)==HIGH)armed=true;
    else if(armed){delay(25);if(digitalRead(BTN_PWR)==LOW){buttonExit=true;break;}}
    delay(10);
  }
  accepting=false;disk.mediaPresent(false);
  Serial.printf("PIKA_USB_BACKUP_EXIT source=%s pwr=%d\n",buttonExit?"lower":"eject",digitalRead(BTN_PWR));
  resumeCheck=~kResumeMagic;resumeMagic=kResumeMagic;
  // Release the raw host only if its current read finished within the budget.
  // Leaving its IRQ live through CPU restart can otherwise trip the watchdog.
  bool drained=!cardMutex||xSemaphoreTake(cardMutex,pdMS_TO_TICKS(250))==pdTRUE;
  if(drained&&initialized)sdmmc_host_deinit();
  Serial.printf("PIKA_USB_BACKUP_RESTART drained=%d\n",drained);
  esp_restart();
}
