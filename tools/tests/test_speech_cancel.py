from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
s=(root/'pika_note/src/app/speech_stream.cpp').read_text(encoding='utf-8');a=s.index('bool PikaSpeechStream::poll()');b=s.index('bool PikaSpeechStream::put(',a)
code='''#include <cassert>
#include <cstdint>
#include <iostream>
constexpr int BTN_REC=0,BTN_PWR=1,HIGH=1,LOW=0;int pins[2]={HIGH,HIGH};uint32_t now=100;int digitalRead(int p){return pins[p];}uint32_t millis(){return now;}
unsigned pikaScreenButtonsPending(){return 0;}
struct PikaSpeechStream{bool cancelLower=false,cancelArmed=false,cancelled=false,aborting=false,failed=false;uint32_t lowSince=0;bool poll();};
'''+s[a:b]+'''
int main(){PikaSpeechStream normal;normal.cancelArmed=true;pins[BTN_PWR]=LOW;assert(normal.poll());PikaSpeechStream mail;mail.cancelLower=true;assert(!mail.poll()&&mail.cancelled&&mail.aborting);pins[BTN_PWR]=HIGH;pins[BTN_REC]=LOW;assert(normal.poll());now+=21;assert(!normal.poll()&&normal.cancelled);PikaSpeechStream released;released.cancelLower=true;pins[BTN_REC]=HIGH;assert(released.poll());std::cout<<"Production speech cancel: lower exits Mail immediately; upper debounce and ordinary Ask preserved: PASS\\n";}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.cpp').write_text(code,encoding='utf-8');exe=p/'speech-cancel.exe';subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
