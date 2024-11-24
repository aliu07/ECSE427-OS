#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "shellmemory.h"
#include <stdbool.h>
#include "shell.h"
#include "pcb.h"
#include "ready_queue.h"
#include <pthread.h>
#include "lru_tracker.h"

#define FRAME_SIZE 3 // Our frame sizes are defined to be 3 lines/frame

typedef struct {
    char *script_name;
    int base_address;
    int length;
    int page_table[MAX_PAGE_TABLE_SIZE];
} Script;
// Store an array of up to 3 unique scripts... we will use this struct when encountering duplicate scripts
Script *scripts[3];
// Keep track of the number of unique scripts we have (i.e. ignoring duplicates)
int num_unique_scripts = 0;

int MAX_ARGS_SIZE = 7; //CHANGED THIS TO 7 FOR THE 5 INPUTS
int PCB_PID = 1; // We have to give a unqiue pid for each pcb

// Leaving next_memory_location_index since its still used for backround mode (not necessary for A3)
int next_memory_location_index = 0; // This var says where the NEXT memory location should be since we want "run()" to be contiguous


// For MT management
int mt_mode = 0; // Flag for MT mode
pthread_mutex_t threads_created_mutex = PTHREAD_MUTEX_INITIALIZER;
int threads_created = 0; // Flag to signal that threads have been created
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex for queue operations
pthread_cond_t queue_not_empty = PTHREAD_COND_INITIALIZER; // Condition var to signal that queue is not empty
pthread_cond_t all_done = PTHREAD_COND_INITIALIZER; // Condition var to signal that all work is done (for quit)
pthread_t worker1, worker2; // Worker threads
pthread_mutex_t num_active_threads_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex for num of active threads
int num_active_threads = 0; // Represents count of worker threads processing programs



// This is a helper function to find the first free frame in memory.
// The first free frame will have a pointer of NULL.
int find_free_frame(char *store[]) {
    // We compute the total number of frames in our store to know how many we need to peek
    // We divide our value framesize specifying the number of lines in the store by
    // the size of each frame (3 lines in our case) to obtain our final result.
    int total_num_frames = framesize / FRAME_SIZE;

    for (int i = 0; i < total_num_frames; i++) {
        // We peek the first entry of each frame, if entry is NULL, then the whole frame is free
        int frame_first_entry_address = i * 3;
        if (store[i * 3] == NULL) {
            return i;
        }
    }

    // If no free slots in memory, return -1
    return -1;
}

// This helper function intakes the frame number we want to evict
// and prints the contents of the frame.
void evict_frame(int frame_number) {
    for (int i = 0; i < FRAME_SIZE; i++) {
        // Calculate the memory address using the frame number
        // Frame number * 3 since 3 lines/frame
        // Then, add offset i
        int mem_address = frame_number * FRAME_SIZE + i;
        printf("%s", frame_store[mem_address]);
        // Release allocated memory
        if (frame_store[mem_address] != NULL) {
            free(frame_store[mem_address]);
            frame_store[mem_address] = NULL; // To avoid dangling ptr
        }
    }
}

// This helper function loads a new frame into the frame store at the specified frame number.
// It calculates which instructions to load based on the file name and program counter given to it.
void load_frame_into_store(int frame_number, char *progname, int program_counter) {
    // Open the file
    FILE *p = fopen(progname, "rt");
    // Buffer to hold the line we are trying to read
    char line[MAX_USER_INPUT];

    if (p == NULL) {
        printf("Bad command: File not found\n");
        return;
    }

    // We will skip all lines of the file up to PC
    for (int i = 0; i < program_counter; i++) {
        // If we reach EOF before PC, then close the file and exit
        // THIS SHOULD NEVER HAPPEN!
        // But just in case to prevent crashing...
        if (fgets(line, sizeof(line), p) == NULL) {
            fclose(p);
            return;
        }
    }

    int lines_read = 0;

    while (lines_read < 3 && fgets(line, sizeof(line), p) != NULL) {
        // We calculate the physical address to load the line into
        // Here, the base is the frame number multiplied by the frame size
        // Our lines_read variable essentially acts as the offset
        int mem_addr = frame_number * FRAME_SIZE + lines_read;
        frame_store[mem_addr] = (char *)malloc(MAX_USER_INPUT * sizeof(char));
        strcpy(frame_store[mem_addr], line);
        lines_read++;
    }

    // Finally, loading count as access, so update the LRU tracker
    update_frame_access(frame_number);
}

