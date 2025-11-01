# Makefile for Echo Server and Client (CS:APP)

CC = gcc
CFLAGS = -Wall -g
CSAPP = csapp.c

# Targets
all: echo_server echo_client

# 서버 빌드
echo_server: echo_server.c $(CSAPP)
	$(CC) $(CFLAGS) -o echo_server echo_server.c $(CSAPP) -lpthread

# 클라이언트 빌드
echo_client: echo_client.c $(CSAPP)
	$(CC) $(CFLAGS) -o echo_client echo_client.c $(CSAPP) -lpthread

# 청소
clean:
	rm -f echo_server echo_client *.o
