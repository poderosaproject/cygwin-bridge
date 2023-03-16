#
# Cygwin bridge for Poderosa
#

.PHONY: all clean

OPTS = 
ifdef TRACE_FILE
OPTS += -DTRACE_FILE=$(TRACE_FILE)
endif

LONG_BIT := $(shell getconf LONG_BIT)
ifeq ($(LONG_BIT),64)
TARGET = cygwin-bridge64.exe
else ifeq ($(LONG_BIT),32)
TARGET = cygwin-bridge32.exe
else
$(error cannot determine target)
endif

all: $(TARGET)

clean:
	rm -f $(TARGET)

$(TARGET): cygwin-bridge.c
	gcc -Wall -std=c11 -O2 -s -mwindows -D_DEFAULT_SOURCE $(OPTS) -o $@ $^
