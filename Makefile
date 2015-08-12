all: piserver piclient daemon

piserver:
	gcc -o piserver piserver.c -lpthread

piclient:
	gcc -o piclient piclient.c

daemon:
	gcc -o piclientdaemon piclientdaemon.c -lpthread

clean:
	rm piserver piclient piclientdaemon
