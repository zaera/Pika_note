#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <Arduino.h>
#include <atomic>
#include "device_extras.h"
namespace {
constexpr int pin=2;
struct Pattern{uint16_t ms[5];uint8_t count;};
const Pattern patterns[]={{{175},1},{{400},1},{{130,100,130},3},{{250,150,250},3},{{130,100,130,100,130},5},{{200,150,200,150,200},5},{{120,80,200},3},{{200,80,120},3},{{120,80,120,80,300},5},{{300,100,120,80,120},5}};
QueueHandle_t queue=nullptr;std::atomic<int> outstanding{0};std::atomic<bool> wakePending{false};std::atomic<unsigned> queued{0},completed{0},dropped{0};
void worker(void*){int index;for(;;){if(xQueueReceive(queue,&index,portMAX_DELAY)==pdTRUE){gpio_hold_dis(static_cast<gpio_num_t>(pin));Serial.printf("PIKA_VIB_START pattern=%d at=%lu\n",index,(unsigned long)millis());const auto& p=patterns[index-1];for(int i=0;i<p.count;i++){digitalWrite(pin,i%2==0?HIGH:LOW);vTaskDelay(pdMS_TO_TICKS(p.ms[i]));}digitalWrite(pin,LOW);completed++;Serial.printf("PIKA_VIB_DONE pattern=%d at=%lu\n",index,(unsigned long)millis());outstanding--;}}}
}
void pikaVibrationBegin(){gpio_hold_dis(static_cast<gpio_num_t>(pin));rtc_gpio_hold_dis(static_cast<gpio_num_t>(pin));rtc_gpio_deinit(static_cast<gpio_num_t>(pin));digitalWrite(pin,LOW);pinMode(pin,OUTPUT);if(queue)return;queue=xQueueCreate(8,sizeof(int));if(queue&&xTaskCreatePinnedToCore(worker,"vibration",3072,nullptr,2,nullptr,1)!=pdPASS){vQueueDelete(queue);queue=nullptr;}}
void pikaVibrate(int kind){const int pattern=pikaVibrationPattern(kind);if(!queue||!pikaVibrationEnabled()||pattern<1||pattern>10)return;outstanding++;if(xQueueSend(queue,&pattern,0)!=pdTRUE){outstanding--;dropped++;}else{queued++;Serial.printf("PIKA_VIB_QUEUED kind=%d pattern=%d at=%lu\n",kind,pattern,(unsigned long)millis());}}
void pikaVibrationWait(){const auto start=millis();while(outstanding>0&&millis()-start<7000)delay(2);digitalWrite(pin,LOW);}

void pikaVibrationArmWake(bool enabled){wakePending=enabled;}
void pikaVibrationWakeReady(){if(wakePending.exchange(false))pikaVibrate(0);}
void pikaVibrationPrepareSleep(){pikaVibrationWait();digitalWrite(pin,LOW);gpio_hold_en(static_cast<gpio_num_t>(pin));}
void pikaVibrationPrintStatus(){Serial.printf("PIKA_VIB_STATUS enabled=%d wake_pattern=%d queued=%u completed=%u dropped=%u pending=%d GPIO2=%d\n",pikaVibrationEnabled(),pikaVibrationPattern(0),queued.load(),completed.load(),dropped.load(),outstanding.load(),digitalRead(pin));}

bool pikaVibrationTest(int pattern){if(!queue||outstanding.load()>0||pattern<1||pattern>10)return false;outstanding++;if(xQueueSend(queue,&pattern,0)!=pdTRUE){outstanding--;dropped++;return false;}queued++;return true;}
