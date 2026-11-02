#include "error_log.h"
#include "ask_stage.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
namespace {
struct Job {void (*callback)(void*);void* context;SemaphoreHandle_t done;};
QueueHandle_t jobs=nullptr;
void worker(void*) {
 for(;;){Job job;if(xQueueReceive(jobs,&job,portMAX_DELAY)!=pdTRUE)continue;
  job.callback(job.context);
  Serial.printf("[AskStage] complete stackFree=%u\n",uxTaskGetStackHighWaterMark(nullptr));
  xSemaphoreGive(job.done);
 }
}
}
bool pikaAskStagesBegin(){
 if(jobs)return true;
 jobs=xQueueCreate(1,sizeof(Job));if(!jobs){pikaError("Ask","worker","Worker resource unavailable");Serial.printf("[AskStage] unavailable: queue allocation\n");return false;}
 if(xTaskCreate(worker,"ask-stages",32*1024,nullptr,1,nullptr)!=pdPASS){vQueueDelete(jobs);jobs=nullptr;pikaError("Ask","worker","Worker resource unavailable");Serial.printf("[AskStage] unavailable: 32KB worker stack allocation\n");return false;}
 Serial.printf("[AskStage] worker reserved before first request\n");return true;
}
bool pikaRunAskStage(void (*callback)(void*),void* context){
 if(!pikaAskStagesBegin())return false;
 SemaphoreHandle_t done=xSemaphoreCreateBinary();if(!done){pikaError("Ask","worker","Worker resource unavailable");Serial.printf("[AskStage] unavailable: completion semaphore\n");return false;}
 Job job{callback,context,done};
 if(xQueueSend(jobs,&job,0)!=pdTRUE){vSemaphoreDelete(done);pikaError("Ask","worker","Worker resource unavailable");Serial.printf("[AskStage] unavailable: queue busy\n");return false;}
 // The UI/SD/network belong exclusively to this stage until completion.
 while(xSemaphoreTake(done,pdMS_TO_TICKS(20))!=pdTRUE)delay(1);
 vSemaphoreDelete(done);return true;
}
