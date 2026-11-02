from pathlib import Path
import subprocess, tempfile, sys

root = Path(__file__).resolve().parents[2]
source = (root / 'pika_note/src/app/phone_link.cpp').read_text(encoding='utf-8')

def function(signature):
    a = source.index(signature)
    b = source.index('{', a)
    depth, i = 1, b + 1
    while depth:
        if source[i] == '{': depth += 1
        if source[i] == '}': depth -= 1
        i += 1
    return source[a:i]

assert 'pikaPhoneSend(' not in function('void pikaPhoneForeground(')
assert 'pikaPhoneSend(' not in function('void pikaPhoneService(')
ino = (root / 'pika_note/pika_note.ino').read_text(encoding='utf-8')
loop = ino[ino.index('void loop() {'):]
assert loop.index('handleIdle()') < loop.index('pikaPhoneSyncTick()')
ask = ino[ino.index('void askBrain('):ino.index('void playDailyBriefing(')]
assert ask.index('recordQuestion(') < ask.index('phoneRequired()')

stubs = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
std::atomic<bool> foreground{false},foregroundNotice{false};
constexpr int pdTRUE=1,BLE_HS_EDONE=14;int ack=1;std::atomic<int> ackCode{BLE_HS_EDONE};bool indicationPending=true,shuttingDown=false,interrupt=false,token=false;int conn=1;uint32_t now=0;
struct Server{int disconnects=0;void disconnect(int){++disconnects;}} object,*server=&object;
uint32_t millis(){return now;}int pdMS_TO_TICKS(int x){return x;}
int xSemaphoreTake(int,int ms){now+=ms;if(token){token=false;return pdTRUE;}return 0;}
bool preemptBackground(){return interrupt;}
'''
main = r'''
int main(){
 pikaPhoneForeground(true);assert(foreground&&foregroundNotice);foregroundNotice=false;pikaPhoneForeground(true);assert(!foregroundNotice);pikaPhoneForeground(false);assert(!foreground&&foregroundNotice);
 interrupt=true;assert(!finishIndication(true));assert(now<=5&&indicationPending);
 interrupt=false;token=true;assert(finishIndication(false));assert(!indicationPending);
 indicationPending=true;now=0;assert(!finishIndication(false));assert(now==8000&&server->disconnects==1&&!indicationPending);
 puts("Production foreground pause nonblocking; background ACK interrupted in 5 ms; pending ACK drained; dead peer bounded; Home and microphone precede sync/connection: PASS");
}
'''
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(stubs + function('bool finishIndication(bool interruptible){') + function('void pikaPhoneForeground(') + main, encoding='utf-8')
    exe = p / 'foreground.exe'
    subprocess.run((sys.argv[1:] or ['c++']) + [str(p/'test.cpp'), '-std=c++17', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
