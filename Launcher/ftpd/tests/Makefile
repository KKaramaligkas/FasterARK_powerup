# Host tests for the launcher's FTP server.
#   sudo apt-get install libmbedtls-dev libgnutls28-dev curl lftp
#   make -C Launcher/ftpd/tests check

CC      ?= gcc
CFLAGS  ?= -O1 -g -Wall -Wextra -Wno-unused-parameter -std=gnu99 -fsanitize=address,undefined
SERVER   = ../ftpd.c ../tls.c sys_host.c server.c

.PHONY: all check clean

all: check

server: $(SERVER) ../ftpd.h ../sys.h ../tls.h
	$(CC) $(CFLAGS) -I.. -o $@ $(SERVER) -lmbedtls -lmbedx509 -lmbedcrypto -lpthread

fzclient: fzclient.c
	$(CC) $(CFLAGS) -o $@ fzclient.c -lgnutls

check: server fzclient
	python3 test_ftpd.py

clean:
	rm -f server fzclient
