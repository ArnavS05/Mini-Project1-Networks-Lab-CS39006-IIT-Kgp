CC      = gcc
CFLAGS  = -Wall
AR      = ar
ARFLAGS = rcs

LIB = libksocket.a
OBJ = ksocket.o

$(LIB): $(OBJ)
	$(AR) $(ARFLAGS) $@ $^

$(OBJ): ksocket.c ../include/ksocket.h
	$(CC) $(CFLAGS) -I../include -c ksocket.c -o $@

clean:
	rm -f $(OBJ) $(LIB)
