#pragma once
#include <string>
#include <vector>
#include "utf8_text.h"

inline std::string voiceCommandFold(const char* text){
  std::string out;
  while(*text){uint32_t c=pikaNextCodepoint(text);
    if(c>='A'&&c<='Z')c+=32;
    if(c>=0x410&&c<=0x42f)c+=32;if(c==0x401)c=0x451;
    if(c<128){out+=((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='\'')?char(c):' ';}
    else if(c>=0x400&&c<=0x4ff){out+=char(0xc0|(c>>6));out+=char(0x80|(c&63));}
    else out+=' ';
  }return out;
}
inline bool voiceCommandHas(const std::string& text,const char* term){
  return (" "+text+" ").find(std::string(" ")+term+" ")!=std::string::npos;
}
// Only an entirely numeric label is normalized; ordinary prose is unchanged.
inline std::string voiceNumericLabel(const char* speech){
 const std::string text=voiceCommandFold(speech);std::vector<std::string> words;size_t at=0;
 while(at<text.size()){if(text[at]==' '){++at;continue;}size_t end=text.find(' ',at);if(end==std::string::npos)end=text.size();words.push_back(text.substr(at,end-at));at=end;}
 if(words.empty())return "";
 auto digit=[](const std::string& w)->int{const char* ru[]={"ноль","один","два","три","четыре","пять","шесть","семь","восемь","девять"};const char* en[]={"zero","one","two","three","four","five","six","seven","eight","nine"};for(int i=0;i<10;i++)if(w==ru[i]||w==en[i])return i;return -1;};
 std::string joined;bool digits=true;for(const auto& word:words){int n=digit(word);if(n>=0)joined+=char('0'+n);else{for(char c:word)if(c<'0'||c>'9')digits=false;joined+=word;}}if(digits&&joined.size()<=16)return joined;
 const std::vector<std::pair<const char*,int>> values={{"десять",10},{"одиннадцать",11},{"двенадцать",12},{"тринадцать",13},{"четырнадцать",14},{"пятнадцать",15},{"шестнадцать",16},{"семнадцать",17},{"восемнадцать",18},{"девятнадцать",19},{"двадцать",20},{"тридцать",30},{"сорок",40},{"пятьдесят",50},{"шестьдесят",60},{"семьдесят",70},{"восемьдесят",80},{"девяносто",90},{"сто",100},{"двести",200},{"триста",300},{"четыреста",400},{"пятьсот",500},{"шестьсот",600},{"семьсот",700},{"восемьсот",800},{"девятьсот",900},{"ten",10},{"eleven",11},{"twelve",12},{"thirteen",13},{"fourteen",14},{"fifteen",15},{"sixteen",16},{"seventeen",17},{"eighteen",18},{"nineteen",19},{"twenty",20},{"thirty",30},{"forty",40},{"fifty",50},{"sixty",60},{"seventy",70},{"eighty",80},{"ninety",90}};
 int total=0,lastPlace=1000;bool valueSeen=false;for(size_t i=0;i<words.size();++i){const auto& word=words[i];if(word=="and")continue;int n=digit(word);for(const auto& v:values)if(word==v.first)n=v.second;
  if(n>=1&&n<=9&&i+1<words.size()&&words[i+1]=="hundred"){n*=100;++i;}
  if(n<0)return "";valueSeen=true;int place=n>=100?100:n>=10?10:1;if(place>=lastPlace)return "";lastPlace=place;total+=n;if(n>=11&&n<=19&&i+1<words.size())return "";
 }return valueSeen?std::to_string(total):std::string();
}
// Match a complete numeric name, never a prefix of a longer spoken number.
inline bool voiceNumericTitleMentioned(const char* speech,const char* title){
 const auto wanted=voiceNumericLabel(title);if(wanted.empty())return false;
 const auto folded=voiceCommandFold(speech);std::string run;bool matched=false;size_t at=0;
 auto flush=[&](){if(!run.empty()&&voiceNumericLabel(run.c_str())==wanted)matched=true;run.clear();};
 while(at<folded.size()){
  if(folded[at]==' '){++at;continue;}size_t end=folded.find(' ',at);if(end==std::string::npos)end=folded.size();const auto word=folded.substr(at,end-at);
  if(!voiceNumericLabel(word.c_str()).empty()||word=="hundred"||word=="and"){if(!run.empty())run+=' ';run+=word;}else flush();at=end;
 }flush();return matched;
}
inline bool voiceRecordCommandCandidate(const char* speech){
  const auto text=voiceCommandFold(speech);bool verb=false,noun=false;
  for(const char* value:{"delete","remove","forget","archive","complete","finish","mark","restore","remember","reopen","undo","удали","удалить","удалите","забудь","забудьте","заархивируй","архивируй","отметь","отметьте","пометь","пометьте","выполни","заверши","закрой","восстанови","восстановить","восстановите","верни","разархивируй"})verb|=voiceCommandHas(text,value);
  for(const char* value:{"memory","memories","memo","note","notes","task","tasks","todo","event","events","appointment","meeting","меморис","мемориса","мемори","заметку","заметки","запись","задачу","задачи","задача","задачку","таску","таски","ивент","ивенты","событие","события","встречу"})noun|=voiceCommandHas(text,value);
  return verb&&noun;
}
// These utterances cannot authorize a mutation even if a model returns an action.
inline bool voiceRecordCommandBlocked(const char* speech){
  const auto text=voiceCommandFold(speech);
  for(const char* value:{"если","if","unless"})if(voiceCommandHas(text,value))return true;
  size_t verb=text.size();for(const char* value:{"delete","remove","forget","archive","complete","finish","mark","restore","remember","reopen","undo","удали","удалить","удалите","забудь","забудьте","заархивируй","архивируй","отметь","отметьте","пометь","пометьте","выполни","заверши","закрой","восстанови","восстановить","восстановите","верни","разархивируй"}){
    const size_t found=(" "+text+" ").find(std::string(" ")+value+" ");if(found!=std::string::npos&&found<verb)verb=found;
  }
  const auto prefix=text.substr(0,verb);
  for(const char* value:{"не","нельзя","никогда","don't","dont","not","never","how","как","почему","why","whether"})if(voiceCommandHas(prefix,value))return true;
  return false;
}

inline bool voiceRecordRestoreRequested(const char* speech){const auto text=voiceCommandFold(speech);for(const char* value:{"restore","remember","reopen","undo","восстанови","восстановить","восстановите","верни","разархивируй"})if(voiceCommandHas(text,value))return true;return false;}
inline const char* voiceRecordRequestedKind(const char* speech){
 const auto text=" "+voiceCommandFold(speech)+" ";size_t position=text.size();const char* kind="";
 for(const auto& entry:std::vector<std::pair<const char*,const char*>>{{"memories","memory"},{"memories","memories"},{"memories","memo"},{"memories","note"},{"memories","notes"},{"memories","меморис"},{"memories","мемориса"},{"memories","мемори"},{"memories","заметку"},{"memories","заметки"},{"memories","запись"},{"tasks","task"},{"tasks","tasks"},{"tasks","todo"},{"tasks","to do"},{"tasks","задачу"},{"tasks","задачи"},{"tasks","задача"},{"tasks","задачку"},{"tasks","таску"},{"tasks","таска"},{"tasks","таски"},{"events","event"},{"events","events"},{"events","meeting"},{"events","appointment"},{"events","событие"},{"events","события"},{"events","ивент"},{"events","ивенты"},{"events","встречу"}}){auto found=text.find(std::string(" ")+entry.second+" ");if(found<position){position=found;kind=entry.first;}}
 return kind;
}
inline bool voiceRecordEvidenceValid(const char* source,const char* evidence){
 auto trim=[](std::string value){const auto first=value.find_first_not_of(' ');if(first==std::string::npos)return std::string();return value.substr(first,value.find_last_not_of(' ')-first+1);};
 const auto excerpt=trim(voiceCommandFold(evidence));if(excerpt.empty())return false;
 if(excerpt.size()>=4)return voiceCommandFold(source).find(excerpt)!=std::string::npos;
 // Short titles (123, UI, X) require the whole record, not a substring.
 // Callers join title and body with a newline; repeated identical title/body is valid.
 const std::string text(source);bool found=false;size_t at=0;
 do{const size_t end=text.find('\n',at);const auto line=trim(voiceCommandFold(text.substr(at,end==std::string::npos?std::string::npos:end-at).c_str()));
  if(!line.empty()){if(line!=excerpt)return false;found=true;}if(end==std::string::npos)break;at=end+1;
 }while(at<=text.size());return found;
}

inline bool voiceMutationGestureAllowed(int holdPin,int lowerPin){return holdPin==lowerPin;}

inline bool voiceArchiveRequested(const char* speech){
 const auto text=voiceCommandFold(speech);
 for(const char* phrase:{"без архив", "не ищи в архив", "не используй архив", "without archive", "excluding archive", "not in archive"})if(text.find(phrase)!=std::string::npos)return false;
 size_t from=0;while(from<text.size()){const auto end=text.find(' ',from);const auto word=text.substr(from,end==std::string::npos?end:end-from);if(word.find("архив")==0||word=="archive"||word=="archives"||word=="archived")return true;if(end==std::string::npos)break;from=end+1;}
 return false;
}
inline bool voiceCreateCandidate(const char* speech){
 const auto text=voiceCommandFold(speech);bool verb=false,noun=false;
 for(const char* term:{"create","add","schedule","make","set","put","создай","создать","создайте","добавь","добавить","добавьте","запланируй","запланировать","запланируйте","сделай","сделать","сделайте","поставь","поставить","поставьте","заведи","завести","заведите","организуй","назначь","назначить","запиши","записать"})verb|=voiceCommandHas(text,term);
 for(const char* term:{"task","tasks","todo","to do","event","events","appointment","meeting","задачу","задача","задачи","задачку","таску","таска","таски","событие","события","ивент","ивенты","встречу"})noun|=voiceCommandHas(text,term);
 return verb&&noun;
}

inline bool voiceMemoryOverviewRequested(const char* speech){
 const auto text=voiceCommandFold(speech);bool noun=false,overview=false;
 for(const char* term:{"memories","notes","меморисы","меморисов","заметки","заметок","записи","записей"})noun|=voiceCommandHas(text,term);
 for(const char* term:{"all","list","which","what","все","всё","список","какие","перечисли","сколько"})overview|=voiceCommandHas(text,term);
 return noun&&overview;
}
