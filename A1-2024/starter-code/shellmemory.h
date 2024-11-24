#define MEM_SIZE 1000

// Default values if store sizes are not provided at compile-time
#ifndef X
#define X 200
#endif

#ifndef Y
#define Y 20
#endif

// Define variables to hold sizes defined at compile-time
extern int framesize;
extern int varmemsize;
// Define 2 memory stores
// // Another store for frames i.e. user data of size X
// One store for variables of size Y
extern char *frame_store[X];

void mem_init();
char *mem_get_value(char *var);
void mem_set_value(char *var, char *value);
