#include "phone_sync.h"
#include "home_buttons.h"
#include "manager_store.h"
#include "tasks.h"

#include "brain.h"
#include "notes.h"
#include "storage_commit.h"
#include "settings.h"
#include "task_command_safety.h"
#include "../../config.h"
#include "../../globals.h"
#include <SD_MMC.h>
#include <algorithm>

namespace {
constexpr const char* kTaskStateFile = NOTES_DIR "/tasks.state";

bool statesReadComplete=true;
struct TaskState { String id; bool done; String updatedUtc; String normalizedText; };

std::vector<TaskState> loadStates() {
  statesReadComplete=true;std::vector<TaskState> states;
  // Recover the last committed state if power failed between FAT renames.
  const String backup = String(kTaskStateFile) + ".bak";
  if (!SD_MMC.exists(kTaskStateFile) && SD_MMC.exists(backup.c_str()))
    SD_MMC.rename(backup.c_str(), kTaskStateFile);
  File file = SD_MMC.open(kTaskStateFile);
  if(!file){statesReadComplete=!SD_MMC.exists(kTaskStateFile);return states;}
  while (file.available()) {
    String line = file.readStringUntil('\n'); line.trim();
    int first = line.indexOf('|');
    int second = line.indexOf('|', first + 1);
    if(first<=0||second<=first){if(line.length())statesReadComplete=false;continue;}
    int third = line.indexOf('|', second + 1);
    states.push_back({line.substring(0, first), line.substring(first + 1, second) == "1",
                      line.substring(second + 1, third >= 0 ? third : line.length()),
                      third >= 0 ? line.substring(third + 1) : ""});
  }
  file.close();
  return states;
}

const TaskState* findState(const std::vector<TaskState>& states, const String& id) {
  auto found = std::lower_bound(states.begin(), states.end(), id,
    [](const TaskState& state, const String& value) { return state.id < value; });
  return found != states.end() && found->id == id ? &*found : nullptr;
}

String stableTaskId(int noteNum, const String& text) {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < text.length(); i++) { hash ^= (uint8_t)text[i]; hash *= 16777619UL; }
  char id[24];
  snprintf(id, sizeof(id), "%d-%08lx", noteNum, (unsigned long)hash);
  return String(id);
}

String normalizedWords(String text) {
  text.toLowerCase();
  text.replace("ä", "ae"); text.replace("ö", "oe"); text.replace("ü", "ue"); text.replace("ß", "ss");
  for (size_t i = 0; i < text.length(); i++) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c < 128 && !isalnum(c)) text.setCharAt(i, ' ');
  }
  while (text.indexOf("  ") >= 0) text.replace("  ", " ");
  text.trim();
  return text;
}

bool ignoredCompletionWord(const String& word) {
  static const char* ignored[] = {
    "abgeschlossen", "abschliessen", "erledige", "erledigt", "fertig", "fertiggemacht",
    "gemacht", "geschafft", "beendet", "hake", "abhaken", "markiere", "complete", "completed",
    "done", "task", "aufgabe", "als", "ich", "habe", "hatte", "ist", "jetzt", "die", "den",
    "das", "der", "ein", "eine", "einen", "mit", "von", "und", "bitte", "mein", "meine",
    "offen", "offene", "offenen", "offener", "offenes", "open"
  };
  for (const char* item : ignored) if (word == item) return true;
  return false;
}

std::vector<String> significantCompletionWords(const String& speech) {
  String text = normalizedWords(speech);
  std::vector<String> words;
  int start = 0;
  while (start < static_cast<int>(text.length())) {
    while (start < static_cast<int>(text.length()) && text[start] == ' ') start++;
    int end = text.indexOf(' ', start); if (end < 0) end = text.length();
    String word = text.substring(start, end);
    if (word.length() >= 3 && !ignoredCompletionWord(word) &&
        std::find(words.begin(), words.end(), word) == words.end()) words.push_back(word);
    start = end + 1;
  }
  return words;
}
}

