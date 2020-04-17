.POSIX:
CC     = gcc
CFLAGS = -Wall -Wextra -O3 -g3

FILE_DIR = our_cs244b_test_13245646

all: redirect redir_filter install

redirect: redirect.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ redirect.c

install: redirect
	cp -r $(FILE_DIR) /tmp

redir_filter: redir_filter.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ redir_filter.c

clean:
	rm -f redirect redir_filter 	
	rm -rf /tmp/$(FILE_DIR)

