#pragma once
#include <cstddef>
#include <cstdint>
// SPSC ring: caller publishes the counters using acquire/release atomics.
// Absolute unsigned counters make wraparound safe; capacity must divide 2^32.
inline uint32_t pcmStreamAvailable(uint32_t written, uint32_t read) { return written - read; }
inline bool pcmStreamValid(size_t bytes) { return bytes > 0 && !(bytes & 1U); }
constexpr uint32_t kPcmStreamCapacity = 1024 * 1024;
constexpr uint32_t kPcmStreamSmallCapacity = 65536;
constexpr uint32_t kPcmStreamPrebuffer = 98304; // 2.048 s of 24 kHz mono PCM

// Hysteresis prevents one tiny packet restarting playback after starvation.
inline bool pcmStreamWaitForBuffer(uint32_t available, bool ended, uint32_t capacity, bool& buffering) {
  if (ended) { buffering = false; return false; }
  if (available < 1024) buffering = true;
  const uint32_t threshold = capacity / 2 < 24000 ? capacity / 2 : 24000;
  if (buffering && available >= threshold) buffering = false;
  return buffering;
}
