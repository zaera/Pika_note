#include "phone_sync.h"
namespace { void rememberHomePixels(); }
#include "device_owner.h"
#include "error_log.h"
#include "device_extras.h"
#include <esp_attr.h>
#include <esp_sleep.h>
#include <Fonts/FreeSans12pt7b.h>
#include "mail.h"
#include "phone_link.h"
#include "manager_store.h"
#include "weather.h"
#include "utf8_text.h"
#include "ui.h"
#include "clock_font.h"
#include "../../config.h"
#include "../../globals.h"
#include "draw.h"
#include "notes.h"
#include "tasks.h"
#include "task_visibility.h"
#include "settings.h"

namespace {
constexpr int kInset = 14;
constexpr int kRight = W - kInset;
constexpr int kContentWidth = W - 2 * kInset;
constexpr int kHeaderY = 10;
constexpr int kHeaderRuleY = 31;
constexpr int kContentTop = 43;
constexpr int kFooterRuleY = 174;
constexpr int kFooterTextY = 181;
constexpr int kAnswerLinesPerPage = 6;

struct UiTranslation { const char* en; const char* fr; const char* es; const char* it; const char* pl; const char* cs; const char* ru; const char* uk; const char* nl; const char* pt; };
const UiTranslation kUiTranslations[] = {
  {"TODAY","AUJOURD'HUI","HOY","OGGI","DZISIAJ","DNES","SEGODNYA","SOHODNI","VANDAAG","HOJE"},
  {"TASKS","TACHES","TAREAS","ATTIVITA","ZADANIA","UKOLY","ZADACHI","ZAVDANNYA","TAKEN","TAREFAS"},
  {"EVENTS","RENDEZ-VOUS","EVENTOS","EVENTI","TERMINY","TERMINY","SOBYTIYA","PODIYI","AFSPRAKEN","EVENTOS"},
  {"NEXT","SUIVANT","SIGUIENTE","AVANTI","DALEJ","DALSI","DALEE","DALI","VOLGENDE","SEGUINTE"},
  {"BACK","RETOUR","ATRAS","INDIETRO","WSTECZ","ZPET","NAZAD","NAZAD","TERUG","VOLTAR"},
  {"NOTE","NOTE","NOTA","NOTA","NOTATKA","POZNAMKA","ZAPISKA","NOTATKA","NOTITIE","NOTA"},
  {"ASK","QUESTION","PREGUNTA","DOMANDA","PYTANIE","DOTAZ","VOPROS","PYTANNYA","VRAAG","PERGUNTA"},
  {"LISTENING","ECOUTE","ESCUCHANDO","ASCOLTO","SLUCHAM","POSLOUCHAM","SLUSHAYU","SLUKHAYU","LUISTEREN","OUVINDO"},
  {"TRANSCRIBING","TRANSCRIPTION","TRANSCRIBIENDO","TRASCRIZIONE","TRANSKRYPCJA","PREPISUJI","RASSHIFROVKA","TRANSKRYBUYU","TRANSCRIBEREN","TRANSCREVENDO"},
  {"RECORDING","ENREGISTRE","GRABANDO","REGISTRA","NAGRYWANIE","NAHRAVANI","ZAPIS","ZAPYS","OPNAME","GRAVANDO"},
  {"SAVED","GARDE","GUARDADO","SALVATO","ZAPISANO","ULOZENO","SOHRANENO","ZBEREZHENO","OPGESLAGEN","GUARDADO"},
  {"ANSWER","REPONSE","RESPUESTA","RISPOSTA","ODPOWIEDZ","ODPOVED","OTVET","VIDPOVID","ANTWOORD","RESPOSTA"},
  {"FROM MEMORY","DE LA MEMOIRE","DE MEMORIA","DA MEMORIA","Z PAMIECI","Z PAMETI","IZ PAMYATI","Z PAMYATI","UIT GEHEUGEN","DA MEMORIA"},
  {"END","FIN","FIN","FINE","KONIEC","KONEC","KONEC","KINETS","EINDE","FIM"},
  {"READY","PRET","LISTO","PRONTO","GOTOWE","PRIPRAVENO","GOTOVO","HOTOVO","KLAAR","PRONTO"},
  {"CONNECTING","CONNEXION","CONECTANDO","CONNESSIONE","LACZENIE","PRIPOJOVANI","PODKLYUCHENIE","PIDKLYUCHENNYA","VERBINDEN","CONECTANDO"},
  {"ORGANIZING","CLASSEMENT","ORGANIZANDO","ORGANIZZO","PORZADKUJE","TRIDIM","SORTIRUYU","SORTUYU","ORDENEN","ORGANIZANDO"},
  {"REMEMBERING","MEMORISE","RECORDANDO","RICORDO","ZAPAMIETUJE","PAMATUJI","ZAPOMINAYU","ZAPAMYATOVUYU","ONTHOUDEN","MEMORIZANDO"},
  {"THINKING","REFLEXION","PENSANDO","PENSO","MYSLE","PREMYSLIM","DUMAYU","DUMAYU","DENKEN","PENSANDO"},
  {"SEARCHING","RECHERCHE","BUSCANDO","CERCO","SZUKAM","HLEDAM","ISHCHU","SHUKAYU","ZOEKEN","PROCURANDO"},
  {"ALMOST THERE","PRESQUE FINI","CASI LISTO","QUASI FATTO","PRAWIE GOTOWE","TEMER HOTOVO","POCHTI GOTOVO","MAYZHE HOTOVO","BIJNA KLAAR","QUASE PRONTO"},
  {"TRY AGAIN","REESSAYER","REINTENTAR","RIPROVA","SPROBUJ","ZNOVU","POVTORIT","SPROBUVATY","OPNIEUW","TENTAR NOVAMENTE"}
  ,{"ONE MOMENT","UN MOMENT","UN MOMENTO","UN MOMENTO","CHWILA","MOMENT","MOMENT","KHVYLYNU","EVEN WACHTEN","UM MOMENTO"}
  ,{"SAVED FOR LATER","GARDE POUR PLUS TARD","GUARDADO PARA DESPUES","SALVATO PER DOPO","ZAPISANE NA POZNIEJ","ULOZENO NA POZDEJI","SOHRANENO NA POTOM","ZBEREZHENO NA POTIM","BEWAARD VOOR LATER","GUARDADO PARA DEPOIS"}
  ,{"RECALLING","RAPPEL","RECORDANDO","RICHIAMO","PRZYPOMINAM","VYBAVUJI","VSPOMINAYU","ZGHADUYU","OPHALEN","RELEMBRANDO"}
  ,{"NO ANSWER","PAS DE REPONSE","SIN RESPUESTA","NESSUNA RISPOSTA","BRAK ODPOWIEDZI","BEZ ODPOVEDI","NET OTVETA","NEMA VIDPOVIDI","GEEN ANTWOORD","SEM RESPOSTA"}
  ,{"NO","AUCUN","NINGUNO","NESSUNO","BRAK","ZADNE","NET","NEMA","GEEN","SEM"}
  ,{"ALL","TOUT","TODO","TUTTO","WSZYSTKO","VSE","VSE","USE","ALLES","TUDO"}
  ,{"DONE","FAIT","HECHO","FATTO","GOTOWE","HOTOVO","GOTOVO","HOTOVO","KLAAR","FEITO"}
  ,{"AGAIN: BACK","ENCORE: RETOUR","OTRA VEZ: ATRAS","ANCORA: INDIETRO","PONOWNIE: WSTECZ","ZNOVU: ZPET","SNOVA: NAZAD","ZNOVU: NAZAD","NOGMAALS: TERUG","NOVAMENTE: VOLTAR"}
  ,{"NOT READY YET","PAS ENCORE PRET","AUN NO LISTO","NON ANCORA PRONTO","JESZCZE NIE GOTOWE","JESTE NEPRIPRAVENO","ESHCHO NE GOTOVO","SHCHE NE HOTOVO","NOG NIET KLAAR","AINDA NAO PRONTO"}
  ,{"SET UP","CONFIGURER","CONFIGURAR","CONFIGURA","KONFIGURUJ","NASTAVIT","NASTROIT","NALASHTUVATY","INSTELLEN","CONFIGURAR"}
  ,{"IN PIKA FLASHER","DANS PIKA FLASHER","EN PIKA FLASHER","IN PIKA FLASHER","W PIKA FLASHER","V PIKA FLASHER","V PIKA FLASHER","U PIKA FLASHER","IN PIKA FLASHER","NO PIKA FLASHER"}
  ,{"SOMETHING WENT WRONG","UNE ERREUR","ALGO SALIO MAL","QUALCOSA NON VA","COS POSZLO NIE TAK","NECO SE NEPOVEDLO","CHTO-TO NE TAK","SHCHOS NE TAK","ER GING IETS MIS","ALGO CORREU MAL"}
  ,{"MEMORIES","SOUVENIRS","RECUERDOS","RICORDI","WSPOMNIENIA","VZPOMINKY","VOSPOMINANIYA","SPHADY","HERINNERINGEN","MEMORIAS"}
};

const char* translatedUi(const char* english) {
  const String language = pikaUiLanguage();
  for (const auto& row : kUiTranslations) {
    if (strcmp(row.en, english)) continue;
    if (language == "fr") return row.fr; if (language == "es") return row.es;
    if (language == "it") return row.it; if (language == "pl") return row.pl;
    if (language == "cs") return row.cs; if (language == "ru") return row.ru;
    if (language == "uk") return row.uk; if (language == "nl") return row.nl;
    if (language == "pt") return row.pt; return english;
  }
  return english;
}

const char* uiText(const char* german, const char* english) {
  const String language = pikaUiLanguage();
  if (language == "de") return german;
  return translatedUi(english);
}

void labelLeft(int y, const char* text, int scale, uint8_t color, int maxWidth = kContentWidth) {
  if (!text || !strlen(text)) return;
  while (scale > 1 && textW(text, scale) > maxWidth) scale--;
  drawStrFit(kInset, y, maxWidth, text, scale, color);
}

void labelRight(int y, const char* text, int scale, uint8_t color, int maxWidth = kContentWidth) {
  if (!text || !strlen(text)) return;
  while (scale > 1 && textW(text, scale) > maxWidth) scale--;
  const int width = min(textW(text, scale), maxWidth);
  drawStrFit(kRight - width, y, width, text, scale, color);
}

void drawHeader(const char* right, uint8_t color) {
  labelLeft(kHeaderY, "PIKA", 1, color, 76);
  labelRight(kHeaderY, right, 1, color, 88);
  hline(kInset, kHeaderRuleY, kContentWidth, color);
}

void drawFooter(const char* left, const char* right, uint8_t color) {
  hline(kInset, kFooterRuleY, kContentWidth, color);
  // Footer copy is interaction guidance, not user content. Never run it
  // through drawStrFit(), because an instruction such as ZURUECK must not turn
  // into an ambiguous ZUR... . All footer labels are deliberately authored to
  // fit at scale 1 and are drawn verbatim.
  if (left && strlen(left)) drawStr(kInset, kFooterTextY, left, 1, color);
  if (right && strlen(right))
    drawStr(kRight - textW(right, 1), kFooterTextY, right, 1, color);
}

void drawFooterCentered(const char* text, uint8_t color) {
  hline(kInset, kFooterRuleY, kContentWidth, color);
  if (text && strlen(text)) drawStrC(W / 2, kFooterTextY, text, 1, color);
}

void drawBattery(int percent) {
  if (percent < 0) return;
  percent = constrain(percent, 0, 100);
  if(!pikaPhoneOnline()){const int cx=kRight-39,cy=15;for(int a=0;a<360;a+=15){const float r=a*0.0174532925f;fillRect(cx+int(5*cosf(r)),cy+int(5*sinf(r)),1,1,BLACK);}for(int i=-4;i<=4;i++)fillRect(cx+i,cy-i,1,1,BLACK);}
  constexpr int width = 23, height = 12;
  const int x = kRight - width - 2, y = 9;
  strokeRect(x, y, width, height, 1, BLACK);
  fillRect(x + width, y + 3, 2, 6, BLACK);
  const int bars = percent > 66 ? 3 : percent > 33 ? 2 : percent > 10 ? 1 : 0;
  for (int i = 0; i < bars; i++) fillRect(x + 3 + i * 6, y + 3, 5, 6, BLACK);
}

void drawHero(const char* text, int y, uint8_t color, int maxBottom = 122) {
  if (!text || !strlen(text)) return;
  for (int scale = 3; scale >= 1; scale--) {
    if (textW(text, scale) <= kContentWidth && y + uiFontHeight(scale) <= maxBottom) {
      labelLeft(y, text, scale, color);
      return;
    }
  }
  labelLeft(y, text, 1, color);
}

void drawTwoLineHero(const char* first, const char* second, int y, uint8_t color) {
  int scale = 3;
  while (scale > 1 && (textW(first, scale) > kContentWidth || textW(second, scale) > kContentWidth)) scale--;
  const int lineHeight = uiFontHeight(scale) + 4;
  labelLeft(y, first, scale, color);
  labelLeft(y + lineHeight, second, scale, color);
}

void drawStatusHero(const char* text, int y, uint8_t color) {
  if (!text || !*text) return;
  // Do not use labelLeft here: it silently falls back to regular-weight text.
  if (textW(text, 2) <= kContentWidth) {
    drawStr(kInset, y, text, 2, color);
    return;
  }
  // Never fall back to the thin body font for a long status. Split it into
  // two bold lines instead (for example SPAETER / WEITER).
  String value = text;
  int split = value.lastIndexOf(' ', value.length() / 2 + 1);
  if (split <= 0) split = value.indexOf(' ', value.length() / 2);
  if (split > 0) {
    const String first = value.substring(0, split);
    const String second = value.substring(split + 1);
    if (textW(first.c_str(), 2) <= kContentWidth && textW(second.c_str(), 2) <= kContentWidth) {
      drawStr(kInset, y - 7, first.c_str(), 2, color);
      drawStr(kInset, y + 19, second.c_str(), 2, color);
      return;
    }
  }
  // Long translations (including single words) wrap in a compact bold font.
  // Device status translations are ASCII/transliterated, unlike user content.
  String remaining = text;
  int lineY = y;
  while (remaining.length()) {
    unsigned int count = remaining.length();
    while (count > 1 && textW(remaining.substring(0, count).c_str(), -1) > kContentWidth) --count;
    if (count < remaining.length()) {
      const int space = remaining.lastIndexOf(' ', count);
      if (space > 0) count = space;
    }
    drawStr(kInset, lineY, remaining.substring(0, count).c_str(), -1, color);
    remaining = remaining.substring(count);
    remaining.trim();
    lineY += 18;
  }
}

void drawProgressSegments(int current, int total, int y, uint8_t color) {
  total = constrain(total, 1, 5);
  current = constrain(current, 0, total);
  constexpr int gap = 6;
  const int width = (kContentWidth - (total - 1) * gap) / total;
  for (int i = 0; i < total; i++) {
    const int x = kInset + i * (width + gap);
    if (i < current) fillRect(x, y, width, 6, color);
    else strokeRect(x, y, width, 6, 1, color);
  }
}

void drawTimer(uint32_t elapsedMs, uint8_t color) {
  const uint32_t seconds = elapsedMs / 1000;
  char timer[12];
  snprintf(timer, sizeof(timer), "%02lu:%02lu",
           static_cast<unsigned long>(seconds / 60),
           static_cast<unsigned long>(seconds % 60));
  drawBigStrC(100, 113, 39, timer, color);
}

int memoryStage(const char* phase) {
  if (!phase) return 0;
  if (!strcmp(phase, "CONNECTING")) return 1;
  if (!strcmp(phase, "DECODING")) return 2;
  if (!strcmp(phase, "UNDERSTANDING") || !strcmp(phase, "ORGANIZING") || !strcmp(phase, "REMEMBERING")) return 3;
  if (!strcmp(phase, "QUEUED")) return 0;
  return 0;
}

int askStage(const char* action) {
  if (!action) return 0;
  if (!strcmp(action, "CONNECTING")) return 1;
  if (!strcmp(action, "THINKING") || !strcmp(action, "TRANSCRIBING") || !strcmp(action, "DECODING") ||
      !strcmp(action, "UNDERSTANDING") || !strcmp(action, "PREPARING") ||
      !strcmp(action, "TODAY") || !strcmp(action, "SEARCHING")) return 2;
  if (!strcmp(action, "BUILDING")) return 3;
  return 0;
}

const char* memoryPhaseCopy(const char* phase) {
  if (!phase) return uiText("MOMENT...", "ONE MOMENT");
  if (!strcmp(phase, "CONNECTING")) return uiText("VERBINDE", "CONNECTING");
  if (!strcmp(phase, "DECODING")) return uiText("TRANSKRIBIERE", "TRANSCRIBING");
  if (!strcmp(phase, "UNDERSTANDING") || !strcmp(phase, "ORGANIZING")) return uiText("ORDNE EIN", "ORGANIZING");
  if (!strcmp(phase, "REMEMBERING")) return uiText("MERKE MIR", "REMEMBERING");
  if (!strcmp(phase, "QUEUED")) return uiText("SPAETER WEITER", "SAVED FOR LATER");
  if (!strcmp(phase, "DONE")) return uiText("FERTIG", "DONE");
  return uiText("MOMENT...", "ONE MOMENT");
}

const char* askPhaseCopy(const char* action) {
  if (action && !strcmp(action,"UPDATING")) return "UPDATING";
  if (action && !strcmp(action,"TESTING")) return "TESTING";
  if (!action) return uiText("DENKE NACH", "THINKING");
  if (!strcmp(action, "CONNECTING")) return uiText("VERBINDE", "CONNECTING");
  if (!strcmp(action, "TRANSCRIBING")) return "TRANSCRIBING";
  if (!strcmp(action, "DECODING") || !strcmp(action, "UNDERSTANDING")) return uiText("HOERE REIN", "LISTENING");
  if (!strcmp(action, "PREPARING")) return uiText("HOLE ES", "RECALLING");
  if (!strcmp(action, "SEARCHING")) return uiText("SUCHE...", "SEARCHING");
  if (!strcmp(action, "BUILDING")) return uiText("FAST DA", "ALMOST THERE");
  if (!strcmp(action, "TODAY")) return uiText("HEUTE", "TODAY");
  return uiText("DENKE NACH", "THINKING");
}

std::vector<String> answerLines(const String& raw) {
  const String text = normalizeForDisplay(raw);
  std::vector<String> lines;
  String line;
  int pos = 0;
  while (pos < static_cast<int>(text.length())) {
    while (pos < static_cast<int>(text.length()) && text[pos] == ' ') pos++;
    if (pos >= static_cast<int>(text.length())) break;
    int end = text.indexOf(' ', pos);
    if (end < 0) end = text.length();
    String word = text.substring(pos, end);
    pos = end + 1;
    while (textW(word.c_str(), 1) > kContentWidth) {
      int cut = word.length();
      while (cut > 1 && textW((word.substring(0, cut) + "-").c_str(), 1) > kContentWidth) cut = pikaUtf8Previous(word.c_str(), cut);
      if (line.length()) { lines.push_back(line); line = ""; }
      lines.push_back(word.substring(0, cut) + "-");
      word = word.substring(cut);
    }
    const String candidate = line.length() ? line + " " + word : word;
    if (textW(candidate.c_str(), 1) <= kContentWidth) line = candidate;
    else { if (line.length()) lines.push_back(line); line = word; }
  }
  if (line.length()) lines.push_back(line);
  if (lines.empty()) lines.push_back(uiText("KEINE ANTWORT", "NO ANSWER"));
  return lines;
}

String homeDateLabel() {
  String local = utcToLocalDeviceLabel(currentUtcIso());
  if (local.length() < 10 || local == "time not set") return uiText("DATUM FEHLT", "DATE NOT SET");
  static const UiTranslation months[] = {
    {"JAN","JAN","ENE","GEN","STY","LED","YAN","SIC","JAN","JAN"},
    {"FEB","FEV","FEB","FEB","LUT","UNO","FEV","LYU","FEB","FEV"},
    {"MAR","MAR","MAR","MAR","MAR","BRE","MAR","BER","MRT","MAR"},
    {"APR","AVR","ABR","APR","KWI","DUB","APR","KVI","APR","ABR"},
    {"MAY","MAI","MAY","MAG","MAJ","KVE","MAY","TRA","MEI","MAI"},
    {"JUN","JUI","JUN","GIU","CZE","CER","IYU","CHE","JUN","JUN"},
    {"JUL","JUI","JUL","LUG","LIP","CEC","IYU","LYP","JUL","JUL"},
    {"AUG","AOU","AGO","AGO","SIE","SRP","AVG","SER","AUG","AGO"},
    {"SEP","SEP","SEP","SET","WRZ","ZAR","SEN","VER","SEP","SET"},
    {"OCT","OCT","OCT","OTT","PAZ","RIJ","OKT","ZHO","OKT","OUT"},
    {"NOV","NOV","NOV","NOV","LIS","LIS","NOY","LYS","NOV","NOV"},
    {"DEC","DEC","DIC","DIC","GRU","PRO","DEK","HRU","DEC","DEZ"}
  };
  const int month = constrain(local.substring(5, 7).toInt(), 1, 12);
  const String language = pikaUiLanguage();
  const UiTranslation& row = months[month - 1];
  const char* label = row.en;
  if (language == "fr") label = row.fr; else if (language == "es") label = row.es;
  else if (language == "it") label = row.it; else if (language == "pl") label = row.pl;
  else if (language == "cs") label = row.cs; else if (language == "ru") label = row.ru;
  else if (language == "uk") label = row.uk; else if (language == "nl") label = row.nl;
  else if (language == "pt") label = row.pt;
  const int day=local.substring(8,10).toInt();int year=local.substring(0,4).toInt();static const int offsets[]={0,3,2,5,0,3,5,1,4,6,2,4};static const char* days[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};year-=month<3;const int weekday=(year+year/4-year/100+year/400+offsets[month-1]+day)%7;
  return String(days[weekday])+" "+String(day)+" "+label;
}

String greetingLabel() {
  const String local = utcToLocalDeviceLabel(currentUtcIso());
  if (local.length() < 13 || local == "time not set") return "TODAY";
  const int hour = local.substring(11, 13).toInt();
  if (hour < 12) return "GUTEN MORGEN";
  if (hour < 18) return "GUTEN TAG";
  return "GUTEN ABEND";
}

String nextTodayItem(const std::vector<BrainTaskRecord>& tasks, const String& todayKey) {
  String candidate;
  String candidateDue;
  for (const BrainTaskRecord& task : tasks) {
    if (task.done || task.type == "event") continue;
    if (!task.dueUtc.length()) {
      if (!candidate.length()) candidate = task.text;
      continue;
    }
    if (!todayKey.length()) continue;
    const String localDue = utcToLocalDeviceLabel(task.dueUtc);
    if (localDue.length() < 10 || localDue == "time not set") continue;
    if (localDue.substring(0, 10) > todayKey) continue;
    if (!candidate.length() || localDue < candidateDue) {
      candidate = task.text;
      candidateDue = localDue;
    }
  }
  return normalizeForDisplay(candidate);
}

std::vector<BrainTaskRecord> todayAgendaItems(bool events) {
  const String nowUtc = currentUtcIso();
  const String localNow = utcToLocalDeviceLabel(nowUtc);
  const String todayKey = localNow.length() >= 10 && localNow != "time not set"
                            ? localNow.substring(0, 10) : "";
  std::vector<BrainTaskRecord> items;
  for (const BrainTaskRecord& item : loadBrainTasks()) {
    if (item.done || item.archived) continue;
    if (!item.dueUtc.length()) {
      if (!events && undatedTaskVisible(item.type == "event", item.done)) items.push_back(item);
      continue;
    }
    if (!todayKey.length()) continue;
    const String localDue = utcToLocalDeviceLabel(item.dueUtc);
    if (localDue.length() < 10 || localDue == "time not set") continue;
    const String dueDay = localDue.substring(0, 10);
    if (events ? (item.type == "event" && dueDay == todayKey && eventStillRelevant(
                   dueDay.c_str(), todayKey.c_str(), item.dueUtc.c_str(), nowUtc.c_str(),
                   item.datePrecision == "day"))
               : (item.type != "event" && dueDay <= todayKey)) items.push_back(item);
  }
  std::sort(items.begin(), items.end(), [](const BrainTaskRecord& a, const BrainTaskRecord& b) {
    return a.dueUtc < b.dueUtc;
  });
  return items;
}

constexpr int kAgendaLinesPerPage = 5;

int agendaPages(const std::vector<BrainTaskRecord>& items) {
  int pages = 0;
  for (const BrainTaskRecord& item : items) {
    const int lines = max(1, static_cast<int>(answerLines(item.text).size()));
    pages += (lines + kAgendaLinesPerPage - 1) / kAgendaLinesPerPage;
  }
  return max(1, pages);
}
}  // namespace

