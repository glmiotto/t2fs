#
# Makefile para programas exemplo
#

CC=gcc
LIB_DIR=../lib

all: clean teste tst_map

teste: teste.c $(LIB_DIR)/libt2fs.a
	$(CC) -o teste teste.c -L$(LIB_DIR) -lt2fs -Wall

teste: tst_map.c $(LIB_DIR)/libt2fs.a
	$(CC) -o tst_map tst_map.c -L$(LIB_DIR) -lt2fs -Wall

clean:
	rm -rf teste tst_map t2shell *.o *~
