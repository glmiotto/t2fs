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
CFLAGS=-Wall -g
OBJS = $(LIB_DIR)/apidisk.o $(LIB_DIR)/bitmap2.o
name = t2fs
TARGET = $(BIN_DIR)/$(name).o
PROG = $(SRC_DIR)/$(name).c

LIB=$(LIB_DIR)/lib$(name).a

all: $(TARGET)
	ar -crs $(LIB) $^ $(OBJS)

$(TARGET): $(PROG)
		$(CC) -o $@ $< -I$(INC_DIR) $(CFLAGS)

$(BIN_DIR):
	mkdir $(BIN_DIR)

tar:
		@cd .. && tar -zcvf 287677_207758_299902.tar.gz $(name)

clean:
	mv $(OBJS) ../
	rm -rf $(LIB_DIR)/*.a $(BIN_DIR)/*.o $(SRC_DIR)/*~ $(INC_DIR)/*~ *~
	mv ../apidisk.o $(LIB_DIR)
	mv ../bitmap2.o $(LIB_DIR)
