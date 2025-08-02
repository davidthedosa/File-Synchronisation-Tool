CC = gcc
CFLAGS = -Wall -O2
TARGETS = server1 client1

all: $(TARGETS)

server1: tcp_server.c
	$(CC) $(CFLAGS) tcp_server.c -o server1

client1: tcp_client.c
	$(CC) $(CFLAGS) tcp_client.c -o client1

run-server:
	./server1

run-client:
	./client1

clean:
	rm -f server1 client1 sync_log.txt
