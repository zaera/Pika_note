#pragma once

#include <Arduino.h>
#include <vector>

struct BrainTaskRecord {
  String id;
  int noteNum = -1;
  String text;
  String createdUtc;
  String dueUtc;
  String endUtc;
  int priority = 1;
  String type = "todo";
  String datePrecision = "none";
  String updatedUtc;
  bool done = false;
  bool archived = false;
};

std::vector<BrainTaskRecord> loadBrainTasks();
std::vector<BrainTaskRecord> loadBrainTasksCooperative();
std::vector<BrainTaskRecord> priorityBrainTasks(size_t limit = 3, int* openCount = nullptr);
bool setBrainTaskDone(const String& id, bool done);
int openBrainTaskCount();
void clearBrainTaskState();
bool applyBrainTaskCommand(const String& speech, String& response, int& sourceNote);

void invalidateBrainTaskCache();
bool brainTaskReadComplete();

std::vector<BrainTaskRecord> brainTaskSourcesForNote(int noteNum);
bool brainApplyTaskOverlays(std::vector<BrainTaskRecord>& tasks);
void brainPublishTaskCache(const std::vector<BrainTaskRecord>& tasks);
void brainPublishTaskCache(std::vector<BrainTaskRecord>&& tasks);
const std::vector<BrainTaskRecord>& brainTaskSnapshot();
