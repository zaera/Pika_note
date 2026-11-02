// Included in network.cpp after the shared OpenAI transport helpers.
#include "manager_store.h"
#include "voice_command_policy.h"
#include "voice_command_prompt.h"
static void invalidatePortalCache();
namespace {
struct VoiceRecordCandidate{String id,kind,title,text;int note=-1;bool archived=false,manualArchived=false;};
String voiceActionMessage(bool ru,const char* russian,const char* english){return ru?String(russian):String(english);}
}
bool applyContextRecordCommand(const String& question,String& answer,std::vector<int>& sources){
  if(!pikaVoiceMutationContext())return false;
  if(!voiceRecordCommandCandidate(question.c_str()))return false;
  sources.clear();const bool ru=pikaMostlyCyrillic(question.c_str());
  if(!pikaVoiceRecordActions()){
    answer=voiceActionMessage(ru,"Голосовые изменения выключены. Включи Allow voice changes to records в Config.","Voice changes are disabled. Enable Allow voice changes to records in Config.");return true;
  }
  if(voiceRecordCommandBlocked(question.c_str())){
    answer=voiceActionMessage(ru,"Ничего не изменено.","Nothing was changed.");return true;
  }
  const String requestedKind=voiceRecordRequestedKind(question.c_str());const bool restore=voiceRecordRestoreRequested(question.c_str());
  if(!requestedKind.length())return false;
  std::vector<VoiceRecordCandidate> candidates;JsonDocument context;JsonArray records=context["records"].to<JsonArray>();
  context["command"]=question;context["requiredKind"]=requestedKind;context["requiredOperation"]=restore?"restore":"archive";
  const String today=utcToLocalDeviceLabel(currentUtcIso()).substring(0,10);
  auto add=[&](const VoiceRecordCandidate& candidate,const String& date,const String& tags){
    candidates.push_back(candidate);JsonObject record=records.add<JsonObject>();record["id"]=candidate.id;record["kind"]=candidate.kind;record["title"]=candidate.title;record["body"]=candidate.text;record["tags"]=tags;record["date"]=date;record["archived"]=candidate.archived;record["manualArchived"]=candidate.manualArchived;
  };
  auto tooLarge=[&](){return records.size()>80||measureJson(context)>64000;};
  if(requestedKind=="memories")for(const auto& note:noteIndex){
    const bool archived=managerArchived(note.num);if(archived!=restore)continue;
    const BrainMemory brain=loadBrainMemory(note.num);VoiceRecordCandidate item;item.id="m"+String(note.num);item.kind="memories";item.note=note.num;item.title=brain.title.length()?brain.title:String("Memory #")+String(note.num);item.text=managerReadText(note.num,64001);item.archived=archived;
    String tags=String(note.tag);for(const auto& topic:brain.topics)tags+=" "+topic;
    // Include tags in verified evidence as well as in the model's record.
    item.text+="\n"+tags;add(item,noteCreatedUtc(note.num),tags);if(tooLarge())break;
  }
  else for(const auto& task:loadBrainTasks()){
    const bool event=task.type=="event";if((event?String("events"):String("tasks"))!=requestedKind)continue;
    const String last=utcToLocalDeviceLabel(task.endUtc.length()?task.endUtc:task.dueUtc).substring(0,10);
    const bool archived=event?(task.archived||(last.length()==10&&today.length()==10&&last<today)):task.done;
    if(restore?(event?!task.archived:!task.done):archived)continue;
    VoiceRecordCandidate item;item.id=task.id;item.kind=event?"events":"tasks";item.title=task.text;item.text=task.text;item.note=task.noteNum;item.archived=archived;item.manualArchived=task.archived;
    add(item,task.dueUtc,"");if(tooLarge())break;
  }
  if(tooLarge()){
    answer=voiceActionMessage(ru,"Слишком много текста для надёжного выбора. Используй редактирование записи в веб-морде. Ничего не изменено.","There is too much text to select a record reliably. Use the web editor. Nothing was changed.");return true;
  }
  if(candidates.empty()){
    answer=voiceActionMessage(ru,"Подходящих записей в нужном состоянии нет. Ничего не изменено.","There are no matching records in the required state. Nothing was changed.");return true;
  }
  JsonDocument request;request["model"]="gpt-4.1-mini";request["store"]=false;request["max_output_tokens"]=650;request["instructions"]=kVoiceRecordPrompt;
  String input;serializeJson(context,input);request["input"]=input;
  JsonObject format=request["text"]["format"].to<JsonObject>();format["type"]="json_schema";format["name"]="pika_record_action";format["strict"]=true;JsonDocument schema;deserializeJson(schema,kVoiceRecordSchema);format["schema"]=schema;
  String payload,response;serializeJson(request,payload);
  if(!openAiJsonPost("/v1/responses",payload,response)){
    answer=voiceActionMessage(ru,"Не удалось проверить команду. Ничего не изменено.","Could not check the command. Nothing was changed.");return true;
  }
  JsonDocument decision;if(deserializeJson(decision,responseOutputText(response))){answer=voiceActionMessage(ru,"Не удалось разобрать ответ. Ничего не изменено.","Could not read the result. Nothing was changed.");return true;}
  const String intent=decision["intent"]|"none";const auto ids=decision["ids"].as<JsonArrayConst>();
  if(intent=="ambiguous"||ids.size()>1){
    answer=voiceActionMessage(ru,"Нашлось несколько записей. Уточни название или дату. Ничего не изменено.","Several records match. Specify the title or date. Nothing was changed.");
    int listed=0;for(auto idValue:ids){String id=idValue.as<String>();for(const auto& candidate:candidates)if(candidate.id==id&&listed<3){answer+="\n- "+candidate.title.substring(0,pikaUtf8Prefix(candidate.title.c_str(),140));listed++;}}
    return true;
  }
  if(intent!="change"||ids.size()!=1){answer=voiceActionMessage(ru,"Подходящая запись не найдена или команда неоднозначна. Ничего не изменено.","No matching record or no clear command. Nothing was changed.");return true;}
  const String kind=decision["kind"]|"",id=ids[0].as<String>(),evidence=decision["evidence"]|"";
  auto selected=std::find_if(candidates.begin(),candidates.end(),[&](const VoiceRecordCandidate& item){return item.id==id&&item.kind==kind&&kind==requestedKind;});
  const float confidence=decision["confidence"]|0.0f;
  if(selected==candidates.end()||!std::isfinite(confidence)||confidence<0.9f||!voiceRecordEvidenceValid((selected->title+"\n"+selected->text).c_str(),evidence.c_str())){
    answer=voiceActionMessage(ru,"Не могу однозначно выбрать запись. Уточни название. Ничего не изменено.","I cannot identify the record confidently. Specify its title. Nothing was changed.");return true;
  }
  JsonDocument mutation;mutation["kind"]=kind;mutation["id"]=id;mutation["revision"]=managerRevision();
  if(kind=="memories")mutation["action"]=restore?"remember":"delete";
  else if(kind=="events"){mutation["action"]="archive";mutation["record"]["archived"]=!restore;}
  else{mutation["action"]="toggle";mutation["record"]["done"]=!restore;}
  String storedId,error;
  if(!managerMutation(mutation.as<JsonVariantConst>(),storedId,error)){
    answer=voiceActionMessage(ru,"Не удалось завершить операцию. Проверь состояние записи в веб-морде.","The operation could not finish. Check the record in the web interface.");return true;
  }
  invalidatePortalCache();
  if(selected->note>0)sources.push_back(selected->note);
  if(kind=="memories")answer=restore?voiceActionMessage(ru,"Заметка восстановлена без аудиозаписи: ","Memory restored without audio: "):voiceActionMessage(ru,"Заметка в архиве, аудиозапись удалена: ","Memory archived, audio removed: ");
  else if(kind=="tasks")answer=restore?voiceActionMessage(ru,"Задача снова открыта: ","Task reopened: "):voiceActionMessage(ru,"Задача выполнена: ","Task completed: ");
  else answer=restore?voiceActionMessage(ru,"Событие восстановлено: ","Event restored: "):voiceActionMessage(ru,"Событие отправлено в архив: ","Event archived: ");
  answer+=selected->title;if(kind=="events"&&restore&&selected->archived&&selected->manualArchived){const auto all=loadBrainTasks();for(const auto& task:all)if(task.id==id){String last=utcToLocalDeviceLabel(task.endUtc.length()?task.endUtc:task.dueUtc).substring(0,10);if(last.length()==10&&last<today)answer+=voiceActionMessage(ru," Дата уже прошла, поэтому оно остаётся в истории календаря и архиве."," Its date is past, so it remains in calendar history and Archive.");}}
  return true;
}
