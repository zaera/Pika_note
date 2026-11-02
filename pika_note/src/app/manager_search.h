#pragma once
// Shared RU/UK/EN case-insensitive matching, streamed from SD in small chunks.
static String managerSearchFold(const String& input){
  String out;out.reserve(input.length());const char* p=input.c_str();
  while(*p){uint32_t c=pikaNextCodepoint(p);
    if(c>='A'&&c<='Z')c+=32;
    else if(c>=0x410&&c<=0x42f)c+=32;
    else if(c>=0x400&&c<=0x40f)c+=80;
    else if(c==0x490)c=0x491;
    else if(c>=0xc0&&c<=0xde&&c!=0xd7)c+=32;
    if(c<0x80)out+=(char)c;
    else if(c<0x800){out+=(char)(0xc0|(c>>6));out+=(char)(0x80|(c&63));}
    else if(c<0x10000){out+=(char)(0xe0|(c>>12));out+=(char)(0x80|((c>>6)&63));out+=(char)(0x80|(c&63));}
    else{out+=(char)(0xf0|(c>>18));out+=(char)(0x80|((c>>12)&63));out+=(char)(0x80|((c>>6)&63));out+=(char)(0x80|(c&63));}
  }return out;
}
static std::vector<String> managerSearchTerms(const String& query){
  std::vector<String> terms;String term;const String folded=managerSearchFold(query);
  for(size_t i=0;i<folded.length();i++){const char c=folded[i];if(c==' '||c=='\t'||c=='\n'){if(term.length()){terms.push_back(term);term="";}}else term+=c;}
  if(term.length())terms.push_back(term);return terms;
}
static bool managerSearchAll(const std::vector<bool>& found){for(bool value:found)if(!value)return false;return true;}
static void managerSearchMark(const String& text,const std::vector<String>& terms,std::vector<bool>& found){
  const String folded=managerSearchFold(text);for(size_t i=0;i<terms.size();i++)if(!found[i]&&folded.indexOf(terms[i])>=0)found[i]=true;
}
static String managerSearchMetadata(const BrainMemory& brain){
  String text=brain.title+" "+brain.summary+" "+brain.kind+" "+brain.language;
  for(const auto* values:{&brain.topics,&brain.people,&brain.projects,&brain.decisions,&brain.facts,&brain.questions,&brain.tasks})for(const auto& value:*values)text+=" "+value;
  return text;
}
static bool managerSearchNote(int num,const String& metadata,const std::vector<String>& terms,String& excerpt){
  std::vector<bool> found(terms.size(),false);managerSearchMark(metadata,terms,found);
  char path[80];snprintf(path,sizeof(path),"%s/note_%03d.txt",NOTES_DIR,num);File file=SD_MMC.open(path);
  String tail;char buffer[512];
  while(file&&file.available()){
    const size_t count=file.readBytes(buffer,sizeof(buffer));if(!count)break;
    String chunk=tail;chunk.concat(buffer,count);const auto previous=found;managerSearchMark(chunk,terms,found);
    if(excerpt.length()==0&&previous!=found)excerpt=chunk.substring(0,pikaUtf8Prefix(chunk.c_str(),420));
    if(managerSearchAll(found))break;
    tail=chunk.length()>256?chunk.substring(chunk.length()-256):chunk;
  }if(file)file.close();
  return managerSearchAll(found);
}
static void handleManagerArchiveSearch(){
  managerLastRequest=millis();String query=transferServer.arg("q");query.trim();
  if(!query.length()||query.length()>160){managerJsonError(400,"Search must contain 1 to 160 UTF-8 bytes");return;}
  const auto terms=managerSearchTerms(query);if(terms.empty()){managerJsonError(400,"Enter a search term");return;}
  loadIndex();const String today=managerUtcToLocalInput(currentUtcIso(),true);
  JsonDocument doc;JsonArray results=doc["results"].to<JsonArray>();const int cursor=max(0L,transferServer.arg("cursor").toInt());
  if(cursor==0)for(const auto& task:loadBrainTasks()){
    const bool event=task.type=="event";const String date=managerUtcToLocalInput(task.dueUtc,event&&task.datePrecision=="day");
    const String last=managerUtcToLocalInput(task.endUtc.length()?task.endUtc:task.dueUtc,true);
    if(event?(!task.archived&&(!last.length()||last>=today)):!task.done)continue;
    String metadata=task.text+" "+date+" "+managerPriorityName(task.priority);
    if(task.noteNum>0){metadata+=" "+managerSearchMetadata(loadBrainMemory(task.noteNum));for(const auto& note:noteIndex)if(note.num==task.noteNum)metadata+=" "+String(note.tag);}
    std::vector<bool> found(terms.size(),false);managerSearchMark(metadata,terms,found);if(!managerSearchAll(found))continue;
    JsonObject result=results.add<JsonObject>();result["kind"]=event?"events":"tasks";result["id"]=task.id;result["title"]=task.text;result["date"]=date;
  }
  const int end=std::min(cursor+20,(int)noteIndex.size());
  for(int position=cursor;position<end;position++){
    const auto& note=noteIndex[noteIndex.size()-1-position];if(!managerArchived(note.num))continue;
    const BrainMemory brain=loadBrainMemory(note.num);const String date=managerUtcToLocalInput(noteCreatedUtc(note.num));
    const String metadata=managerSearchMetadata(brain)+" "+String(note.tag)+" "+date;String excerpt;
    if(!managerSearchNote(note.num,metadata,terms,excerpt))continue;
    JsonObject result=results.add<JsonObject>();result["kind"]="memories";result["id"]="m"+String(note.num);result["date"]=date;
    result["title"]=brain.title.length()?brain.title:"Memory #"+String(note.num);result["text"]=excerpt.length()?excerpt:managerReadText(note.num,260);result["tag"]=note.tag;
  }
  doc["nextCursor"]=end<(int)noteIndex.size()?end:-1;
  String out;serializeJson(doc,out);transferServer.sendHeader("Cache-Control","no-store");transferServer.send(200,"application/json; charset=utf-8",out);
}
