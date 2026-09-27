CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
CPPFLAGS ?= -Iinclude
LDFLAGS ?= -Llib -Wl,-rpath,'$$ORIGIN/lib'
LDLIBS ?= -ltts

.PHONY: all clean

all: dectalk-tune

dectalk-tune: src/dectalk-tune.c include/dtk/ttsapi.h include/dtk/dtmmedefs.h lib/libtts.so
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ src/dectalk-tune.c $(LDFLAGS) $(LDLIBS)

clean:
	rm -f dectalk-tune
