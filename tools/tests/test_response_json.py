from pathlib import Path
import json,re,subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
module=(root/'pika_note/src/app/ask_assistant.inc').read_text(encoding='utf-8')
schema=json.loads(re.search(r'planSchema\[\]=R"json\((.*?)\)json"',module,re.S).group(1))
fixture={'status':'completed','text':{'format':{'type':'json_schema','schema':schema}},'output':[{'type':'message','content':[{'type':'output_text','text':'{"status":"ready","operations":[]}'}]}]}
network=(root/'pika_note/src/app/network.cpp').read_text(encoding='utf-8');a=network.index('static String responseOutputText(const String& response) {');b=network.index('static bool createEmbedding(',a)
code='''#include <string>
#include <cassert>
#include <iostream>
using String=std::string;
#include "response_json.h"
#include "memory_response.h"
struct {template<class... Args>void printf(const char*,Args...) {}} Serial;
'''+network[a:b]+'''int main(){
 const String input=R"fixture('''+json.dumps(fixture,ensure_ascii=False)+''')fixture";
 JsonDocument old;assert(deserializeJson(old,input)==DeserializationError::TooDeep);
 JsonDocument fixed;assert(!parseAiResponse(fixed,input));
 assert(responseOutputText(input)=="{\\"status\\":\\"ready\\",\\"operations\\":[]}");
 std::string excessive="0";for(int i=0;i<40;i++)excessive="["+excessive+"]";
 JsonDocument bound;assert(parseAiResponse(bound,excessive)==DeserializationError::TooDeep);
 std::cout<<"Responses schema echo reproduces TooDeep; production envelope and output parsers accept plan; depth remains bounded: PASS\\n";
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.cpp').write_text(code,encoding='utf-8');exe=p/'response-json.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-I',str(root/'pika_note/src/app'),'-I',str(root/'.pio/libdeps/pika_note_2/ArduinoJson/src'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
