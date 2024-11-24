#include "ready_queue.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include "interpreter.h"

PCB *head = NULL;
PCB *tail = NULL;

void addToQueue(PCB *process) {
    if (tail == NULL) {
        head = process; //if the tail is null, then it is the first process so we make it the head and the tail
        tail = process;
        tail->next = NULL;
    } else {
        tail->next = process; //if its not the only one, then we add the process as tail to the queue
        tail = process;
        tail->next = NULL;
    }
}


PCB* popFromQueue() {
    //if the head is null, there is nothig in the queue so we return NULL
    if (head == NULL) {
        return NULL;
    }

    PCB *process = head;
    if (head -> next == NULL) { //if the head's next is null, then once popped there is nothing left since head was the only one in queue
        tail = NULL; //since there should be nothing left in the queue, we set the tail and head to null
        head = NULL;
    } else { //if there is something after the head, we set the head to the next element
        head = head -> next;
    }

    return process;
}

// Given head of ready queue, sort the ready queue by job length score and return new head
// New head = job with lowest job length score
void ageReadyQueue(PCB *current_process) {
    PCB *rqueue_arr[2] = {NULL, NULL};
    int ix = 0;

    // Append the rest of the processes (if applicable) in order of ready queue
    while (head != NULL && ix < 2) {
        rqueue_arr[ix++] = popFromQueue();
    }

    // Age all processes in the ready queue
    for (int i = 0; i < 2; i++) {
        if (rqueue_arr[i] != NULL && rqueue_arr[i]->job_length_score > 0) {
            rqueue_arr[i]->job_length_score--;
        }
    }

    PCB *temp_var;
    // Sort all processes in increasing job length score
    if (rqueue_arr[0] != NULL && rqueue_arr[1] != NULL && rqueue_arr[0]->job_length_score > rqueue_arr[1]->job_length_score) {
        temp_var = rqueue_arr[0];
        rqueue_arr[0] = rqueue_arr[1];
        rqueue_arr[1] = temp_var;
    }

    // Rebuild queue
    if (current_process != NULL && rqueue_arr[0] != NULL && current_process->job_length_score <= rqueue_arr[0]->job_length_score) {
        // If current process still has lowest job score, then it keeps its position
        head = current_process;
        tail = current_process;

        for (int i = 0; i < 2; i++) {
            if (rqueue_arr[i] != NULL) {
                rqueue_arr[i]->next = NULL; // To prevent circular references
                addToQueue(rqueue_arr[i]);
            }
        }
    } else {
        head = NULL;
        tail = NULL;
        int count = 0;
        for (int i = 0; i < 2; i++) {
            if (rqueue_arr[i] != NULL) {
                count++;
                rqueue_arr[i]->next = NULL; // Prevents circular references
                addToQueue(rqueue_arr[i]);
            }
        }

        // Append current process to end of queue
        if (current_process != NULL) {
            addToQueue(current_process);
        }
    }

    return;
}
