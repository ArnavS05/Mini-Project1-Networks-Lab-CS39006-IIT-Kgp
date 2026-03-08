CC     = gcc
CFLAGS = -Wall

TARGET = initksocket

$(TARGET): initksocket.c ../include/ksocket.h ../lib/libksocket.a
	$(CC) $(CFLAGS) -I../include -o $@ initksocket.c -L../lib -lksocket -lpthread

clean:
	rm -f $(TARGET)
