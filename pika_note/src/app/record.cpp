#include "device_extras.h"
#include "phone_sync.h"
#include "activity_led.h"
#include "Arduino.h"
#include "../../config.h"
#include "../../globals.h"
#include "../../types.h"
#include "record.h"
#include "SD_MMC.h"
#include "esp_heap_caps.h"
#include "notes.h"
#include "settings.h"
#include "ui.h"
#include "pcm_resampler.h"
#include "wav_payload.h"
#include "../../sounds.h"
#include <atomic>

extern "C" {
#include "../../src/audio/audio_bsp.h"
}

namespace {
std::atomic<bool> recordingUiBusy{false},recordingEnded{false},recordingEndedDrawn{false};
bool shortCapture=false;
bool recordingUiProgress = false;
bool recordingUiQuestion = false;
uint32_t recordingUiMarkerMs = 0;
int recordingUiMarkerCount = 0;

void recordingUiTask(void*) {
  // E-paper is intentionally refreshed on the other ESP32 core. The recording
  // loop must keep draining I2S while the comparatively slow panel updates.
  if (recordingEnded) { showRecordingStopped(recordingUiQuestion); recordingEndedDrawn=true; }
  else if (recordingUiProgress) showRecordingProgress(recordingUiMarkerMs, recordingUiMarkerCount);
  else if (recordingUiQuestion) showAskListening();
  else showRecording();
  // Release may arrive while the panel is still painting Listening.
  if(recordingEnded&&!recordingEndedDrawn){showRecordingStopped(recordingUiQuestion);recordingEndedDrawn=true;}
  recordingUiBusy = false;
  vTaskDelete(nullptr);
}

bool startRecordingUi(bool progress = false, uint32_t markerMs = 0, int markerCount = 0, bool question = false) {
  // Never wait for e-paper while draining the microphone. A marker remains
  // saved even when its visual update is skipped during an existing refresh.
  if (recordingUiBusy.load()) return true;
  recordingUiProgress = progress;
  recordingUiQuestion = question;
  recordingUiMarkerMs = markerMs;
  recordingUiMarkerCount = markerCount;
  recordingUiBusy = true;
  const BaseType_t started = xTaskCreatePinnedToCore(
      recordingUiTask, "recording-ui", 4096, nullptr, 1, nullptr, 0);
  if (started != pdPASS) {
    recordingUiBusy = false;
    Serial.println("[Rec] warning: recording UI task could not be started");
  }
  return started == pdPASS;
}

void waitForRecordingUi() {
  while (recordingUiBusy) delay(1);
}
}  // namespace

static bool writeWavHeader(File& file, uint32_t dataBytes) {
  const uint32_t resumeAt = file.position();
  if (!file.seek(0)) return false;
  const uint32_t fileSize = dataBytes + 36;
  const uint32_t byteRate = SAMPLE_RATE * 2;
  const uint32_t fmtLength = 16;
  const uint32_t sampleRate = SAMPLE_RATE;
  const uint16_t blockAlign = 2, audioFormat = 1, channels = 1, bits = 16;
  bool ok = file.write((uint8_t*)"RIFF", 4) == 4;
  ok &= file.write((uint8_t*)&fileSize, 4) == 4;
  ok &= file.write((uint8_t*)"WAVE", 4) == 4;
  ok &= file.write((uint8_t*)"fmt ", 4) == 4;
  ok &= file.write((uint8_t*)&fmtLength, 4) == 4;
  ok &= file.write((uint8_t*)&audioFormat, 2) == 2;
  ok &= file.write((uint8_t*)&channels, 2) == 2;
  ok &= file.write((uint8_t*)&sampleRate, 4) == 4;
  ok &= file.write((uint8_t*)&byteRate, 4) == 4;
  ok &= file.write((uint8_t*)&blockAlign, 2) == 2;
  ok &= file.write((uint8_t*)&bits, 2) == 2;
  ok &= file.write((uint8_t*)"data", 4) == 4;
  ok &= file.write((uint8_t*)&dataBytes, 4) == 4;
  file.flush();
  const bool restored = file.seek(max<uint32_t>(resumeAt, 44));
  return ok && restored;
}

