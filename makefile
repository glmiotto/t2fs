#
# Makefile ESQUELETO
#
# DEVE ter uma regra "all" para geração da biblioteca
# regra "clean" para remover todos os objetos gerados.
#
# NECESSARIO adaptar este esqueleto de makefile para suas necessidades.
#
#
CC=gcc -c
LIB_DIR=./lib
INC_DIR=./include
BIN_DIR=./bin
SRC_DIR=./src
EXMP_DIR=./exemplo
CFLAGS=-Wall -g -std=c99
OBJS = $(LIB_DIR)/apidisk.o $(LIB_DIR)/bitmap2.o
name = t2fs
TARGET = $(BIN_DIR)/$(name).o
PROG = $(SRC_DIR)/$(name).c


LIB=lib$(name).a

all: $(TARGET)
	ar -crs $(LIB) $^ $(OBJS)

$(TARGET): $(PROG)
		$(CC) -o $@ $< -I$(INC_DIR) $(CFLAGS)

$(BIN_DIR):
	mkdir $(BIN_DIR)

shell:
	gcc -o $(EXMP_DIR)/t2shell $(EXMP_DIR)/t2shell.c -L ./ -l$(name) -lm -Wall

test_mbr:
	gcc -o $(EXMP_DIR)/test_mbr $(EXMP_DIR)/test_mbr.c -L ./ -l$(name) -lm -Wall

tst_map:
	gcc -o $(EXMP_DIR)/tst_map $(EXMP_DIR)/tst_map.c -L ./ -l$(name) -lm -Wall
	
op:
	make clean
	make all
	make tst_op

tst_op:
	gcc -o $(EXMP_DIR)/tst_op $(EXMP_DIR)/tst_op.c -L ./ -l$(name) -lm -Wall



tar:
		@cd .. && tar -zcvf 287677_207758_299902.tar.gz $(name)

clean:
	mv $(OBJS) ../
	rm -rf $(LIB_DIR)/*.a $(BIN_DIR)/*.o $(SRC_DIR)/*~ $(INC_DIR)/*~ *~ ./*.a
	mv ../apidisk.o $(LIB_DIR)
	mv ../bitmap2.o $(LIB_DIR)
