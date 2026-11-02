#pragma once
#include <Arduino.h>
#include <NetworkClient.h>
bool pikaPhoneOnline();
bool pikaPhoneWait(uint32_t timeoutMs);
bool pikaPhoneBusy();
void pikaPhoneBegin();
void pikaPhoneService();
void pikaPhoneSleep();
bool pikaPhoneTakeRpc(String& request,uint8_t& channel);
bool pikaPhoneSend(uint8_t op,uint8_t channel,const uint8_t* data,size_t size);
void pikaPhoneReply(uint8_t channel,int status,const String& body,bool bulk=false);
class PhoneSecureClient:public NetworkClient {
 int channel=-1;uint32_t timeoutMs=30000;
 public:
 ~PhoneSecureClient(){stop();}
 bool failed()const;
 int connectSpeech(const String& payload);
 int lastError(char* buffer,size_t n){if(n)snprintf(buffer,n,"Phone unavailable");return -1;}
 void setCACert(const char*){}void setHandshakeTimeout(unsigned){}void setTimeout(uint32_t n){timeoutMs=n;Stream::setTimeout(n);}
 int connect(const char* host,uint16_t port)override{return connect(host,port,30000);}
 int connect(const char* host,uint16_t port,int32_t timeout)override;
 int connect(IPAddress,uint16_t)override{return 0;}
 int connect(IPAddress,uint16_t,int32_t)override{return 0;}
 size_t write(uint8_t b)override{return write(&b,1);}
 size_t write(const uint8_t* bytes,size_t n)override;
 int available()override;int read()override;int peek()override;int read(uint8_t* out,size_t n)override;
 size_t readBytes(char* out,size_t n);
 void flush()override{}void stop()override;uint8_t connected()override;
 void setConnectionTimeout(uint32_t n)override{timeoutMs=n;}
 int fd()const override{return -1;}
};

void pikaPhoneChanged();
bool pikaPhoneHomeDirty();
uint32_t pikaPhoneGeneration();

uint32_t pikaPhonePairCode();

String pikaPhoneDiagnostics();

void pikaPhoneForeground(bool active);
bool pikaPhoneForegroundActive();
bool pikaPhoneBackgroundInterrupted();
void pikaPhoneBackgroundReset();
bool pikaPhoneTransferRecent();

void pikaPhoneRequestSync();

void pikaPhoneUseWindowUpload(bool enabled);

bool pikaPhoneQueueLiveSnapshot(const String& json);

void pikaPhoneRequestHomeRefresh();
