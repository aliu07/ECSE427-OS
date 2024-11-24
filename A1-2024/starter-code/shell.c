#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "shell.h"
#include "interpreter.h"
#include "shellmemory.h"
#include "lru_tracker.h"

int parseInput(char ui[]);

// Use this to store remaining commands
char remaining_input[1000][MAX_USER_INPUT];
int remaining_input_count = 0;
int background_mode = 0;

// Start of everything
int main(int argc, char *argv[]) {
    printf("Frame Store Size = %d; Variable Store Size = %d\n", framesize, varmemsize);
    // help();

    char prompt = '$';  				// Shell prompt
    char userInput[MAX_USER_INPUT];		// user's input stored here
    int errorCode = 0;					// zero means no error, default

    //init user input
    for (int i = 0; i < MAX_USER_INPUT; i++) {
        userInput[i] = '\0';
    }

    //init shell memory
    mem_init();
    // Compute number of frames by taking framesize obtained at compile-time and dividing by 3
    // GIVEN ASSUMPTION -> frame size will always be multiple of 3
    int num_frames = framesize / 3;
    // init LRU tracker
    lru_init(num_frames);

    while(1) {
        // if we enter this, we are in interactive mode so we show the $
        if (isatty(STDIN_FILENO)) {
            printf("%c ", prompt);
        }

        // here you should check the unistd library
        // so that you can find a way to not display $ in the batch mode
        if (fgets(userInput, MAX_USER_INPUT - 1, stdin) == NULL) {
            break;
        }

        // If current command has a '#' symbol, enable background mode
        if (strstr(userInput, "exec") && strstr(userInput, "#")) {

            background_mode = 1;
            char tempInput[MAX_USER_INPUT];

            while (fgets(tempInput, MAX_USER_INPUT - 1, stdin) != NULL) {
                // Only store non-empty lines
                if (strlen(tempInput) > 1) {
                    strcpy(remaining_input[remaining_input_count], tempInput);
                    remaining_input_count++;
                }
            }
        }

        errorCode = parseInput(userInput);

        // ignore all other errors
        if (errorCode == -1) {
            exit(99);
        }

        memset(userInput, 0, sizeof(userInput));
    }

    return 0;
}

int wordEnding(char c) {
    // You may want to add ';' to this at some point,
    // or you may want to find a different way to implement chains.
    return c == '\0' || c == '\n' || c == ' ' || c == ';';
}

int parseInput(char inp[]) {
    char tmp[200], *words[100];
    int ix = 0, w = 0, commandStart = 0;
    int wordlen;
    int errorCode;

    while (inp[ix] != '\0' && ix < 1000) {
        // Skip leading whitespace
        while (inp[ix] == ' ' && ix < 1000) {
            ix++;
        }

        // Check for blank command
        if (inp[ix] == ';' || inp[ix] == '\n' || inp[ix] == '\0') {
            if (ix == commandStart) {
                errorCode = badcommand();
            } else if (w > 0) {
                errorCode = interpreter(words, w);
            }

            // Reset for next command
            w = 0;

            for (int i = 0; i < 100; i++) {
                words[i] = NULL;
            }

            ix++;
            commandStart = ix;
            continue;
        }

        // Extract a word
        for (wordlen = 0; !wordEnding(inp[ix]) && ix < 1000; ix++, wordlen++) {
            tmp[wordlen] = inp[ix];
        }

        tmp[wordlen] = '\0';
        words[w] = strdup(tmp);
        w++;
    }

    // Handle the last command if it doesn't end with a semicolon
    if (w > 0) {
        errorCode = interpreter(words, w);
    } else if (commandStart < ix) {
        // This handles the case where the last command is blank
        errorCode = badcommand();
    }

    return errorCode;
}