void showIdle(int pendingCount, int batteryPercent) {
 ManagerReadSnapshot readSnapshotScope;
  clearWhite();
  drawHeader(nullptr, BLACK);
  drawBattery(batteryPercent);
  const String nowUtc = currentUtcIso();
  const String localNow = utcToLocalDeviceLabel(nowUtc);
  const String todayKey = localNow.length() >= 10 && localNow != "time not set" ? localNow.substring(0, 10) : "";
  const auto& tasks = brainTaskSnapshot();
  int memoryCount = pikaPhoneCachedMemoryCount();
  if(memoryCount<0){memoryCount=0;for(const NoteEntry& note:noteIndex)if(!managerArchived(note.num))++memoryCount;}
  int taskCount = 0, eventCount = 0, upcomingEventCount = 0;
  for (const BrainTaskRecord& task : tasks) {
    if (task.done || task.archived) continue;
    // Home counts every active task, independent of its deadline.
    if (task.type != "event") { ++taskCount; continue; }
    if (!task.dueUtc.length()) continue;
    if (!todayKey.length()) continue;
    const String localDue = utcToLocalDeviceLabel(task.dueUtc);
    if (localDue.length() < 10 || localDue == "time not set") continue;
    const String dueDay = localDue.substring(0, 10);
    if (task.type == "event") {
      const String lastUtc = task.endUtc.length() ? task.endUtc : task.dueUtc;
      const String lastDay = utcToLocalDeviceLabel(lastUtc).substring(0, 10);
      if (eventStillRelevant(lastDay.c_str(), todayKey.c_str(), lastUtc.c_str(),
          nowUtc.c_str(), task.datePrecision == "day")) ++upcomingEventCount;
      if (dueDay == todayKey && eventStillRelevant(dueDay.c_str(), todayKey.c_str(),
          task.dueUtc.c_str(), nowUtc.c_str(), task.datePrecision == "day")) eventCount++;
    }
  }
  const String date = homeDateLabel();
  if(!pikaPhonePairCode())labelLeft(kContentTop, date.c_str(), 1, BLACK);
  if(pikaPhonePairCode()){char pair[30];snprintf(pair,sizeof(pair),"Pair phone: %06lu",(unsigned long)pikaPhonePairCode());labelLeft(kContentTop,pair,1,BLACK);}
  drawHero(uiText("HEUTE", "TODAY"), 58, BLACK, 96);
  hline(kInset, 103, kContentWidth, BLACK);
  const bool mailEnabled=pikaMailConfigured();const int row=mailEnabled?22:26,first=mailEnabled?109:111;
  char count[8];
  labelLeft(first+4, "MEMORIES", 1, BLACK, 120);
  snprintf(count, sizeof(count), "%d", min(memoryCount, 999));
  labelRight(first, count, 2, BLACK, 48);
  labelLeft(first+row+4, uiText("AUFGABEN", "TASKS"), 1, BLACK, 120);
  snprintf(count, sizeof(count), "%d", min(taskCount, 999));
  labelRight(first+row, count, 2, BLACK, 48);
  labelLeft(first+row*2+4, uiText("TERMINE", "EVENTS"), 1, BLACK, 120);
  snprintf(count, sizeof(count), "%d", min(eventCount, 999));
  char total[12];
  snprintf(total, sizeof(total), "/%d", min(upcomingEventCount, 999));
  // Same family, point size and baseline; only the weight changes at the slash.
  const GFXfont* regular = &FreeSans12pt7b;
  const GFXglyph* glyphs = reinterpret_cast<const GFXglyph*>(pgm_read_ptr(&regular->glyph));
  int suffixAdvance = 0;
  for (const char* p = total; *p; ++p)
    suffixAdvance += pgm_read_byte(&glyphs[*p - pgm_read_word(&regular->first)].xAdvance);
  int minX, minY, maxX, maxY, countAdvance;
  textBoundsFont(count, 2, &minX, &minY, &maxX, &maxY, &countAdvance);
  int cursor = kRight - suffixAdvance - countAdvance;
  const int baseline = first+row*2 - minY;
  for (const char* p = count; *p; ++p) {
    int advance; drawGlyphFont(cursor, baseline, *p, uiFontForScale(2), BLACK, &advance); cursor += advance;
  }
  for (const char* p = total; *p; ++p) {
    int advance; drawGlyphFont(cursor, baseline, *p, regular, BLACK, &advance); cursor += advance;
  }
  if(mailEnabled){labelLeft(first+row*3+4,"EMAILS",1,BLACK,120);snprintf(count,sizeof(count),"%d",min(pikaMailUnreadCount(),999));labelRight(first+row*3,count,2,BLACK,48);}
  rememberHomePixels();refreshHomeAsync();
}

