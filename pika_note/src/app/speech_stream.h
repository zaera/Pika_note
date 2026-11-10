#pragma once
#ifdef PIKA_SPEECH_STREAM_HOST_TEST
#include "../../../scripts/speech_stream_mocks.h"
#else
#include <Arduino.h>
#endif
#include <atomic>
#include "pcm_stream.h"

// Only the audio task touches I2S. Only the calling task touches TLS/UI.
class PikaSpeechStream {
 public:
  static bool reserveWorker();
  bool begin(bool cancelOnLower = false);
  bool put(char byte);
  bool poll();
  bool ready() const;
  void start();
  bool finish(bool complete);
  bool wasStarted() const { return started.load(); }
  bool wasCancelled() const { return cancelled.load(); }
 private:
  static void worker(void* context);
  static void dispatcher(void* context);
  uint8_t* buffer = nullptr;
  uint32_t capacity = kPcmStreamCapacity;
  std::atomic<uint32_t> written{0}, read{0};
  std::atomic<bool> started{false}, ended{false}, aborting{false}, cancelled{false}, failed{false}, done{false};
  uint32_t lowSince = 0;
  bool cancelArmed = false;
  bool cancelLower = false;
};