namespace {bool taskReadCacheReady=false,taskReadYield=false;std::vector<BrainTaskRecord> taskReadCache;}
void invalidateBrainTaskCache(){taskReadCacheReady=false;}
bool brainTaskReadComplete(){return taskReadCacheReady;}
std::vector<BrainTaskRecord> brainTaskSourcesForNote(int noteNum){std::vector<BrainTaskRecord> tasks;
    BrainMemory memory = loadBrainMemory(noteNum);
    for (size_t index = 0; index < memory.tasks.size(); index++) {
      BrainTaskRecord task;
      task.id = index < memory.taskIds.size() && memory.taskIds[index].length()
                  ? memory.taskIds[index]
                  : stableTaskId(noteNum, memory.tasks[index]);
      task.noteNum = noteNum;
      task.text = memory.tasks[index];
      task.createdUtc = memory.createdUtc.length() ? memory.createdUtc : noteCreatedUtc(noteNum);
      if (index < memory.taskDueUtc.size()) task.dueUtc = memory.taskDueUtc[index];
      if (index < memory.taskEndUtc.size()) task.endUtc = memory.taskEndUtc[index];
      if (index < memory.taskPriority.size()) task.priority = memory.taskPriority[index];
      if (index < memory.taskTypes.size()) task.type = memory.taskTypes[index];
      if (index < memory.taskDatePrecision.size()) task.datePrecision = memory.taskDatePrecision[index];
      tasks.push_back(task);
    }
return tasks;}
bool brainApplyTaskOverlays(std::vector<BrainTaskRecord>& tasks){auto states=loadStates();if(!statesReadComplete)return false;std::sort(states.begin(),states.end(),[](const TaskState&a,const TaskState&b){return a.id<b.id;});for(auto& task:tasks){const TaskState* state=findState(states,task.id);if(state){task.done=state->done;task.updatedUtc=state->updatedUtc;}}return managerMergeTasks(tasks);}
void brainPublishTaskCache(const std::vector<BrainTaskRecord>& tasks){taskReadCache=tasks;taskReadCacheReady=true;}
void brainPublishTaskCache(std::vector<BrainTaskRecord>&& tasks){taskReadCache=std::move(tasks);taskReadCacheReady=true;}
const std::vector<BrainTaskRecord>& brainTaskSnapshot(){if(!taskReadCacheReady){auto loaded=loadBrainTasks();}return taskReadCache;}
std::vector<BrainTaskRecord> loadBrainTasks() {
  if(taskReadCacheReady)return taskReadCache;
  std::vector<BrainTaskRecord> tasks;
  for(const NoteEntry& note:noteIndex){
#if defined(ARDUINO)
   if(taskReadYield&&pikaHomeButtonsBusy())return taskReadCache;
#endif
   auto source=brainTaskSourcesForNote(note.num);tasks.insert(tasks.end(),source.begin(),source.end());
  }
  // A failed overlay read must never revive archived source tasks or hide manual tasks.
  // Retain the last complete view for local UI; APIs reject this stale view.
  if(!brainApplyTaskOverlays(tasks))return taskReadCache;
  taskReadCache=tasks;taskReadCacheReady=true;
  return tasks;
}

std::vector<BrainTaskRecord> loadBrainTasksCooperative(){const bool old=taskReadYield;taskReadYield=true;auto out=loadBrainTasks();taskReadYield=old;return out;}

std::vector<BrainTaskRecord> priorityBrainTasks(size_t limit, int* openCount) {
  std::vector<BrainTaskRecord> tasks = loadBrainTasks();
  tasks.erase(std::remove_if(tasks.begin(), tasks.end(),
    [](const BrainTaskRecord& task) { return task.done || task.type == "event"; }), tasks.end());
  if (openCount) *openCount = static_cast<int>(tasks.size());
  std::sort(tasks.begin(), tasks.end(), [](const BrainTaskRecord& a, const BrainTaskRecord& b) {
    if (a.priority != b.priority) return a.priority > b.priority;
    // Explicit due dates always win. ISO-8601 strings sort chronologically.
    if ((a.dueUtc.length() > 0) != (b.dueUtc.length() > 0)) return a.dueUtc.length() > 0;
    if (a.dueUtc.length() && a.dueUtc != b.dueUtc) return a.dueUtc < b.dueUtc;
    // Undated open loops: show the most recently captured first.
    if (a.createdUtc != b.createdUtc) return a.createdUtc > b.createdUtc;
    return a.noteNum > b.noteNum;
  });
  if (tasks.size() > limit) tasks.resize(limit);
  return tasks;
}

