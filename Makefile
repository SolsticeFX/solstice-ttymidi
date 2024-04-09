
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

all: solstice-ttymidi solstice-ttymidi.so

debug:
	$(MAKE) DEBUG=1

solstice-ttymidi: src/solstice-ttymidi.c src/mod-semaphore.h
	$(CC) $< $(CFLAGS) $(shell pkg-config --cflags --libs jack) $(LDFLAGS) -lpthread -o $@

solstice-ttymidi.so: src/solstice-ttymidi.c src/mod-semaphore.h
	$(CC) $< $(CFLAGS) $(shell pkg-config --cflags --libs jack) $(LDFLAGS) -fPIC -lpthread -shared -o $@

install: solstice-ttymidi solstice-ttymidi.so
	install -m 755 solstice-ttymidi    $(DESTDIR)$(PREFIX)/bin/
	install -m 755 solstice-ttymidi.so $(DESTDIR)$(shell pkg-config --variable=libdir jack)/jack/

clean:
	rm -f solstice-ttymidi solstice-ttymidi.so

uninstall:
	rm $(DESTDIR)$(PREFIX)/bin/solstice-ttymidi
	rm $(DESTDIR)$(shell pkg-config --variable=libdir jack)/jack/solstice-ttymidi.so
