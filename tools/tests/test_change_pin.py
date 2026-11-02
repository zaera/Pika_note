from pathlib import Path
import runpy,subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2];b=runpy.run_path(str(root/'tools/tests/test_web_pin_policy.py'))
src=(root/'pika_note/src/app/web_pin.cpp').read_text(encoding='utf-8');change=src[src.index('bool pikaWebChangePin('):]
stubs=b['stubs'].replace('struct Preferences{};', 'struct Preferences{std::map<std::string,String> values;size_t putString(const char* key,const String& value){values[key]=value;return 64;}};')
main=r'''
int main(){
 String error;pinHash="1234";sessions[0].token="active-session";
 assert(!pikaWebChangePin("1234","12a4",error)&&pinHash=="1234");
 for(int i=0;i<5;i++)assert(!pikaWebChangePin("0000","5678",error));assert(!pikaWebChangePin("1234","5678",error)&&pinHash=="1234");tick+=30001;
 assert(pikaWebChangePin("1234","5678",error));assert(pinHash=="5678"&&prefs.values["hash"]=="5678"&&pinLength==4&&sessions[0].token.length()==0);
 assert(!pikaWebChangePin("1234","9999",error));assert(pikaWebChangePin("5678","0001",error));assert(pinHash=="0001");
 puts("Production PIN change: digits, current PIN, throttle, persistent write, leading zero and session invalidation: PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(stubs+b['namespace']+change+main,encoding='utf-8');exe=p/'pin.exe';subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-I',str(root/'tools/tests/host'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
assert 'prefs.getString("pinCfg","")!=hash' in src and 'prefs.putString("pinCfg",hash)' in src
assert '-webkit-tap-highlight-color:transparent' in src and 'button:focus-visible{outline:none;box-shadow:0 0 0 2px #ef573f}' in src
print('PIN config fingerprint preserves changed PIN across boot; mobile tap rectangle disabled: PASS')
