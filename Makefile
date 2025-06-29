all:
	gcc -Wall -c common.c
	gcc -Wall client.c common.o -o client
	gcc -Wall server-mt.c common.o -lpthread -lm -o server-mt

clean:
	rm -f common.o client server-mt