int badcommand() {
    printf("Unknown Command\n");
    return 1;
}

// For run command only
int badcommandFileDoesNotExist(){
    printf("Bad command: File not found\n");
    return 3;
}

int is_str_alnum(char *str) {
    int i = 0;

    while (str[i] != '\0') {
        int res = isalnum(str[i]);

        if (res == 0) {
            return 1;
        }

        i++;
    }

    return 0;
}

int compare(const void *a, const void *b) {
    const char *fa = *(const char **)a;
    const char *fb = *(const char **)b;
    int res = strcasecmp(fa, fb);

    if (res == 0) {
        return strcmp(fa, fb);
    }

    return res;
}

void* worker_thread(void *arg) {
    // Need to cast policy back from void pointer
    char *policy = (char *) arg;

    // Use similar logic to scheduler to define logic of each worker thread
    // Since they only cover RR and RR30 policies, logic is less complex
    while (1) {
        PCB *current_PCB_process = NULL;

        pthread_mutex_lock(&queue_mutex);
        while (head == NULL) {
            // Check if all work has been completed
            pthread_mutex_lock(&num_active_threads_mutex);
            if (num_active_threads == 0) {
                pthread_cond_broadcast(&all_done);
                // Release locks
                pthread_mutex_unlock(&num_active_threads_mutex);
                pthread_mutex_unlock(&queue_mutex);
                // End the thread once all work is done
                return NULL;
            }
            pthread_mutex_unlock(&num_active_threads_mutex);
            // If not, wait for work to be available
            pthread_cond_wait(&queue_not_empty, &queue_mutex);
        }

        // Peek head and release mutex
        current_PCB_process = popFromQueue();
        pthread_mutex_unlock(&queue_mutex);

        if (current_PCB_process == NULL) {
            continue;
        }

        // We successfully popped a process, increment num of active threads
        pthread_mutex_lock(&num_active_threads_mutex);
        num_active_threads++;
        pthread_mutex_unlock(&num_active_threads_mutex);

        // Reset RR_counter for this process
        int RR_counter = 0;

        // Execute instructions from PCB
        while (current_PCB_process->program_counter < (current_PCB_process->base_address + current_PCB_process->length)) {
            char *current_instruction = frame_store[current_PCB_process->program_counter];
            parseInput(current_instruction);
            current_PCB_process->program_counter++;

            // Handle RR & RR30 policies
            if (strcmp(policy, "RR") == 0) {
                RR_counter++;
                if (RR_counter >= 2 && current_PCB_process->program_counter <
                    (current_PCB_process->base_address + current_PCB_process->length)) {
                        // Lock mutex before adding PCB back to queue
                        pthread_mutex_lock(&queue_mutex);
                        addToQueue(current_PCB_process);
                        pthread_mutex_unlock(&queue_mutex);
                        // Broadcast to sleeping threads that there is new work to be done
                        pthread_cond_broadcast(&queue_not_empty);
                        // Decrement number of active threads
                        pthread_mutex_lock(&num_active_threads_mutex);
                        num_active_threads--;
                        pthread_mutex_unlock(&num_active_threads_mutex);
                        break;
                }
            } else if (strcmp(policy, "RR30") == 0) {
                RR_counter++;
                if (RR_counter >= 30 && current_PCB_process->program_counter <
                    (current_PCB_process->base_address + current_PCB_process->length)) {
                        // Lock mutex before adding PCB back to queue
                        pthread_mutex_lock(&queue_mutex);
                        addToQueue(current_PCB_process);
                        pthread_mutex_unlock(&queue_mutex);
                        // Broadcast to sleeping threads that there is new work to be done
                        pthread_cond_broadcast(&queue_not_empty);
                        // Decrement number of active threads
                        pthread_mutex_lock(&num_active_threads_mutex);
                        num_active_threads--;
                        pthread_mutex_unlock(&num_active_threads_mutex);
                        break;
                }
            }
        }

        // Clean up memory if process is complete
        if (current_PCB_process->program_counter >=
            (current_PCB_process->base_address + current_PCB_process->length)) {

            // Decrement number of active threads
            pthread_mutex_lock(&num_active_threads_mutex);
            num_active_threads--;
            pthread_mutex_unlock(&num_active_threads_mutex);
            free(current_PCB_process);
        }
    }

    return NULL;
}

