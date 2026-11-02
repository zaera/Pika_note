#pragma once

bool record(int holdPin = -1);
bool recordQuestion(const char* path, int holdPin);
bool playWavFile(const char* path,bool* cancelled = nullptr);

bool recordWasTooShort();