bool setBrainTaskDone(const String& id, bool done) {
  const int managed = managerSetTaskDone(id, done);
  if (managed != 0) return managed > 0;
  if (id.length() == 0 || id.length() > 32 || id.indexOf('|') >= 0 ||
      id.indexOf('\n') >= 0 || id.indexOf('\r') >= 0) return false;
  std::vector<TaskState> states = loadStates();
  String normalizedText;
  for (const NoteEntry& note : noteIndex) {
    BrainMemory memory = loadBrainMemory(note.num);
    for (size_t index = 0; index < memory.tasks.size(); index++) {
      const String taskId = index < memory.taskIds.size() && memory.taskIds[index].length()
                              ? memory.taskIds[index]
                              : stableTaskId(note.num, memory.tasks[index]);
      if (taskId == id) { normalizedText = normalizedWords(memory.tasks[index]); break; }
    }
    if (normalizedText.length()) break;
  }
  if (!normalizedText.length()) return false; // Only mutate a real, current task.
  bool found = false;
  for (TaskState& state : states) {
    if (state.id == id) {
      state.done = done; state.updatedUtc = currentUtcIso();
      if (normalizedText.length()) state.normalizedText = normalizedText;
      found = true; break;
    }
  }
  if (!found) states.push_back({id, done, currentUtcIso(), normalizedText});
  const String temp = String(kTaskStateFile) + ".tmp";
  SD_MMC.remove(temp.c_str());
  File file = SD_MMC.open(temp.c_str(), FILE_WRITE);
  if (!file) return false;
  size_t expected = 0;
  bool complete = true;
  for (const TaskState& state : states) {
    const String line = state.id + "|" + (state.done ? "1" : "0") + "|" + state.updatedUtc + "|" + state.normalizedText + "\n";
    expected += line.length();
    complete &= file.print(line) == line.length();
  }
  file.flush();
  complete &= file.size() == expected;
  file.close();
  invalidateBrainTaskCache();
  return complete && commitStoredFile(SD_MMC, String(kTaskStateFile), temp);
}

int openBrainTaskCount() {
  int count = 0;
  for (const BrainTaskRecord& task : loadBrainTasks()) if (!task.done && task.type != "event") count++;
  return count;
}

void clearBrainTaskState() {
  if(!pikaPhoneFileWillChange(kTaskStateFile))return;
  pikaPhoneFileTouched(kTaskStateFile);
  SD_MMC.remove(kTaskStateFile);
  SD_MMC.remove((String(kTaskStateFile) + ".bak").c_str());
  SD_MMC.remove((String(kTaskStateFile) + ".tmp").c_str());
}