int todayAgendaPageCount(bool events) {
  return agendaPages(todayAgendaItems(events));
}

void showTodayAgenda(bool events, int page) {
  clearWhite();
  const std::vector<BrainTaskRecord> items = todayAgendaItems(events);
  const int pages = agendaPages(items);
  page = constrain(page, 0, pages - 1);
  char pageLabel[12];
  snprintf(pageLabel, sizeof(pageLabel), "%d/%d", page + 1, pages);
  drawHeader(items.empty() ? nullptr : pageLabel, BLACK);
  labelLeft(kContentTop, events ? uiText("TERMINE", "EVENTS") : uiText("AUFGABEN", "TASKS"), 1, BLACK, 112);

  if (items.empty()) {
    drawTwoLineHero(events ? uiText("KEINE", "NO") : uiText("NICHTS", "ALL"),
                    events ? uiText("TERMINE", "EVENTS") : uiText("OFFEN", "DONE"),
                    76, BLACK);
  } else {
    int remaining = page;
    int selected = 0;
    int firstLine = 0;
    std::vector<String> lines;
    for (int i = 0; i < static_cast<int>(items.size()); i++) {
      lines = answerLines(items[i].text);
      const int itemPages = max(1, (static_cast<int>(lines.size()) + kAgendaLinesPerPage - 1) /
                                       kAgendaLinesPerPage);
      if (remaining < itemPages) {
        selected = i;
        firstLine = remaining * kAgendaLinesPerPage;
        break;
      }
      remaining -= itemPages;
    }
    const String localDue = utcToLocalDeviceLabel(items[selected].dueUtc);
    String meta = events && items[selected].datePrecision != "day" && localDue.length() >= 16 ? localDue.substring(11, 16)
                                                    : String(selected + 1);
    labelRight(kContentTop, meta.c_str(), 1, BLACK, 58);
    for (int i = 0; i < kAgendaLinesPerPage && firstLine + i < static_cast<int>(lines.size()); i++)
      drawStr(kInset, 68 + i * 19, lines[firstLine + i].c_str(), 1, BLACK);
  }
  if (items.empty() || pages == 1) drawFooterCentered(uiText("NOCHMAL: ZURUECK", "AGAIN: BACK"), BLACK);
  else drawFooter(uiText("WEITER", "NEXT"), uiText("ZURUECK", "BACK"), BLACK);
  refresh();
}

