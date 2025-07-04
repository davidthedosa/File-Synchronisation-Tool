# Makefile for TCP client-server sync project

CC = gcc
CFLAGS = -Wall

all: server client

server: tcp_server.c
	$(CC) $(CFLAGS) tcp_server.c -o server

client: tcp_client.c
	$(CC) $(CFLAGS) tcp_client.c -o client

clean:
	rm -f server client test.txt received_file.txt
