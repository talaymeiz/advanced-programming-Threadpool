.PHONY: all
all: task stdinExample coder

task:	codec.h basic_main.c 
	gcc basic_main.c -L. -l Codec -o encoder

stdinExample:	stdin_main.c
	gcc stdin_main.c -L. -l Codec -o tester

coder: coder.c
	gcc coder.c -L. -l Codec -o coder -lpthread

.PHONY: clean
clean:
	-rm encoder tester libCodec.so 2>/dev/null