bool showRecording() {
  fillRect(0, 0, W, H, BLACK);
  drawHeader(uiText("NOTIZ", "NOTE"), WHITE);
  drawStatusHero(uiText("HOERE ZU", "LISTENING"), 67, WHITE);
  fillCircle(kRight - 7, 146, 7, WHITE);
  labelLeft(139, uiText("AUFNAHME", "RECORDING"), 1, WHITE);
  return refreshRecordingStart();
}

void showRecordingStopped(bool question) {
  clearWhite();
  drawHeader(nullptr, BLACK);
  drawStatusHero(question ? "PROCESSING" : "SAVING", 78, BLACK);
  drawProgressSegments(1, 3, 151, BLACK);
  refreshFast();
}

void showRecordingProgress(uint32_t elapsedMs, int moments) {
  fillRect(0, 0, W, H, BLACK);
  drawHeader(uiText("NOTIZ", "NOTE"), WHITE);
  labelLeft(kContentTop, uiText("AUFNAHME", "RECORDING"), 1, WHITE);
  drawTimer(elapsedMs, WHITE);
  refreshFast();
}

void showSaved(int noteNumber) {
  clearWhite();
  char number[12];
  snprintf(number, sizeof(number), "#%03d", noteNumber % 1000);
  drawHeader(number, BLACK);
  strokeCircle(36, 67, 14, 2, BLACK);
  thickLine(29, 67, 34, 73, 2, BLACK);
  thickLine(34, 73, 44, 60, 2, BLACK);
  drawStatusHero(uiText("GEMERKT", "SAVED"), 96, BLACK);
  refreshFast();
}

