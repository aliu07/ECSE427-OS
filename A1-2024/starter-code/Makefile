CC=gcc
CFLAGS=-D X=$(framesize) -D Y=$(varmemsize)

mysh: shell.c interpreter.c shellmemory.c
	$(CC) $(CFLAGS) -c shell.c interpreter.c shellmemory.c ready_queue.c lru_tracker.c
	$(CC) $(CFLAGS) -o mysh shell.o interpreter.o shellmemory.o ready_queue.o lru_tracker.o

clean:
	rm mysh; rm *.o
