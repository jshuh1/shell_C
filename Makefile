CFLAGS = -g3 -Wall -Wextra -Wconversion -Wcast-qual -Wcast-align -g
CFLAGS += -Winline -Wfloat-equal -Wnested-externs
CFLAGS += -pedantic -std=gnu99 -Werror

PROMPT = -DPROMPT

all:
	33sh 33noprompt

33sh:
	gcc -Wl,--wrap=write $(CFLAGS) -o 33sh $(PROMPT) sh.c 
	#TODO: compile your program, including the -DPROMPT macro

33noprompt:
	gcc -Wl,--wrap=write $(CFLAGS) -o 33noprompt sh.c
	#TODO: compile your program without the prompt macro

clean:
	rm -f 33sh 33noprompt
	#TODO: clean up any executable files that this Makefile has produced