int help();
int quit();
int set(char* var, char* value);
int print(char* var);
int run(char* script);
int exec(char* prog1, char* prog2, char* prog3, char* policy, int background_mode, int mt_mode);
int echo(char* var);
int my_ls(void);
int my_mkdir(char* dirname);
int my_touch(char* filename);
int my_cd(char* dirname);

// Interpret commands and their arguments
int interpreter(char* command_args[], int args_size) {
    int i;

    // Check input size
    if ((args_size < 1 || args_size > MAX_ARGS_SIZE)) {
        printf("Bad command: Too many tokens\n");
        return 1;
    }

    for (i = 0; i < args_size; i++) { // terminate args at newlines
        command_args[i][strcspn(command_args[i], "\r\n")] = 0;
    }

    if (strcmp(command_args[0], "help") == 0){
        // help
        if (args_size != 1) {
            return badcommand();
        }

        return help();

    } else if (strcmp(command_args[0], "quit") == 0) {
        // quit
        if (args_size != 1) {
            return badcommand();
        }

        return quit();

    } else if (strcmp(command_args[0], "set") == 0) {
        // set
        char full_word[MAX_USER_INPUT] = "";

        for (int i = 2; i < args_size; i++) {
            if (i != 2) {
                strcat(full_word, " ");
            }
            strcat(full_word, command_args[i]);
        }

        return set(command_args[1], full_word);

    } else if (strcmp(command_args[0], "print") == 0) {
        // print
        if (args_size != 2) {
            return badcommand();
        }

        return print(command_args[1]);

    } else if (strcmp(command_args[0], "run") == 0) {
        // run
        if (args_size != 2) {
            return badcommand();
        }

        return run(command_args[1]);

    } else if (strcmp(command_args[0], "exec") == 0) {
        // exec
        // Requires at least 2 arguments (exec prog1 POLICY)
        // At most 7 arguments (exec prog1 prog 2 prog3 POLICY # MT)
        if (args_size < 3 || args_size > 7) {
            return badcommand();
        }

        // Init prog2, prog3, and background flag
        char *prog1 = command_args[1]; // prog1 always going to be next token following "exec"
        char *prog2 = NULL;
        char *prog3 = NULL;
        char *policy = NULL;

        // We will parse from right to left because of optional inputs
        int current_position = args_size - 1;

        // Check if last arg is MT
        if (strcmp(command_args[current_position], "MT") == 0) {
            mt_mode = 1; // Toggle MT mode flag
            current_position--;
        }

        // Check if background mode arg present
        if (current_position >= 3 && strcmp(command_args[current_position], "#") == 0) {
            // Background mode flag already captured by shell.c, so simply skip
            current_position--;
        }

        // Next arg should be POLICY
        if (current_position >= 2) {
            policy = command_args[current_position];
            current_position--;
        }

        // Check for prog3
        if (current_position >= 2) {
            prog3 = command_args[current_position];
            current_position--;
        }

        // Check for prog2
        if (current_position >= 2) {
            prog2 = command_args[current_position];
            current_position--;
        }

        return exec(prog1, prog2, prog3, policy, background_mode, mt_mode);

    } else if (strcmp(command_args[0], "echo") == 0) {
        // echo
        if (args_size != 2) {
            return badcommand();
        }

        return echo(command_args[1]);

    } else if (strcmp(command_args[0], "my_ls") == 0) {
        // my_ls
        if (args_size != 1) {
            return badcommand();
        }

        return my_ls();

    } else if (strcmp(command_args[0], "my_mkdir") == 0) {
        // my_mkdir
        if (args_size != 2) {
            return badcommand();
        }

        return my_mkdir(command_args[1]);

    } else if (strcmp(command_args[0], "my_touch") == 0) {
        // my_touch
        if (args_size != 2) {
            return badcommand();
        }

        if (is_str_alnum(command_args[1]) != 0) {
            return badcommand();
        }

        return my_touch(command_args[1]);

    } else if (strcmp(command_args[0], "my_cd") == 0) {
        // my_cd
        if (args_size != 2) {
            return badcommand();
        }

        if (is_str_alnum(command_args[1]) != 0) {
            return badcommand();
        }

        return my_cd(command_args[1]);

    } else {
        // invalid command
        return badcommand();
    }
}

