CFLAGS ?= -Wall -Wextra -std=c99

ifeq ($(OS),Windows_NT)
	LDFLAGS = -lgdi32
else
	UNAME_S := $(shell uname -s)
	ifeq ($(UNAME_S),Darwin)
		LDFLAGS = -framework Cocoa -framework AudioToolbox
	else
		LDFLAGS = -lX11
	endif
endif

hop: hop.c fenster.h fenster_audio.h
	$(CC) hop.c -o $@ $(CFLAGS) $(LDFLAGS)

clean:
	rm -f hop

.PHONY: clean