void showMemoryProcessing(int noteNumber, const char* phase) {
  clearWhite();
  char number[12];
  snprintf(number, sizeof(number), "#%03d", noteNumber % 1000);
  drawHeader(noteNumber>0?number:"ASK", BLACK);
  labelLeft(kContentTop, noteNumber>0?uiText("NOTIZ", "NOTE"):uiText("FRAGE","QUESTION"), 1, BLACK);
  drawStatusHero(memoryPhaseCopy(phase), 78, BLACK);
  const int stage = memoryStage(phase);
  if(strcmp(phase,"QUEUED"))drawProgressSegments(stage, 3, 151, BLACK);
  refresh();
}

void showBacklogProcessing(int completed, int total, const char* phase) {
  clearWhite();
  completed = constrain(completed, 0, max(total, 1));
  total = max(total, 1);
  drawHeader("SYNC", BLACK);
  char progress[20];
  snprintf(progress, sizeof(progress), "%d / %d", completed, total);
  labelLeft(kContentTop, progress, 1, BLACK);
  drawStatusHero(memoryPhaseCopy(phase), 78, BLACK);
  strokeRect(kInset, 149, kContentWidth, 12, 1, BLACK);
  const int innerWidth = kContentWidth - 4;
  const int filled = total > 0 ? (innerWidth * completed) / total : 0;
  if (filled > 0) fillRect(kInset + 2, 151, filled, 8, BLACK);
  refresh();
}