static bool recordHeldToPath(const char* path, int holdPin, bool captureMoments, int noteNum) {
  shortCapture=false;
  Serial.printf("[Rec] %s\n", path);

  // A recording absent from the catalog must never be silently overwritten.
  // Questions remove their disposable file explicitly before reaching here.
  if (SD_MMC.exists(path)) return false;

  if(!pikaPhoneFileWillChange(path))return false;
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) return false;

  uint8_t header[44]={};
  if (f.write(header, 44) != 44 || !writeWavHeader(f, 0)) {
    f.close();
    return false;
  }

  int16_t* sbuf = (int16_t*)heap_caps_malloc(REC_BUF,   MALLOC_CAP_8BIT);
  int16_t* mbuf = (int16_t*)heap_caps_malloc(REC_BUF/2, MALLOC_CAP_8BIT);
  if (!sbuf||!mbuf) {
    if(sbuf)heap_caps_free(sbuf); if(mbuf)heap_caps_free(mbuf);
    f.close(); return false;
  }

  pikaVibrate(captureMoments?4:3);
  pikaPlayNotify(0);
  recordingEnded=false;recordingEndedDrawn=false;
  if (!startRecordingUi(false, 0, 0, !captureMoments)) {
    heap_caps_free(sbuf); heap_caps_free(mbuf);
    f.close();
    SD_MMC.remove(path); // Only the empty file created by this attempt.
    return false;
  }

  PikaRecordingLight recordingLight;
  uint32_t voicedSamples=0;
  uint32_t totalMono=0, t0=millis(), lastCheckpoint=t0;
  std::vector<uint32_t> momentsMs;
  const int markPin=holdPin==BTN_REC?BTN_PWR:BTN_REC;
  bool markReady = digitalRead(markPin) == HIGH;
  bool audioOk = true;

  while (digitalRead(holdPin)==LOW) {
    if (audio_playback_read((void*)sbuf, REC_BUF) != 0) {
      audioOk = false;
      Serial.println("[Rec] microphone read failed");
      break;
    }
    int mono = REC_BUF/4;
    int64_t energy=0,sum=0;int peak=0,lowest=32767,highest=-32768;
    for (int i=0;i<mono;i++){mbuf[i]=sbuf[i*2];const int value=abs((int)mbuf[i]);energy+=(int64_t)value*value;sum+=mbuf[i];if(mbuf[i]<lowest)lowest=mbuf[i];if(mbuf[i]>highest)highest=mbuf[i];if(value>peak)peak=value;}
    if(!captureMoments&&highest-lowest>1800&&energy*mono-sum*sum>(int64_t)mono*mono*250*250)voicedSamples+=mono;
    size_t written = f.write((uint8_t*)mbuf, mono*2);
    if (written != (size_t)(mono * 2)) {
      audioOk = false;
      Serial.println("[Rec] storage write failed; partial recording retained");
      break;
    }
    totalMono += written;

    const uint32_t now = millis();
    if (now - lastCheckpoint >= 5000UL) {
      if (!writeWavHeader(f, totalMono)) {
        audioOk = false;
        Serial.println("[Rec] WAV checkpoint failed; partial recording retained");
        break;
      }
      lastCheckpoint = now;
      const uint64_t total = SD_MMC.totalBytes(), used = SD_MMC.usedBytes();
      if (used >= total || total - used < 2ULL * 1024ULL * 1024ULL) {
        Serial.println("[Rec] stopping: SD card is nearly full");
        break;
      }
    }

    bool markDown = captureMoments && digitalRead(markPin) == LOW;
    if (!markDown) {
      markReady = true;
    } else if (markReady) {
      markReady = false;
      uint32_t recordedSamples = totalMono / sizeof(int16_t);
      uint32_t markerMs = (recordedSamples * 1000ULL) / SAMPLE_RATE;
      if (momentsMs.empty() || markerMs - momentsMs.back() >= 250) {
        momentsMs.push_back(markerMs);
        startRecordingUi(true, markerMs, (int)momentsMs.size());
      }
    }
  }

  Serial.printf("[Rec] hold ended after %lu ms\n",(unsigned long)(millis()-t0));
  shortCapture=totalMono < SAMPLE_RATE; // mono PCM16: 0.5 seconds = sampleRate bytes
  recordingEnded=!shortCapture;
  // Queue visual acknowledgement before microphone shutdown and SD finalization.
  // An in-flight Listening refresh consumes this flag before it finishes.
  if(!shortCapture&&!recordingUiBusy)startRecordingUi(false,0,0,!captureMoments);
  recordingLight.stop();
  // Close the microphone as soon as the hold ends, before waiting for e-paper.
  audio_idle_suspend();
  heap_caps_free(sbuf); heap_caps_free(mbuf);

  if(shortCapture){f.close();SD_MMC.remove(path);waitForRecordingUi();Serial.println("[Rec] discarded under 500 ms");return false;}
  const bool headerOk = writeWavHeader(f, totalMono);
  f.close();
  pikaPhoneFileTouched(path);

  if (captureMoments && !saveNoteMoments(noteNum, momentsMs)) {
    Serial.println("[Rec] warning: moments were not saved");
  }

  waitForRecordingUi();
  if(!recordingEndedDrawn)showRecordingStopped(!captureMoments);

  Serial.printf("[Rec] done: %lu bytes, %u moments\n",
                (unsigned long)totalMono, (unsigned)momentsMs.size());
  return audioOk && headerOk && totalMono > 1000 && (captureMoments || voicedSamples >= SAMPLE_RATE/5);
}