int help() {
    // note the literal tab characters here for alignment
    char help_string[] = "COMMAND			DESCRIPTION\n \
help			Displays all the commands\n \
quit			Exits / terminates the shell with “Bye!”\n \
set VAR STRING		Assigns a value to shell memory\n \
print VAR		Displays the STRING assigned to VAR\n \
run SCRIPT.TXT		Executes the file SCRIPT.TXT\n";
    printf("%s\n", help_string);
    return 0;
}

int quit() {
    printf("Bye!\n");

    //backup for freeing (if ever we get here and smt needs to be freed that isnt it will get freed)
    while (head != NULL) {
        PCB *pcb = popFromQueue();
        if (pcb != NULL) {
            free(pcb);
        }
    }

    // Flush out our frame store
    for (int i = 0; i < framesize; i++) {
        if (frame_store[i] != NULL) {
            free(frame_store[i]);
            frame_store[i] = NULL; // To avoid dangling ptr
        }
    }

    // Free memory allocated to LRU tracker
    destroy_lru_tracker();

    pthread_mutex_lock(&threads_created_mutex);
    if (mt_mode && threads_created) {
        pthread_mutex_unlock(&threads_created_mutex);
        pthread_mutex_lock(&num_active_threads_mutex);

        // Wait until there are no active threads
        while (num_active_threads > 0) {
            pthread_cond_wait(&all_done, &num_active_threads_mutex);
        }

        pthread_mutex_unlock(&num_active_threads_mutex);

        // Wait for threads to finish
        pthread_join(worker1, NULL);
        pthread_join(worker2, NULL);

        // Clean up synchronization primitives
        pthread_mutex_destroy(&queue_mutex);
        pthread_mutex_destroy(&num_active_threads_mutex);
        pthread_cond_destroy(&queue_not_empty);
        pthread_cond_destroy(&all_done);
    }

    exit(0);
}

int set(char *var, char *value) {
    char *link = "=";

    /* PART 1: You might want to write code that looks something like this.
         You should look up documentation for strcpy and strcat.

    char buffer[MAX_USER_INPUT];
    strcpy(buffer, var);
    strcat(buffer, link);
    strcat(buffer, value);
    */

    mem_set_value(var, value);

    return 0;
}

int print(char *var) {
    printf("%s\n", mem_get_value(var));
    return 0;
}


PCB* create_shell_program(char remaining_input[][MAX_USER_INPUT], int count) {
    // Create a new PCB to hold remaining shell program commands
    PCB *shell_PCB = (PCB *)malloc(sizeof(PCB));
    // In case malloc fails, we will return NULL (will be captured by caller)
    if (shell_PCB == NULL) {
        return NULL;
    }

    // Set attributes of new PCB following exec
    int initial_position = next_memory_location_index;
    shell_PCB->pid = PCB_PID++;
    shell_PCB->base_address = initial_position;
    shell_PCB->program_counter = initial_position;

    // Copy remaining commands to memory
    for (int i = 0; i < count; i++) {
        frame_store[next_memory_location_index] = strdup(remaining_input[i]);
        next_memory_location_index++;
    }

    shell_PCB->length = count;
    shell_PCB->job_length_score = 0; // Give shell highest priority i.e. shortest job length score

    // Toggle background mode back to off after creating PCB for rest of batch program
    // This prevents any unexpected behaviour from treating future exec statements as
    // background tasks too
    background_mode = 0;

    return shell_PCB;
}

Script *get_current_script(char *script_name) {
    // Iterate through unique scripts to find a match
    for (int i = 0; i < num_unique_scripts; i++) {
        if (strcmp(scripts[i]->script_name, script_name) == 0) {
            return scripts[i];
        }
    }
    // If we do not find a match, return NULL
    return NULL;
}