bool applyBrainTaskCommand(const String& speech, String& response, int& sourceNote) {
  String query=normalizedWords(speech);
  if ((pikaUiLanguage() != "de" && pikaUiLanguage() != "en") ||
      unsafeLocalCompletion(query.c_str()) || speech.indexOf('?') >= 0) return false;
  const bool complete=query.startsWith("erledige ") || query.startsWith("hake ") ||
                      query.startsWith("markiere ") || query.startsWith("complete task ") ||
                      query.indexOf(" erledigt") >= 0 || query.endsWith("erledigt") ||
                      query.indexOf(" abgeschlossen") >= 0 || query.endsWith("abgeschlossen") ||
                      query.indexOf(" fertiggemacht") >= 0 || query.endsWith("fertiggemacht") ||
                      query.indexOf(" fertig gemacht") >= 0 || query.indexOf(" bin fertig") >= 0 ||
                      query.indexOf(" ist fertig") >= 0 ||
                      query.indexOf(" completed") >= 0 || query.endsWith("done");
  if(!complete) return false;
  const bool german = pikaUiLanguage() == "de";
  std::vector<BrainTaskRecord> openTasks;
  for (const BrainTaskRecord& task : loadBrainTasks())
    if (!task.done && task.type != "event") openTasks.push_back(task);
  if (openTasks.empty()) {
    response = german ? "Du hast keine offene Aufgabe, die ich abhaken kann."
                      : "You have no open task I can complete.";
    return true;
  }

  const bool allIntent = query.indexOf("alle aufgaben") >= 0 ||
                         query.indexOf("all tasks") >= 0 ||
                         query.indexOf("alles fuer heute") >= 0;
  const bool todayIntent = query.indexOf("heute") >= 0 || query.indexOf("today") >= 0;
  if (allIntent) {
    const String localNow = utcToLocalDeviceLabel(currentUtcIso());
    const String today = localNow.length() >= 10 && localNow != "time not set"
                           ? localNow.substring(0, 10) : "";
    std::vector<BrainTaskRecord> batch;
    for (const BrainTaskRecord& task : openTasks) {
      if (task.type == "event") continue;
      if (!todayIntent) { batch.push_back(task); continue; }
      if (!task.dueUtc.length()) { batch.push_back(task); continue; }
      if (!today.length() || !task.dueUtc.length()) continue;
      const String localDue = utcToLocalDeviceLabel(task.dueUtc);
      if (localDue.length() >= 10 && localDue != "time not set" &&
          localDue.substring(0, 10) <= today) batch.push_back(task);
    }
    if (batch.empty()) {
      response = german ? (todayIntent ? "Heute sind keine offenen datierten Aufgaben mehr übrig."
                                       : "Du hast keine offenen Aufgaben mehr.")
                        : (todayIntent ? "There are no open dated tasks left for today."
                                       : "You have no open tasks left.");
      return true;
    }
    bool stored = true;
    for (const BrainTaskRecord& task : batch) stored = setBrainTaskDone(task.id, true) && stored;
    if (!stored) {
      response = german ? "Ich konnte nicht alle Aufgaben aktualisieren."
                        : "I could not update every task.";
      return true;
    }
    sourceNote = batch[0].noteNum;
    response = german ? String(batch.size()) + " Aufgaben erledigt."
                      : String(batch.size()) + " tasks completed.";
    return true;
  }

  const std::vector<String> words = significantCompletionWords(query);
  if (words.empty()) {
    if (openTasks.size() != 1) {
      response = german ? "Ich habe mehrere offene Aufgaben. Sag mir bitte, welche du meinst."
                        : "You have several open tasks. Tell me which one you mean.";
      return true;
    }
    if (!setBrainTaskDone(openTasks[0].id, true)) {
      response = german ? "Ich konnte die Aufgabe nicht aktualisieren."
                        : "I could not update that task.";
      return true;
    }
    sourceNote = openTasks[0].noteNum;
    response = (german ? "Erledigt: " : "Done: ") + openTasks[0].text;
    return true;
  }
  int bestScore=0;
  std::vector<BrainTaskRecord> matches;
  for(const BrainTaskRecord& task:openTasks) {
    String text=normalizedWords(task.text);
    int score=0;
    for (const String& word : words) if(text.indexOf(word)>=0) score++;
    if(score>bestScore){bestScore=score;matches.clear();matches.push_back(task);}
    else if(score==bestScore && score>0) matches.push_back(task);
  }
  if(bestScore==0 || matches.empty()) {
    response = german ? "Ich finde keine passende offene Aufgabe."
                      : "I cannot find a matching open task.";
    return true;
  }

  // Old firmware could create exact duplicate tasks from completion notes.
  // Complete those together, but never guess between genuinely different tasks.
  const String canonical = normalizedWords(matches[0].text);
  for (size_t i = 1; i < matches.size(); i++) {
    if (normalizedWords(matches[i].text) != canonical) {
      response = german ? "Mehrere Aufgaben passen dazu. Sag den genaueren Titel."
                        : "Several tasks match. Say the more specific title.";
      return true;
    }
  }
  bool stored = true;
  for (const BrainTaskRecord& match : matches) stored = setBrainTaskDone(match.id, true) && stored;
  if (!stored) {
    response = german ? "Ich konnte die Aufgabe nicht aktualisieren."
                      : "I could not update that task.";
    return true;
  }
  sourceNote=matches[0].noteNum;
  response=(german ? "Erledigt: " : "Done: ")+matches[0].text;
  return true;
}
