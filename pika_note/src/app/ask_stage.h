#pragma once
// Synchronous hand-off to a bounded worker. The caller must keep context alive.
// Each callback returns to the queue before the next stage starts.
// Reserve the large worker stack before BLE/audio can fragment internal RAM.
bool pikaAskStagesBegin();
bool pikaRunAskStage(void (*callback)(void*), void* context);
