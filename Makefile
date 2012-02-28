CC=gcc
CFLAGS=-O3
LDFLAGS=-pthread
OBJECTS=htserve.o

all: htserve

clean :
	rm *.o htserve

fifo-test: $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@
	
.c.o:
	$(CC) -c $(CFLAGS) $< -o $@

