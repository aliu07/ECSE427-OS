#include <stdlib.h>
#include "lru_tracker.h"

LRUTracker *tracker;

void lru_init(int num_frames) {
    // Allocate memory for LRU tracker
    tracker = (LRUTracker *)malloc(sizeof(LRUTracker));
    tracker->head = NULL;
    tracker->tail = NULL;
    tracker->num_frames = num_frames;
    tracker->frames = (LRUNode **)calloc(num_frames, sizeof(LRUNode*));

    // We will initialize all frames in order
    // Order here doesn't matter as we will rearrange when
    // 1) Loading 1st 2 pages of each program
    // 2) Accessing frames subsequently
    for (int i = 0; i < num_frames; i++) {
        LRUNode *node = (LRUNode *)malloc(sizeof(LRUNode));
        node->frame_number = i;

        // Add to the tail of the list
        // Case if list is empty
        if (tracker->tail == NULL) {
            tracker->head = tracker->tail = node;
        } else { // Case where list is not empty
            node->prev = tracker->tail;
            node->next = NULL;
            tracker->tail->next = node;
            tracker->tail = node;
        }

        tracker->frames[i] = node;
    }
}

// This function returns the least recently used frame number
int get_lru_frame() {
    return tracker->tail->frame_number;
}

// This function updates the LRU tracker when a frame is accessed
void update_frame_access(int frame_number) {
    if (frame_number >= tracker->num_frames) return;

    LRUNode* node = tracker->frames[frame_number];

    // If already at head, nothing to do
    if (node == tracker->head) return;

    // Remove from current position
    if (node->prev) node->prev->next = node->next;
    if (node->next) node->next->prev = node->prev;

    // If it's the tail, update tail
    if (node == tracker->tail) {
        tracker->tail = node->prev;
    }

    // Move to head
    node->next = tracker->head;
    node->prev = NULL;
    tracker->head->prev = node;
    tracker->head = node;
}

// Function used for cleanup in quit()
void destroy_lru_tracker() {
    LRUNode* current = tracker->head;

    while (current) {
        LRUNode* next = current->next;
        free(current);
        current = next;
    }

    free(tracker->frames);
    free(tracker);
}
