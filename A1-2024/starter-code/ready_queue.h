#include "pcb.h"
// Pass head and tail in header file in case we need to use as "global variables"
extern PCB *head;
extern PCB *tail;

void addToQueue(PCB *process);
PCB* popFromQueue();
void ageReadyQueue(PCB *current_process);
