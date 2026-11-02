#pragma once
void pikaActivityLedBegin();
bool pikaActivityLedEnabled();
bool pikaActivityLedSave(bool enabled);
void pikaActivityLedConnection(bool active);
void pikaActivityLedCompanion(bool active);
void pikaActivityLedSleep();
class PikaConnectionLight{
 public:PikaConnectionLight(){pikaActivityLedConnection(true);}~PikaConnectionLight(){pikaActivityLedConnection(false);}
 PikaConnectionLight(const PikaConnectionLight&)=delete;PikaConnectionLight& operator=(const PikaConnectionLight&)=delete;
};

void pikaActivityLedRecording(bool active);
void pikaActivityLedWake();
class PikaRecordingLight{
 bool active=true;
 public:PikaRecordingLight(){pikaActivityLedRecording(true);}~PikaRecordingLight(){stop();}
 void stop(){if(active){active=false;pikaActivityLedRecording(false);}}
 PikaRecordingLight(const PikaRecordingLight&)=delete;PikaRecordingLight& operator=(const PikaRecordingLight&)=delete;
};

void pikaActivityLedHomeReady();

void pikaActivityLedBusy(bool active);
class PikaBusyLight{
 bool active=true;
 public:PikaBusyLight(){pikaActivityLedBusy(true);}~PikaBusyLight(){stop();}
 void stop(){if(active){active=false;pikaActivityLedBusy(false);}}
 PikaBusyLight(const PikaBusyLight&)=delete;PikaBusyLight& operator=(const PikaBusyLight&)=delete;
};

void pikaActivityLedTraffic(bool active);
