CC = gcc
CFLAGS = -O2 -Wall -I ..

all: adder

adder: adder.c ../csapp.o
	$(CC) $(CFLAGS) -o adder adder.c ../csaap.o

echo: echo.c ../csapp.o
	$(CC) $(CFLAGS) -o echo echo.c ../csaap.o

clean:
	rm -f adder echo *~
