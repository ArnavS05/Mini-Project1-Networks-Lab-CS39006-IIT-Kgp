CC     = gcc
CFLAGS = -Wall

all: user1 user2

user1: user1.c ../include/ksocket.h ../lib/libksocket.a
	$(CC) $(CFLAGS) -I../include -o user1 user1.c -L../lib -lksocket

user2: user2.c ../include/ksocket.h ../lib/libksocket.a
	$(CC) $(CFLAGS) -I../include -o user2 user2.c -L../lib -lksocket

clean:
	rm -f user1 user2