void RR_scheduler(int slice_size) {
    while (1) {
        PCB *current_process = popFromQueue();
        int RR_counter = 0;

        // If popped PCB is null, then we have reached end of execution and can terminate
        if (current_process == NULL) {
            break;
        }

        while (1) {
            // Compute components of physical memory using program counter:
            // - Floor divide PC by 3 to get index of frame in PCB's page table
            // - Use calculated index to access physical frame number
            // - Compute offset within frame by taking PC and doing modulo 3 operation
            int frame_index = current_process->program_counter / 3;
            Script *current_script = get_current_script(current_process->script_name);
            int frame_number = current_script->page_table[frame_index];
            int offset = current_process->program_counter % 3;

            // Once we have finished executing 2 commands, put PCB back at end of queue
            // Check time slice before checking for invalid entry to avoid prematurely launching a page fault.
            if (RR_counter >= slice_size) {
                addToQueue(current_process);
                break;
            }

            // Encountered a page fault if the PCB's entry value at current frame index is -1
            // since we have defined -1 to be an invalid page entry.
            if (frame_number == -1) {
                // We need to check if there is space in the frame store or if we have to evict
                // a victime page...
                int free_frame_number = find_free_frame(frame_store);
                // Case where there is space
                if (free_frame_number != -1) {
                    printf("Page fault!\n");
                    // Load the new frame into store
                    load_frame_into_store(free_frame_number, current_process->script_name, current_process->program_counter);
                    // Update the page table of the current script
                    current_script->page_table[frame_index] = free_frame_number;
                } else { // Have to evict a victim page...
                    printf("Page fault! Victim page contents:\n\n");
                    // Determine the LRU frame number and evict it
                    int lru_frame_num = get_lru_frame();
                    evict_frame(lru_frame_num);
                    printf("\nEnd of victim page contents.\n");
                    // Then, we need to bring in new frame based on current PC
                    load_frame_into_store(lru_frame_num, current_process->script_name, current_process->program_counter);
                    // Update the page table of the current script
                    current_script->page_table[frame_index] = lru_frame_num;
                }

                // Put back at end of ready queue after handling page fault interrupt and break
                addToQueue(current_process);
                break;
            } else {
                // Update LRU tracker for most recently accessed frame
                update_frame_access(frame_number);
            }

            // Fetch the current instruction we want to execute by calculating the physical memory address
            // Calculate physical address by taking frame number multiplied by 3 (since 3 lines/frame) and then adding offset
            int physical_address = frame_number * 3 + offset;
            char *instruction = frame_store[physical_address];
            int errCode;

            // Error checking that instruction is not null
            if (instruction != NULL) {
                // Execute the instruction
                errCode = parseInput(instruction);
            } else {
                printf("Error: Null command encountered at PC = %d, physical address = %d.\n", current_process->program_counter, physical_address);
            }

            // Increment PC
            current_process->program_counter++;
            // Increment RR counter as well
            RR_counter++;

            // Check if program has finished executing all of its instructions
            if (current_process->program_counter >= current_process->length) {
                // printf("Free called by %s since PC = %d, length = %d\n", current_process->script_name, current_process->program_counter, current_process->length);
                free(current_process);
                break;
            }
        }
    }
}

void FCFS_scheduler() {
    while (1) {
        PCB *current_process = popFromQueue();

        if (current_process == NULL) {
            break;
        }

        while (1) {
            int frame_index = current_process->program_counter / 3;
            Script *current_script = get_current_script(current_script->script_name);
            int frame_number = current_script->page_table[frame_index];
            int offset = current_process->program_counter % 3;

            int physical_address = frame_number * 3 + offset;
            char *instruction = frame_store[physical_address];
            int errCode;

            // Error checking
            if (instruction != NULL) {
                errCode = parseInput(instruction);
            } else {
                printf("Error: Null command encountered at PC = %d.\n", current_process->program_counter);
            }

            // Increment PC
            current_process->program_counter++;

            if (current_process->program_counter >= current_process->length) {
                free(current_process);
                break;
            }
        }
    }
}