bool showAskListening() {
  fillRect(0, 0, W, H, BLACK);
  drawHeader(uiText("FRAGE", "ASK"), WHITE);
  drawStatusHero(uiText("HOERE ZU", "LISTENING"), 67, WHITE);
  fillCircle(kRight - 7, 146, 7, WHITE);
  labelLeft(139, uiText("AUFNAHME", "RECORDING"), 1, WHITE);
  return refreshRecordingStart();
}

void showAskThinking() { showAskProgress("THINKING", "WITH MEMORY"); }

void showAskProgress(const char* action, const char* object) {
  const uint32_t started=millis();
  clearWhite();
  drawHeader(nullptr, BLACK);
  drawStatusHero(askPhaseCopy(action), 78, BLACK);
  const int stage = askStage(action);
  if (!action || strcmp(action,"UPDATING")) drawProgressSegments(stage, 3, 151, BLACK);
  // Transitional white status pages do not need a periodic full cleaning.
  refreshFast();
  Serial.printf("PIKA_ASK_PANEL phase=%s ms=%lu\n",action?action:"",(unsigned long)(millis()-started));
}

void showWakeUpdate(int completed,int total,const char* phase){
 clearWhite();drawHeader(nullptr,BLACK);drawStatusHero("UPDATING",78,BLACK);
 if(phase&&*phase)labelLeft(122,phase,1,BLACK,kContentWidth);
 total=max(1,total);completed=constrain(completed,0,total);
 constexpr int gap=6,width=(kContentWidth-2*gap)/3;
 for(int i=0;i<3;++i){const int x=kInset+i*(width+gap);strokeRect(x,151,width,6,1,BLACK);const int fill=constrain((completed*3-i*total)*(width-2)/total,0,width-2);if(fill>0)fillRect(x+1,152,fill,4,BLACK);}
 refresh();
}

void showAskSpeaking(const String& answer, int sourceCount) {
  showAskSpeakingPage(answer, sourceCount, 0);
}

int askAnswerPageCount(const String& answer) {
  const int count = answerLines(answer).size();
  return max(1, (count + kAnswerLinesPerPage - 1) / kAnswerLinesPerPage);
}

void showAskSpeakingPage(const String& answer, int sourceCount, int page) {
  const std::vector<String> lines = answerLines(answer);
  const int pages = max(1, (static_cast<int>(lines.size()) + kAnswerLinesPerPage - 1) / kAnswerLinesPerPage);
  page = constrain(page, 0, pages - 1);
  clearWhite();
  char pageLabel[12];
  snprintf(pageLabel, sizeof(pageLabel), "%d/%d", page + 1, pages);
  drawHeader(pageLabel, BLACK);
  labelLeft(kContentTop, sourceCount > 0 ? uiText("AUS ERINNERUNGEN", "FROM MEMORY") : uiText("ANTWORT", "ANSWER"), 1, BLACK);
  const int first = page * kAnswerLinesPerPage;
  for (int i = 0; i < kAnswerLinesPerPage && first + i < static_cast<int>(lines.size()); i++)
    labelLeft(65 + i * 18, lines[first + i].c_str(), 1, BLACK);
  drawFooter(page < pages - 1 ? uiText("WEITER", "NEXT") : uiText("ENDE", "END"),
             uiText("ZURUECK", "BACK"), BLACK);
  refresh();
}

void showTransferConnecting() {
  clearWhite();
  drawHeader("LOCAL", BLACK);
  labelLeft(kContentTop, "COMPANION", 1, BLACK);
  drawStatusHero(uiText("VERBINDE", "CONNECTING"), 67, BLACK);
  drawProgressSegments(1, 3, 143, BLACK);
  drawFooter("1 / 3", "WORKING", BLACK);
  refresh();
}

void showTransferMode(const char* ip, const char* apPassword) {
  clearWhite();
  drawHeader("COMPANION", BLACK);
  if (apPassword && strlen(apPassword) > 0) {
    labelLeft(kContentTop, "JOIN WI-FI", 1, BLACK);
    drawHero("PIKA NOTE", 66, BLACK, 100);
    labelLeft(112, "PASSWORD", 1, BLACK);
    labelLeft(132, apPassword, 2, BLACK);
    drawFooter("SETUP", ip, BLACK);
  } else {
    labelLeft(kContentTop, "OPEN IN BROWSER", 1, BLACK);
    fillRect(kInset, 70, kContentWidth, 52, BLACK);
    drawStrInBox(kInset, 70, kContentWidth, 52, ip, 2, WHITE);
    labelLeft(140, "pikanote.local", 1, BLACK);
    drawFooter("COMPANION", "READY", BLACK);
  }
  refresh();
}

void showVolumeSetting(int percent) {
  clearWhite();
  drawHeader("SETTINGS", BLACK);
  const int level = constrain(percent, 20, 100);
  char value[18];
  snprintf(value, sizeof(value), "VOLUME %d", (level + 19) / 20);
  labelLeft(kContentTop, "SPEAKER", 1, BLACK);
  drawHero(value, 67, BLACK, 108);
  const int bars = constrain((level + 19) / 20, 1, 5);
  for (int i = 0; i < 5; i++) {
    const int height = 8 + i * 7;
    if (i < bars) fillRect(18 + i * 34, 157 - height, 20, height, BLACK);
    else strokeRect(18 + i * 34, 157 - height, 20, height, 1, BLACK);
  }
  drawFooter("UPPER +", "LOWER -", BLACK);
  refresh();
}

void showSetupRequired() {
  clearWhite();
  drawHeader("SETUP", BLACK);
  labelLeft(kContentTop, uiText("NOCH NICHT BEREIT", "NOT READY YET"), 1, BLACK);
  drawHero(uiText("EINRICHTEN", "SET UP"), 70, BLACK, 120);
  labelLeft(143, uiText("IM PIKA FLASHER", "IN PIKA FLASHER"), 1, BLACK);
  drawFooter("USB", uiText("BEREIT", "READY"), BLACK);
  refresh();
}

void showError(const char* message) {
  pikaError("ui","Try again",message?message:"Unspecified failure; inspect preceding errors");
  pikaPlayNotify(2);
  Serial.printf("[UI] error: %s\n", message ? message : "unspecified");
  clearWhite();
  drawHeader("PIKA", BLACK);
  drawStatusHero(uiText("NOCHMAL?", "TRY AGAIN"), 72, BLACK);
  labelLeft(143, uiText("HAT NICHT GEKLAPPT", "SOMETHING WENT WRONG"), 1, BLACK);
  if (message && *message) labelLeft(158, message, 1, BLACK);
  refresh();
}

