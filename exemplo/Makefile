#
# Makefile para programas exemplo
#

CC=gcc
LIB_DIR=../lib

all: clean teste

teste: teste.c $(LIB_DIR)/libt2fs.a
	$(CC) -o teste teste.c -L$(LIB_DIR) -lt2fs -Wall

clean:
	rm -rf teste t2shell *.o *~