// The create_process_PCB function is the same logic that was given to us in "run", but we wanted to reuse the code for
// the scheduler so we created a function we could call. This avoids code reuse.
PCB* create_process_PCB(char *script) {
    // We loop through the different scripts we have loaded so far
    for (int i = 0; i < num_unique_scripts; i++) {
        if (scripts[i] != NULL && strcmp(scripts[i]->script_name, script) == 0) {
            PCB *PCB_process = (PCB *)malloc(sizeof(PCB));

            if (PCB_process == NULL) {
                printf("Failure to allocate memory for PCB\n");
                return NULL;
            }

            // Assign a new pid for each PCB_process and increase global pcb pid value for the next one
            PCB_process->pid = PCB_PID++;
            PCB_process->script_name = script;
            PCB_process->base_address = scripts[i]->base_address;
            PCB_process->length = scripts[i]->length;
            PCB_process->job_length_score = scripts[i]->length;
            PCB_process->program_counter = 0;
            PCB_process->next = NULL;

            return PCB_process;
        }
    }

    // If not an identical script, we continue as usual...
    // The program is in a file
    FILE *p = fopen(script, "rt");

    if (p == NULL) {
        printf("Bad command: File not found\n");
        return NULL;
    }

	PCB *PCB_process = (PCB *)malloc(sizeof(PCB));

	if (PCB_process == NULL) {
        printf("Failure to allocate memory for PCB\n");
        fclose(p);
        return NULL;
    }

	// Init counters for PCB fields
	// We find the beginning position to be the base address of the first free frame in memory
	int process_beginning_position = find_free_frame(frame_store) * FRAME_SIZE;
	// We will increment this for each page we allocate for the process
	int page_index = 0;
	// We create an array to hold page table mappings
	int page_table[MAX_PAGE_TABLE_SIZE];
	// We create an array of instructions of size 3 since our frames our of size 3
    char *instructions[FRAME_SIZE];

    // For loop only goes up to 2 because we only allocate first 2 pages of the program
    // ASSUMPTION: There will always be enough space in memory for first 2 pages of all program args
    for (int c = 0; c < 2; c++) {
        int instructions_read_count = 0;

        for (int i = 0; i < FRAME_SIZE; i++) {
            // Allocate memory for each instruction we want to store in frame
            instructions[i] = malloc(MAX_USER_INPUT * sizeof(char));
            // Error check memory allocation
            if (instructions[i] == NULL) {
                printf("Error allocating memory for instructions.\n");
                return NULL;
            }
            // When we reach end of file, free allocated memory and break
            if (fgets(instructions[i], MAX_USER_INPUT-1, p) == NULL) {
                free(instructions[i]);
                instructions[i] = NULL; // Set ptr to NULL to avoid dangling ptr
                break;
            }

            instructions_read_count++;
        }

        // If no instructions were read, then EOF has been reached
        if (instructions_read_count == 0) {
            // Free ptrs in our array
            for (int j = 0; j < FRAME_SIZE; j++) {
                if (instructions[j] != NULL) {
                    free(instructions[j]);
                    instructions[j] = NULL;
                }
            }

            break;
        }

        // Calculate physical base address of free frame in memory
        int frame_number = find_free_frame(frame_store);
        // Update process page table and increment the page count field
        page_table[page_index] = frame_number;
        page_index++;

        // Process instructions read from file
        for (int i = 0; i < FRAME_SIZE; i++) {
            if (instructions[i] != NULL && strlen(instructions[i]) > 0) {
                int mem_addr = frame_number * FRAME_SIZE + i;
                frame_store[mem_addr] = (char *)malloc(MAX_USER_INPUT * sizeof(char));
                strcpy(frame_store[mem_addr], instructions[i]);
            }
        }

        // Update LRU tracker with most recently accessed frame
        update_frame_access(frame_number);

        // Reset instruction pointers back to NULL for next iteration
        for (int i = 0; i < FRAME_SIZE; i++) {
            if (instructions[i] != NULL) {
                free(instructions[i]);
                instructions[i] = NULL;
            }
        }
    }

    // We calculate the total length of the file here by opening the file with a new file ptr
    FILE *fp = fopen(script, "rt");

    if (fp == NULL) {
        printf("Bad command: File not found\n");
        return NULL;
    }

    int file_length = 0;
    char buffer[MAX_USER_INPUT];

    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        file_length++;
    }

    fclose(fp);

    // Populate rest of page table with invalid entries
    for (int i = page_index; i < MAX_PAGE_TABLE_SIZE; i++) {
        page_table[i] = -1;
    }

    // Assign a new pid for each PCB_process and increase global pcb pid value for the next one
    PCB_process->pid = PCB_PID++;
    PCB_process->script_name = script;
    PCB_process->base_address = process_beginning_position;
    PCB_process->length = file_length;
    PCB_process->job_length_score = file_length;
    PCB_process->program_counter = 0;
    PCB_process->next = NULL;

    // Create new entry in our scripts array
    scripts[num_unique_scripts] = (Script *)malloc(sizeof(Script));

    if (scripts[num_unique_scripts] == NULL) {
        printf("Error allocating memory for new script entry.\n");
        fclose(p);
        return NULL;
    }

    scripts[num_unique_scripts]->script_name = script;
    scripts[num_unique_scripts]->base_address = process_beginning_position;
    scripts[num_unique_scripts]->length = file_length;
    // The reason why we copy the script after creating the PCB is so that the page table is populated
    // with the proper entries i.e. valid frame numbers + -1's for all invalid frame entries
    memcpy(scripts[num_unique_scripts]->page_table, page_table, sizeof(int) * MAX_PAGE_TABLE_SIZE);
    // Put new script in array and increment the number of unique scripts
    num_unique_scripts++;

    // Close the file
    fclose(p);
    return PCB_process;
}


