# Author: strawberryhacker

flags += -std=c11
flags += -Wall -Wno-unused-function -Wno-unused-variable

.PHONY: all
all: 
	@$(CC) $(flags) main.c disk.c exfat.c -o main
	@./main test/filesystem
	@rm main
