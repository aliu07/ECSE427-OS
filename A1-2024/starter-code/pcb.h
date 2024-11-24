#ifndef PCB_H //added include guards cuz i had  errors since pcb was used in many files
#define PCB_H
// Define a max of 40 pages for the PCB's page table...
// This is more than enough (max 100 lines = 34 pages -> 40 > 34)
#define MAX_PAGE_TABLE_SIZE 40

typedef struct PCB {
    int pid;
    char *script_name;
    int base_address;
    int length;
    int job_length_score;
    int program_counter;
    struct PCB *next; //this is to point to the next one so it can run one after the other
} PCB;

#endif
