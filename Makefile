.PHONY: all clean
.SUFFIXES:

CFLAGS += -pipe -O2 -D_FILE_OFFSET_BITS=64
override CFLAGS += -Wall -Wextra -Wshadow -Werror -D_POSIX_C_SOURCE=200112L -D_ISOC99_SOURCE -D_SVID_SOURCE -D_BSD_SOURCE

all: uk-dvb-hd-tsfix
clean:
	rm -f uk-dvb-hd-tsfix

uk-dvb-hd-tsfix: uk-dvb-hd-tsfix.c Makefile
	$(CC) $(CFLAGS) -lm -lrt -lrrd -o $@ uk-dvb-hd-tsfix.c
