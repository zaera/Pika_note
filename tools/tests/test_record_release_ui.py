from pathlib import Path
import subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'pika_note/src/app/record.cpp').read_text()
code=s[s.index('namespace {'):s.index('static bool writeWavHeader')]
stub=r'''#include <atomic>
#include <cstdint>
#include <cassert>
#include <cstdio>
using Fn=void(*)(void*);Fn pending=nullptr;bool fail=false,releaseDuringPaint=false;int listening=0,stopped=0;bool stoppedQuestion=false;int vibrations=0,beeps=0;bool held=true;
constexpr int BTN_PWR=18,LOW=0;int digitalRead(int){return held?LOW:1;}
void pikaVibrate(int){assert(listening>0);++vibrations;}void pikaPlayNotify(int){++beeps;}
constexpr int pdPASS=1;using BaseType_t=int;
struct Log{void println(const char*){} template<class... A>void printf(const char*,A...){}}Serial;
bool homePainting=false;int homeWaits=0;uint32_t millis(){return 100;}
void pikaWaitHomeRefresh(){++homeWaits;homePainting=false;}
void showRecordingProgress(uint32_t,int){} void showRecording();void showAskListening();
void showRecordingStopped(bool q){++stopped;stoppedQuestion=q;}
void vTaskDelete(void*){}int xTaskCreatePinnedToCore(Fn fn,const char*,int,void*,int,void*,int){if(fail)return 0;pending=fn;return pdPASS;}
void delay(int){assert(pending);Fn f=pending;pending=nullptr;f(nullptr);}
'''
main=r'''void showRecording(){assert(!homePainting);++listening;if(releaseDuringPaint){recordingEnded=true;recordingReleased=true;}}
void showAskListening(){showRecording();}
int main(){
 homePainting=true;startRecordingUi();assert(recordingUiBusy&&listening==0&&homeWaits==0&&homePainting);releaseDuringPaint=true;waitForRecordingUi();assert(homeWaits==1&&listening==1&&stopped==1&&recordingEndedDrawn&&!stoppedQuestion);
 assert(vibrations==0&&beeps==0);recordingReleased=false;recordingEnded=false;recordingEndedDrawn=false;releaseDuringPaint=false;startRecordingUi(false,0,0,true);waitForRecordingUi();assert(listening==2&&stopped==1&&vibrations==1&&beeps==1);
 recordingEnded=true;startRecordingUi(false,0,0,true);waitForRecordingUi();assert(listening==2&&stopped==2&&stoppedQuestion&&vibrations==1);
 fail=true;assert(!startRecordingUi());assert(!recordingUiBusy);
 puts("Release during panel update and after panel update: PASS");}
'''
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'test.cpp').write_text(stub+code+main);exe=p/'record.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