bool recordWasTooShort(){return shortCapture;}

bool record(int holdPin) {
  if(holdPin<0)holdPin=BTN_REC;
  int num = nextNoteNumber();
  if (num <= 0) return false;
  char path[64]; snprintf(path, sizeof(path), "%s/note_%03d.wav", NOTES_DIR, num);
  bool ok = recordHeldToPath(path, holdPin, true, num);
  if (ok) lastRecNum = num;
  return ok;
}

bool recordQuestion(const char* path, int holdPin) {
  SD_MMC.remove(path);
  return recordHeldToPath(path, holdPin, false, -1);
}

bool playWavFile(const char* path,bool* cancelled) {
  if(cancelled)*cancelled=false;
  File f = SD_MMC.open(path);
  if (!f) return false;
  if (f.size() <= 44) { f.close(); return false; }

  // Do not assume that PCM starts at byte 44. Speech services may add LIST,
  // JUNK or an extended fmt chunk, especially for longer generations.
  uint8_t riff[12];
  if (f.read(riff, sizeof(riff)) != sizeof(riff) ||
      memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
    f.close(); return false;
  }
  uint16_t format = 0, channels = 0, bits = 0;
  uint32_t sampleRate = 0, dataOffset = 0, dataBytes = 0;
  while (f.available() >= 8) {
    uint8_t chunk[8];
    if (f.read(chunk, sizeof(chunk)) != sizeof(chunk)) break;
    const uint32_t chunkSize = (uint32_t)chunk[4] | ((uint32_t)chunk[5] << 8) |
                               ((uint32_t)chunk[6] << 16) | ((uint32_t)chunk[7] << 24);
    const uint32_t chunkData = f.position();
    if (!wavChunkFits(chunkData, chunkSize, f.size())) { f.close(); return false; }
    if (memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16) {
      uint8_t fmt[16];
      if (f.read(fmt, sizeof(fmt)) != sizeof(fmt)) break;
      format = fmt[0] | ((uint16_t)fmt[1] << 8);
      channels = fmt[2] | ((uint16_t)fmt[3] << 8);
      sampleRate = (uint32_t)fmt[4] | ((uint32_t)fmt[5] << 8) |
                   ((uint32_t)fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
      bits = fmt[14] | ((uint16_t)fmt[15] << 8);
    } else if (memcmp(chunk, "data", 4) == 0) {
      dataOffset = chunkData;
      dataBytes = chunkSize;
      break;
    }
    const uint64_t next = uint64_t(chunkData) + chunkSize + (chunkSize & 1U);
    if (next > f.size() || !f.seek(static_cast<uint32_t>(next))) break;
  }
  if (format != 1 || (channels != 1 && channels != 2) || bits != 16 ||
      (sampleRate != 16000 && sampleRate != 24000) || !dataOffset || !wavFramesValid(dataBytes, channels) ||
      !f.seek(dataOffset)) {
    Serial.printf("[Audio] invalid WAV fmt=%u ch=%u rate=%lu bits=%u data=%lu\n",
                  format, channels, static_cast<unsigned long>(sampleRate), bits,
                  static_cast<unsigned long>(dataBytes));
    f.close(); return false;
  }
  Serial.printf("[Audio] playing %s: %lu bytes, %lu Hz, %u ch\n", path,
                static_cast<unsigned long>(dataBytes),
                static_cast<unsigned long>(sampleRate), channels);

  const int monoBytes = 1024;
  uint8_t* monoBuf   = (uint8_t*)heap_caps_malloc(monoBytes,     MALLOC_CAP_8BIT);
  int16_t* stereoBuf = (int16_t*)heap_caps_malloc(monoBytes * 2, MALLOC_CAP_8BIT);

  if (!monoBuf || !stereoBuf) {
    if (monoBuf)   heap_caps_free(monoBuf);
    if (stereoBuf) heap_caps_free(stereoBuf);
    f.close();
    return false;
  }

  audioPlaying  = true;
  stopPlayback  = false;

  pikaSoundSetEnabled(false);
  audio_playback_set_vol(pikaPlaybackVolume());
  delay(25); // Give the codec amplifier time to leave mute before first PCM.

  uint32_t remaining = dataBytes;
  uint32_t playedBytes = 0;
  bool playbackOk = true;
  bool cancelArmed = digitalRead(BTN_REC) == HIGH;
  PikaPcmResampler resampler;
  while (remaining > 0 && !stopPlayback) {
    int readBytes = f.read(monoBuf, min<uint32_t>(monoBytes, remaining));
    if (readBytes <= 0) { playbackOk = false; break; }
    remaining -= readBytes;
    if (!wavFramesValid(readBytes, channels)) { playbackOk = false; break; }

    int samples = readBytes / 2;
    int16_t* input = (int16_t*)monoBuf;
    const int frames = samples / channels;
    const int outputSamples = resampler.convert(input, frames, channels,
                                                sampleRate == 24000, stereoBuf);
    const uint32_t outputBytes = outputSamples * 2 * sizeof(int16_t);
    if (outputBytes) {
      const int result = audio_playback_write((void*)stereoBuf, outputBytes);
      if (result != 0) {
        Serial.printf("[Audio] codec write failed: %d\n", result);
        playbackOk = false;
        break;
      }
      playedBytes += outputBytes;
    }

    if (digitalRead(BTN_REC) == HIGH) cancelArmed = true;
    else if (cancelArmed) {
      delay(20);
      if (digitalRead(BTN_REC) == LOW) {
        stopPlayback = true;
      }
    }
  }

  // I2S writes queue DMA buffers; muting immediately clips the final audio.
  // Flush with 160 ms of silence (more than the configured 6 x 240-frame DMA).
  if (playbackOk && !stopPlayback && playedBytes) {
    memset(stereoBuf, 0, monoBytes * 2);
    for (int i = 0; i < 5; ++i) {
      if (audio_playback_write(stereoBuf, monoBytes * 2) != 0) {
        playbackOk = false;
        break;
      }
    }
  }
  audio_playback_set_vol(0);
  audio_idle_suspend();
  pikaSoundSetEnabled(true);

  heap_caps_free(monoBuf);
  heap_caps_free(stereoBuf);
  f.close();

  audioPlaying = false;
  if(cancelled)*cancelled=stopPlayback;
  stopPlayback = false;
  Serial.printf("[Audio] playback complete: %lu output bytes\n",
                static_cast<unsigned long>(playedBytes));
  return playbackOk && playedBytes > 0;
}