namespace {
RTC_NOINIT_ATTR uint32_t sleepHomeMagic,sleepHomeHash;
RTC_NOINIT_ATTR uint8_t sleepHomePixels[W*H/8];
RTC_NOINIT_ATTR time_t sleepHomeTime;
uint32_t homePixelsHash(const uint8_t* bytes){uint32_t h=2166136261UL;for(size_t i=0;i<sizeof(sleepHomePixels);i++)h=(h^bytes[i])*16777619UL;return h;}
void rememberHomePixels(){sleepHomeMagic=0;memcpy(sleepHomePixels,display->getBuffer(),sizeof(sleepHomePixels));sleepHomeHash=homePixelsHash(sleepHomePixels);sleepHomeTime=time(nullptr);sleepHomeMagic=0x50484f4d;}
}
bool showSleepLocked() {
  pikaWaitHomeRefresh();
  // Always derive sleep from the normal retained Today, never toggle whatever
  // frame happens to be in RAM. Repeated sleep/reboot attempts stay dark.
  if(sleepHomeMagic!=0x50484f4d||homePixelsHash(sleepHomePixels)!=sleepHomeHash)rememberHomePixels();
  uint8_t* pixels=display->getBuffer();
  for(size_t i=0;i<sizeof(sleepHomePixels);i++)pixels[i]=~sleepHomePixels[i];
  unsigned white=0;for(size_t i=0;i<sizeof(sleepHomePixels);i++)white+=__builtin_popcount(pixels[i]);Serial.printf("PIKA_SLEEP_PIXELS white=%u black=%u\n",white,unsigned(sizeof(sleepHomePixels)*8-white));
  for(unsigned attempt=0;attempt<2;attempt++){
    const bool confirmed=refreshSleep();
    Serial.printf("PIKA_SLEEP_FRAME attempt=%u confirmed=%d\n",attempt+1,confirmed);
    if(confirmed)return true;
  }
  pikaError("sleep","display_frame","Sleep postponed: final display update failed");
  return false;
}

bool showCachedWakeHome(bool afterUsbRestart){
  if((!afterUsbRestart&&esp_sleep_get_wakeup_cause()!=ESP_SLEEP_WAKEUP_EXT1)||sleepHomeMagic!=0x50484f4d||homePixelsHash(sleepHomePixels)!=sleepHomeHash)return false;
  memcpy(display->getBuffer(),sleepHomePixels,sizeof(sleepHomePixels));refreshHomeAsync();return true;
}
String cachedWakeHomeDay(){struct tm local={};localtime_r(&sleepHomeTime,&local);char out[11];strftime(out,sizeof(out),"%Y-%m-%d",&local);return String(out);}

void showUltraSleepScreen() {
  clearWhite();
  drawHeader("RESTING", BLACK);
  drawStrC(W / 2, 76, "PIKA", 3, BLACK);
  drawStrC(W / 2, 123, "PRESS TO WAKE", 1, BLACK);
  refreshClean();
}

void showClockScreen(){
  clearWhite();setenv("TZ",LOCAL_TZ_RULE,1);tzset();struct tm local={};time_t now=time(nullptr);localtime_r(&now,&local);
  char clock[8];strftime(clock,sizeof(clock),"%H:%M",&local);
  int width=0;for(const char* p=clock;*p;p++)width+=pgm_read_byte(&kClockGlyphs[*p-'0'].xAdvance);
  int cursor=(W-width)/2;const int baseline=123;
  for(const char* p=clock;*p;p++){int advance=0;drawGlyphFont(cursor,baseline,*p,&kClockFont,BLACK,&advance);cursor+=advance;}
  refresh();
}
void showEventAlert(const String& title,const String& starts){
  clearWhite();drawHeader("REMINDER",BLACK);labelLeft(43,"UPCOMING EVENT",1,BLACK);
  drawWrappedText(14,68,172,22,3,title,BLACK);labelLeft(154,starts.c_str(),1,BLACK);drawFooter("EVENT", "BACK: LOWER",BLACK);refresh();
}


