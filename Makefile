
DESTDIR ?=
PREFIX = /usr

CC ?= gcc
CFLAGS += -std=gnu99 -Wall -Wextra -Wshadow -Werror -fvisibility=hidden
LDFLAGS += -Wl,--no-undefined

ifeq ($(DEBUG),1)
CFLAGS += -O0 -g -DDEBUG
else
CFLAGS += -O2 -DNDEBUG
endif

all: sysex-ttymidi sysex-ttymidi.so

debug:
	$(MAKE) DEBUG=1

sysex-ttymidi: src/sysex-ttymidi.c src/mod-semaphore.h
	$(CC) $< $(CFLAGS) $(shell pkg-config --cflags --libs jack) $(LDFLAGS) -lpthread -o $@

sysex-ttymidi.so: src/sysex-ttymidi.c src/mod-semaphore.h
	$(CC) $< $(CFLAGS) $(shell pkg-config --cflags --libs jack) $(LDFLAGS) -fPIC -lpthread -shared -o $@

install: sysex-ttymidi sysex-ttymidi.so
	install -m 755 ttymidi    $(DESTDIR)$(PREFIX)/bin/
	install -m 755 ttymidi.so $(DESTDIR)$(shell pkg-config --variable=libdir jack)/jack/

clean:
	rm -f sysex-ttymidi sysex-ttymidi.so

uninstall:
	rm $(DESTDIR)$(PREFIX)/bin/ttymidi
	rm $(DESTDIR)$(shell pkg-config --variable=libdir jack)/jack/sysex-ttymidi.so
