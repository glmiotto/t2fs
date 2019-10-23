#
# Makefile para programas exemplo
#

CC=gcc
LIB_DIR=../lib

all: clean teste t2shell

teste: teste.c $(LIB_DIR)/libt2fs.a
	$(CC) -o teste teste.c -L$(LIB_DIR) -lt2fs -Wall

t2shell: t2shell.c $(LIB_DIR)/libt2fs.a
	$(CC) -o t2shell t2shell.c -L$(LIB_DIR) -lt2fs -lm -Wall

clean:
	rm -rf teste t2shell *.o *~