void showMonthCalendar(){
 clearWhite();drawHeader(nullptr,BLACK);time_t now=time(nullptr);struct tm local={};localtime_r(&now,&local);
 if(now<1700000000){labelLeft(80,"TIME NOT SET",1,BLACK);drawFooter(nullptr,"ANY: BACK",BLACK);refresh();return;}
 char title[24];const char* months[]={"January","February","March","April","May","June","July","August","September","October","November","December"};snprintf(title,sizeof(title),"%s %d",months[local.tm_mon],local.tm_year+1900);drawStrC(W/2,43,title,2,BLACK);
 const char* weekdays[]={"M","T","W","T","F","S","S"};for(int col=0;col<7;col++)drawStrC(26+col*24,67,weekdays[col],1,BLACK);
 struct tm first=local;first.tm_mday=1;first.tm_hour=12;first.tm_isdst=-1;mktime(&first);const int offset=(first.tm_wday+6)%7;
 const int lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};const int year=local.tm_year+1900;int count=lengths[local.tm_mon]+(local.tm_mon==1&&year%4==0&&(year%100!=0||year%400==0));
 for(int day=1;day<=count;day++){const int cell=offset+day-1,x=26+(cell%7)*24,y=83+(cell/7)*14;char text[4];snprintf(text,sizeof(text),"%d",day);const bool today=day==local.tm_mday;if(today)fillCircle(x,y+4,9,BLACK);drawStrC(x,y,text,1,today?WHITE:BLACK);}
 drawFooter(nullptr,"ANY: BACK",BLACK);refresh();
}
namespace {
// Original vector icons for the 200x200 Pika display.
void weatherIcon(int x,int y,int size,int code,bool day){
 const int r=size/6,cx=x+size/2,cy=y+size/2;
 if(code<0){drawStrC(cx,y+size/3,"?",1,BLACK);return;}
 const bool snow=code==71||code==73||code==75||code==77||code==85||code==86;
 const bool storm=code>=95,wet=(code>=51&&code<=67)||(code>=80&&code<=82);
 if(code<=2){int sx=x+size/3,sy=y+size/3;if(day){strokeCircle(sx,sy,r,1,BLACK);for(int i=0;i<8;i++){float angle=i*0.785398f;line(sx+cosf(angle)*(r+3),sy+sinf(angle)*(r+3),sx+cosf(angle)*(r+(size<40?4:6)),sy+sinf(angle)*(r+(size<40?4:6)),BLACK);}}else{fillCircle(sx,sy,r+2,BLACK);fillCircle(sx+4,sy-3,r+1,WHITE);}}
 if(code==0)return;
 if(code==45||code==48){for(int i=0;i<4;i++)hline(x+5+(i%2)*4,y+size/3+i*4,size-14,BLACK);return;}
 int base=cy+size/8;fillCircle(cx-r,cy,r,BLACK);fillCircle(cx,cy-r/2,r+2,BLACK);fillCircle(cx+r,cy,r,BLACK);fillRect(cx-r,cy,2*r,r,BLACK);
 fillCircle(cx-r,cy,r-1,WHITE);fillCircle(cx,cy-r/2,r+1,WHITE);fillCircle(cx+r,cy,r-1,WHITE);fillRect(cx-r,cy,2*r,r-1,WHITE);hline(cx-2*r,cy+r,4*r,BLACK);
 if(wet)for(int i=-1;i<=1;i++)line(cx+i*(r+1),base+r+2,cx+i*(r+1)-3,base+r+7,BLACK);
 if(snow)for(int i=-1;i<=1;i++){int sx=cx+i*(r+1),sy=base+r+5;hline(sx-2,sy,5,BLACK);vline(sx,sy-2,5,BLACK);}
 if(storm){line(cx+2,base+r,cx-3,base+r+6,BLACK);hline(cx-3,base+r+6,7,BLACK);line(cx+3,base+r+6,cx-2,base+r+(size<40?8:12),BLACK);}
}
const char* weatherDescription(int code){if(code==0)return "Clear sky";if(code==1)return "Mainly clear";if(code==2)return "Partly cloudy";if(code==3)return "Overcast";if(code==45||code==48)return "Fog";if(code>=95)return "Thunderstorm";if(code==71||code==73||code==75||code==77||code==85||code==86)return "Snow";if(code>=80)return "Rain showers";if(code>=61)return "Rain";if(code>=51)return "Drizzle";return "No current data";}
}
void showWeatherScreen(){
 clearWhite();const auto& location=pikaWeatherLocation(0);const auto& now=location.current;
 if(!location.enabled){drawStrC(100,72,"SET LOCATION",1,BLACK);drawStrC(100,94,"IN CONFIG",1,BLACK);drawFooter(nullptr,"ANY: BACK",BLACK);refresh();return;}
 struct tm local={};time_t clock=time(nullptr);localtime_r(&clock,&local);char text[80];
 if(clock>=1700000000){strftime(text,sizeof(text),"%H:%M",&local);drawStr(5,2,text,1,BLACK);}
 drawStrFit(65,2,130,location.name.c_str(),1,BLACK);hline(2,19,196,BLACK);
 if(!now.valid){drawStrC(100,60,"CONNECT TO UPDATE",1,BLACK);drawStrC(100,82,"NO CURRENT DATA",1,BLACK);drawFooter(nullptr,"ANY: BACK",BLACK);refresh();return;}
 vline(113,20,80,BLACK);snprintf(text,sizeof(text),"%.0f",now.temperature);int scale=3;while(scale>1&&textW(text,scale)>84)scale--;drawStr(7+(84-textW(text,scale))/2,25+(40-uiFontHeight(scale))/2,text,scale,BLACK);strokeCircle(99,29,3,1,BLACK);drawStr(95,43,"C",1,BLACK);
 if(location.days[0].valid){snprintf(text,sizeof(text),"%.0f / %.0f C",location.days[0].maximum,location.days[0].minimum);drawStrC(57,69,text,1,BLACK);}
 snprintf(text,sizeof(text),"%.0f%% RH",now.humidity);drawStrC(57,86,text,1,BLACK);weatherIcon(122,24,60,now.code,now.day);
 const char* compass[]={"N","NE","E","SE","S","SW","W","NW"};snprintf(text,sizeof(text),"%s %.0fm/s",compass[(int((now.direction+22.5f)/45))%8],now.wind);drawStrFit(118,86,78,text,1,BLACK);hline(2,103,196,BLACK);
 drawStrC(100,106,weatherDescription(now.code),1,BLACK);hline(2,122,196,BLACK);
 for(int i=0;i<4;i++){int left=2+i*49;const auto& point=location.hours[i];if(i)vline(left,123,56,BLACK);if(!point.valid)continue;drawStrC(left+24,126,point.time.substring(11,16).c_str(),1,BLACK);weatherIcon(left+12,139,24,point.code,point.day);snprintf(text,sizeof(text),"%.0f",point.temperature);drawStrC(left+24,166,text,1,BLACK);}
 hline(2,182,196,BLACK);String stamp=String("Saved ")+utcToLocalDeviceLabel(location.updated).substring(5,16);drawStrFit(5,186,190,stamp.c_str(),1,BLACK);refresh();
}

void showMailScreen(int page){
 clearWhite();const auto& mail=pikaMailItems();const int pages=max(1,((int)mail.size()+3)/4);page=constrain(page,0,pages-1);
 String heading=String(pikaMailStatus().startsWith("Connected;")?"MAIL ":"MAIL* ")+String(page+1)+"/"+String(pages);drawHeader(heading.c_str(),BLACK);
 if(mail.empty()){labelLeft(55,pikaMailConfigured()?"NO MAIL":"CONNECT GMAIL",1,BLACK);drawWrappedText(14,83,172,18,4,pikaMailStatus(),BLACK);}
 else for(int row=0;row<4&&page*4+row<(int)mail.size();row++){
  const auto& m=mail[page*4+row];const int y=37+row*33;const int weight=m.unread?-1:1;
  drawStrFit(10,y,180,(pikaMailDate(m.received)+" "+m.from).c_str(),weight,BLACK);
  drawStrFit(10,y+16,180,m.subject.length()?m.subject.c_str():"(No subject)",weight,BLACK);
 }
 drawFooter(page+1<pages?"NEXT":"HOME","BACK",BLACK);refresh();
}

void showAskActionProgress(const char* phase,const char* object,int completed,int total){
 clearWhite();drawHeader(nullptr,BLACK);drawStatusHero(phase,78,BLACK);
 char caption[40];snprintf(caption,sizeof(caption),"%s %d/%d",object,completed,total);
 labelLeft(122,caption,1,BLACK,kContentWidth);
 total=max(1,total);completed=constrain(completed,0,total);
 constexpr int gap=6,width=(kContentWidth-2*gap)/3;
 for(int i=0;i<3;++i){const int x=kInset+i*(width+gap);strokeRect(x,151,width,6,1,BLACK);const int fill=constrain((completed*3-i*total)*(width-2)/total,0,width-2);if(fill>0)fillRect(x+1,152,fill,4,BLACK);}
 refresh();
}

void showNoConnection(int stage){clearWhite();drawHeader(nullptr,BLACK);drawStatusHero("NO CONNECTION",78,BLACK);drawProgressSegments(stage,3,151,BLACK);refresh();}

void showPairingOnly(){
 clearWhite();drawHeader("PAIR",BLACK);
 labelLeft(43,"Pika_note",1,BLACK);drawHero("HELLO",67,BLACK,112);
 hline(kInset,111,kContentWidth,BLACK);labelLeft(121,"PAIRING CODE",1,BLACK);
 char code[7];snprintf(code,sizeof(code),"%06lu",(unsigned long)pikaPhonePairCode());drawStatusHero(code,141,BLACK);
 drawFooterCentered(pikaDeviceSerial().c_str(),BLACK);refresh();
}

void showRebooting(){clearWhite();drawHeader(nullptr,BLACK);drawStatusHero("REBOOTING",96,BLACK);refreshFast();}
void showRebootSleep(){pikaWaitHomeRefresh();if(sleepHomeMagic==0x50484f4d&&homePixelsHash(sleepHomePixels)==sleepHomeHash){memcpy(display->getBuffer(),sleepHomePixels,sizeof(sleepHomePixels));}else{clearWhite();drawHeader(nullptr,BLACK);drawStatusHero("PIKA",96,BLACK);}showSleepLocked();}
