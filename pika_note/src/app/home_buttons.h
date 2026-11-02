#pragma once
#include <stdint.h>
bool pikaHomeButtonsBegin();
void pikaHomeButtonsEnable(bool enabled);
bool pikaHomeButtonsBusy();
bool pikaHomeButtonsTake(int& slot);
void pikaHomeButtonsReset();

uint32_t pikaHomeButtonsDispatchDelay();

// Simple screen presses are retained independently of the owner/RPC loop.
void pikaScreenButtonsEnable(bool enabled);
unsigned pikaScreenButtonsPending();
unsigned pikaScreenButtonsTake();

bool pikaHomeButtonsForegroundGrace();

void pikaHomeButtonsPrintTrace();
