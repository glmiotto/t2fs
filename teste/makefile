CC=gcc
SRC_FILES = $(wildcard *.c)
TARGETS = ${SRC_FILES:.c= }
CFLAGS =  -m32 -g
SHELL=/bin/bash

.PHONY: all compile run check

all: compile run clean check

run:
	@cd ../;\
	printf "\nRUNNING TESTS:\n";\
	printf "===MOUNT+FORMAT+UMOUNT TEST BEGINS===\n"; ./mount_format_umount_test > ./teste/log/mount_format_umount_test.txt;\
	printf "===OPEN+CLOSE TEST BEGINS===\n"; ./openclosedir_test > ./teste/log/openclosedir_test.txt;\
	printf "===RECORDWRITE TEST BEGINS===\n"; ./recordwrite_test > ./teste/log/recordwrite_test.txt;\
	printf "\nTESTS FINISHED\nCHECK THE LOGS AT teste/log\n";

compile:
	$(CC) $(CFLAGS) -I ../include/ -L ../lib mount_format_umount_test.c -lt2fs -o ../mount_format_umount_test -Wall -lm
	$(CC) $(CFLAGS) -I ../include/ -L ../lib openclosedir_test.c -lt2fs -o ../openclosedir_test -Wall -lm
	$(CC) $(CFLAGS) -I ../include/ -L ../lib recordwrite_test.c -lt2fs -o ../recordwrite_test -Wall -lm


check:
	@var=0;lines=($$(grep -iRl "NOT OK" ./log/));for x in $${lines[@]}; do if [ $$var = 0 ]; then printf "\nERRORS FOUND IN:\n"; fi; var=$$(($$var + 1)); printf "$$x\n"; done; if [ $$var = 0 ]; then printf "\nNO ERRORS WERE FOUND\nALL OK\n"; else printf "\nERRORS WERE FOUND\nCHECK THE LOGS AND CORRECT IT\n"; fi;

clean:
	@rm -rf *.o;	cd ../;	rm $(TARGETS);