int run(char *script) {
    PCB *PCB_process = create_process_PCB(script);

    if (PCB_process == NULL) {
        // Don't need to print error msg here because we already printed
        // error logs in create_process_PCB
        return 1;
    }

    addToQueue(PCB_process); //add the current pcb process to the ready queue

    // Since all of our demand paging logic is in the RR scheduler function... why not just call it the RR scheduler function
    // We provide a time slice equal to the length of the program as input to give the illusion of a regular scheduler
    // processing the entire program at once.
    RR_scheduler(PCB_process->length);
    return 0;
}

int exec(char *prog1, char *prog2, char *prog3, char *policy, int background_mode, int mt_mode) {
    int errCode = 0;
    //validate the that the policy value is one of the accepted types
    if (strcmp(policy, "FCFS") != 0 &&
        strcmp(policy, "SJF") != 0 &&
        strcmp(policy, "RR") != 0 &&
        strcmp(policy, "AGING") != 0 &&
        strcmp(policy, "RR30") != 0) {
        printf("Error: Invalid scheduling policy\n");
        quit();
    }

    if (background_mode) {
        // Create PCB for remaining shell commands
        PCB *shell_pcb = create_shell_program(remaining_input, remaining_input_count);

        if (shell_pcb != NULL) {
            // Add shell PCB to queue
            addToQueue(shell_pcb);
        }
    }

    char *programs[] = {prog1, prog2, prog3}; //programs array has the three programs so we can loop through them
    PCB *PCB_processes[3] = {NULL, NULL, NULL}; //declaring PCB array (makes it easier to add them to queue one by one by looping through them)
    for (int i = 0; i < 3; i++) { //this loop builds a PCB process by calling the create_process_PCB function on it.
        if (programs[i] != NULL) {
            PCB_processes[i] = create_process_PCB(programs[i]);
        }
    }

	//in the case where the policy is SJF or AGING, we need our queue to be sorted by length, wo we will do that here with bubble sort
    if (strcmp(policy, "SJF") == 0 || strcmp(policy, "AGING") == 0) {
        PCB *temp_var;
        // Worst case is they are sorted in decreasing order of length -> ex: {3,2,1}
        // Sort using bubble sort so that we can send it to the queue in the right order
        // Outer loop goes up to two, because in the worst case it this loop needs to run twice since there
        // are 3 elements max
        for (int i = 0; i < 2; i++) {
            for (int j = i + 1; j < 3; j++) {
                if (PCB_processes[i] != NULL && PCB_processes[j] != NULL && PCB_processes[i] -> length > PCB_processes[j] -> length) {
                    temp_var = PCB_processes[i];
                    PCB_processes[i] = PCB_processes[j];
                    PCB_processes[j] = temp_var;
                }
            }
        }
    }

    if (mt_mode) {
        // Add PCBs to queue, workers will handle execution
        pthread_mutex_lock(&queue_mutex);
        for (int i = 0; i < 3; i++) {
            if (PCB_processes[i] != NULL) {
                addToQueue(PCB_processes[i]);
            }
        }
        pthread_mutex_unlock(&queue_mutex);

        // Only create threads once by leveraging threads_created flag
        // We might enter this section later if in MT mode, but will not create additional threads
        pthread_mutex_lock(&threads_created_mutex);
        if (!threads_created) {
            pthread_create(&worker1, NULL, worker_thread, (void*)policy);
            pthread_create(&worker2, NULL, worker_thread, (void*)policy);
            threads_created = 1;
        }
        pthread_mutex_unlock(&threads_created_mutex);

        // Return immediately as worker threads handle processing (main thread is not considered as worker)
        return 0;
    } else {
        // Single threaded execution
        //regardless of the policy type, we will add the PCB processes from the array into the queue
        //important to note that the way we have implemented it is that at this point, the queue is in the right order, so the scheduler can just pop the elements in order
        for (int i = 0; i < 3; i++) {
            if (PCB_processes[i] != NULL) {
                addToQueue(PCB_processes[i]);
            }
        }

        if (strcmp(policy, "RR") == 0) {
            RR_scheduler(2);
        } else if (strcmp(policy, "AGING") == 0) {
            // TODO - Implement aging scheduler
            // FCFS scheduler is placeholder
            FCFS_scheduler();
        } else if (strcmp(policy, "SJF") == 0) {
            // We already sorted the programs based on job length score, so simply call FCFS
            // since ready queue is sorted
            FCFS_scheduler();
        } else if (strcmp(policy, "RR30") == 0) {
            RR_scheduler(30);
        } else {
            FCFS_scheduler();
        }

        return 0;
    }
}

