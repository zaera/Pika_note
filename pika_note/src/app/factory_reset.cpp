#include "factory_reset.h"
#include "data_files.h"
#include <Preferences.h>
#include <esp_partition.h>
#include <SD_MMC.h>
#include <mbedtls/sha256.h>
namespace {
bool open(Preferences& p){return p.begin("pikaFactory",false);}
}
bool pikaFactoryPending(){Preferences p;if(!open(p))return true;bool pending=p.getBool("pending",false);p.end();return pending;}
bool pikaFactoryConfigSuppressed(){Preferences p;if(!open(p))return true;const bool disabled=p.getBool("noSeed",false);p.end();return disabled;}
bool pikaFactoryRequest(const String& pin,String& error){
 if(pin.length()!=4){error="Choose a new 4-digit PIN";return false;}for(char c:pin)if(c<'0'||c>'9'){error="PIN must contain four digits";return false;}
 unsigned char digest[32];char hex[65];if(mbedtls_sha256((const unsigned char*)pin.c_str(),pin.length(),digest,0)){error="Could not secure the new PIN";return false;}
 for(int i=0;i<32;++i)sprintf(hex+i*2,"%02x",digest[i]);hex[64]=0;
 Preferences p;if(!open(p)){error="Reset journal unavailable";return false;}
 const bool ok=p.putString("newPin",hex)==64&&p.putBool("noSeed",true)==1&&p.putBool("pending",true)==1;p.end();if(!ok)error="Could not save reset intent";return ok;
}
bool pikaFactoryPrepareBoot(){
 Preferences journal;if(!open(journal))return false;if(!journal.getBool("pending",false)){journal.end();return true;}
 const String hash=journal.getString("newPin","");if(hash.length()!=64){journal.end();return false;}
 for(const char* name:{"pika","pikaPin","pikaMail","pikaWeather","pikaExtras","pikaLed","pikaClock"}){Preferences p;if(!p.begin(name,false)){journal.end();return false;}const bool ok=p.clear();p.end();if(!ok){journal.end();return false;}}
 for(const char* label:{"pikacfg","coredump"}){const esp_partition_t* part=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,label);if(part&&esp_partition_erase_range(part,0,part->size)!=ESP_OK){journal.end();return false;}}
 Preferences pin;if(!pin.begin("pikaPin",false)){journal.end();return false;}const bool ok=pin.putString("hash",hash)==64&&pin.putInt("length",4)==4;pin.end();journal.end();return ok;
}
bool pikaFactoryRecoverData(String& error){
 Preferences journal;if(!open(journal)){error="Reset journal unavailable";return false;}if(!journal.getBool("pending",false)){journal.end();return true;}
 // Deleting these exact roots is restartable; unrelated files on SD are untouched.
 for(const char* path:{"/notes","/.pika-restore-stage","/.pika-restore-old"})if(!pikaDataRemoveTree(path)){journal.end();error="Reset interrupted while clearing Pika data; restart to continue";return false;}
 for(const char* suffix:{"",".tmp",".bak"}){const String path=String("/.pika-restore-journal.json")+suffix;if(SD_MMC.exists(path)&&!SD_MMC.remove(path)){journal.end();error="Could not clear restore journal";return false;}}
 for(const char* suffix:{"",".tmp",".bak"}){String sync=String("/.pika-phone-sync.json")+suffix;if(SD_MMC.exists(sync)&&!SD_MMC.remove(sync)){journal.end();error="Could not clear sync catalog";return false;}}
 if(SD_MMC.exists("/.pika-errors.bin")&&!SD_MMC.remove("/.pika-errors.bin")){journal.end();error="Could not clear diagnostics";return false;}
 if(!SD_MMC.mkdir("/notes")){journal.end();error="Could not initialize empty notes";return false;}
 // Release ownership only after the SD erase completed. BLE does not start
 // during pending recovery, so no new owner can claim partially erased data.
 if(journal.getBool("purgeOwner",false)){Preferences owner;if(!owner.begin("pikaOwner",false)){journal.end();error="Owner reset unavailable";return false;}bool cleared=owner.clear()&&owner.putBool("clearBonds",true)==1;owner.end();if(!cleared){journal.end();error="Could not clear device owner";return false;}}
 const bool ok=journal.putBool("pending",false)==1;if(ok){journal.remove("newPin");journal.remove("purgeOwner");}journal.end();if(!ok)error="Could not finish reset journal";return ok;
}
