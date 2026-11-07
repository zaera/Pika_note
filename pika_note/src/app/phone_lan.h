#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
bool pikaLanConfigure(JsonVariantConst request);
bool pikaLanOnline();
bool pikaLanConfigured();
bool pikaLanSend(uint8_t op,uint8_t channel,const uint8_t* data,size_t size);
void pikaLanStop();
void pikaPhoneReceiveLan(const uint8_t* frame,size_t size);
void pikaPhoneLanLost();
void pikaPhoneLanTraffic();
