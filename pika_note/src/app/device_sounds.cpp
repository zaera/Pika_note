#include "error_log.h"
#include <atomic>
#include "device_extras.h"
#include "settings.h"
#include "../../config.h"
#include "../../sounds.h"
namespace {
struct Note{float hz;int ms;};
const Note chime[]={{659,130},{784,130},{988,220},{0,0}};
const Note bell[]={{880,200},{0,100},{880,200},{0,0}};
const Note rise[]={{523,100},{659,100},{784,100},{1047,180},{0,0}};
const Note fall[]={{1047,100},{784,100},{659,100},{523,180},{0,0}};
const Note pulse[]={{740,90},{0,70},{740,90},{0,70},{988,180},{0,0}};
const Note tap[]={{880,65},{0,0}};
const Note ok[]={{784,55},{1047,85},{0,0}};
const Note error[]={{440,95},{0,30},{294,140},{0,0}};
const Note mail[]={{988,65},{0,35},{1319,100},{0,0}};
const Note tick[]={{1109,45},{0,0}};
const Note doubleTap[]={{880,50},{0,40},{880,50},{0,0}};
const Note wake[]={{784,45},{1175,65},{0,0}};
const Note sleep[]={{659,55},{440,75},{0,0}};
const Note* patterns[]={chime,bell,rise,fall,pulse,tap,ok,error,mail,tick,doubleTap,wake,sleep};
std::atomic<bool> mailPending{false};
int pendingMelody=-1,pendingVolume=60;uint32_t pendingAt=0;
void playMelody(int melody,int volume){
 if(melody<0||melody>12)return;
 const bool enabled=pikaSoundIsEnabled();pikaSoundSetEnabled(true);audio_playback_set_vol(volume);
 // Open/prime the output before the short attack, then flush all queued PCM
 // before muting. The I2S DMA ring can exceed a complete short notification.
 int16_t silence[1024]={};
 if(audio_playback_write(silence,sizeof(silence))!=0)pikaError("sound","melody","I2S output initialization failed");
 for(const Note* note=patterns[melody];note->ms;note++){if(note->hz>0)playToneUI(note->hz,note->ms,.22f);else delay(note->ms);delay(35);}
 for(int i=0;i<5;i++)audio_playback_write(silence,sizeof(silence));
 delay(30);audio_playback_set_vol(0);audio_idle_suspend();pikaSoundSetEnabled(enabled);
}
}
void pikaQueueSoundTest(int melody,int volume){pendingMelody=melody;pendingVolume=volume;pendingAt=millis();}
void pikaServiceSoundTest(){if(mailPending.exchange(false))pikaPlayNotify(4);if(pendingMelody<0)return;const int melody=pendingMelody,volume=pendingVolume;pendingMelody=-1;if(millis()-pendingAt>2000){Serial.println("PIKA_SOUND_TEST_EXPIRED");return;}Serial.printf("PIKA_SOUND_TEST melody=%d volume=%d\n",melody,volume);playMelody(melody,volume);}
void pikaPlayAlertMelody(){playMelody(pikaAlertMelody(),pikaPlaybackVolume());}

void pikaPlayNotify(int kind){

 if(kind==6)pikaVibrate(1);else if(kind==2)pikaVibrate(2);else if(kind==1||kind==3)pikaVibrate(5);
 int selected=pikaNotifyMelody(kind);if(selected<0)return;
 playMelody(selected,pikaPlaybackVolume());
 Serial.printf("PIKA_NOTIFY kind=%d melody=%d\n",kind,selected);
}
void pikaQueueMailNotify(){mailPending=true;}