int echo(char *var) {
    if (var[0] == '$') {
        var = var + 1; // To get rid of the $ sign for the print and mem_get_value
        print(var);
    } else {
        printf("%s\n", var);
    }

    return 0;
}

int my_ls(void) {
    DIR *dir;
    struct dirent *entry;
    char **filenames = NULL;
    int file_count = 0;
    int capacity = 10; // Initial capacity value
    dir = opendir(".");
    filenames = malloc(capacity * sizeof(char*));

    if (filenames == NULL) {
        closedir(dir);
        return 1; // Memory allocation error
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] != '.') {
            if (file_count >= capacity) {
                capacity *= 2;
                char **tmp = realloc(filenames, capacity * sizeof(char*));

                if (tmp == NULL) {
                    for (int i = 0; i < file_count; i++) {
                        free(filenames[i]);
                    }

                    free(filenames);
                    closedir(dir);
                    return 1; // Memory realloc error
                }

                filenames = tmp;
            }

            filenames[file_count] = strdup(entry->d_name);

            if (filenames[file_count] == NULL) {
                for (int i = 0; i < file_count; i++) {
                    free(filenames[i]);
                }

                free(filenames);
                closedir(dir);
                return 1; // Memory alloc error for filename
            }

            file_count++;
        }
    }

    closedir(dir);
    qsort(filenames, file_count, sizeof(char *), compare);

    for (int i = 0; i < file_count; i++) {
        printf("%s\n", filenames[i]);
        free(filenames[i]);
    }

    free(filenames);
    return 0;
}

int my_mkdir(char *dirname) {
    if (dirname[0] == '$') {
        dirname = dirname + 1;
        char *memValue = mem_get_value(dirname);

        if (is_str_alnum(memValue) != 0) {
            printf("Bad command: my_mkdir\n");
            return 1;
        }

        dirname = memValue;
    }

    if (mkdir(dirname, 0777) == -1) {
        printf("Bad command: my_mkdir\n");
        return 1;
    }

    return 0;
}

int my_touch(char* filename) {
    FILE *fptr;
    fptr = fopen(filename, "w");

    if (fptr == NULL) {
        return 1; // Error creating file
    }

    if (fclose(fptr) != 0) {
        return 1; // Error closing file
    }

    return 0;
}

int my_cd(char* dirname) {
    if (chdir(dirname) != 0) {
        printf("Bad command: my_cd\n");
        return 1;
    }

    return 0;
}
