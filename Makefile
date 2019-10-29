OBJS = main.o 
SOBJS = libgarrus.so
CC=gcc
DEBUG = -g 
CFLAGS = -Wall $(DEBUG) -pthread 
LDFLAGS = -L$(PWD) 
LIBS = -lgarrus

all: output

output: $(OBJS) $(SOBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

main.o: main.c
	$(CC) $(CFLAGS) -c $<

garrus.o: garrus.c
	$(CC) $(CFLAGS) -c $<

$(SOBJS): garrus.c
	$(CC) -g -Wall -shared -o $(SOBJS) -fPIC $<

test: test.c $(SOBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	$(RM) *.gch *.o *~ output test $(SOBJS)
