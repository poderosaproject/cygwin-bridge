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
	rm -f versioninfo.res resource.h $(TARGET)

resource.h: getversion.sh
	bash getversion.sh $(TARGET) > resource.h

versioninfo.res: versioninfo.rc resource.h
	windres -DUNICODE -D_UNICODE --output-format=coff $< $@

$(TARGET): cygwin-bridge.c versioninfo.res
	gcc -Wall -std=c11 -O2 -s -mwindows -D_DEFAULT_SOURCE -D_GNU_SOURCE $(OPTS) -o $@ $^
