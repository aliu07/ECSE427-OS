// To implement our LRU eviction policy, we will keep the accessed frames in a doubly-linked list.
// - head = most recently accessed
// - tail = least recently accessed

// This way, whenever we need to evict, we simply pop from the tail.
// And whenever we access a frame, we take its node and put it in the head position.

typedef struct LRUNode {
    int frame_number;
    struct LRUNode *prev;
    struct LRUNode *next;
} LRUNode;

typedef struct {
    // Actual size of the LRU cache
    int num_frames;
    // Keep an array of ptrs to nodes for efficient lookups
    LRUNode** frames;
    // Store head and tail of doubly-linked list
    LRUNode* head;
    LRUNode* tail;
} LRUTracker;

void lru_init(int num_frames);
int get_lru_frame();
void update_frame_access(int frame_number);
void destroy_lru_tracker();
