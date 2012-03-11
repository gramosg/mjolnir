CC = clang
CFLAGS = -Wall -Wextra -std=c99 -pedantic -O3
LDFLAGS = -lm
OBJECTS = mjolnir.o
TARGET = mjolnir
PREFIX = /usr/local/bin

release: $(TARGET)

install: release
	mkdir -p $(PREFIX)
	cp $(TARGET) $(PREFIX)/$(TARGET)
	chmod 755 $(PREFIX)/$(TARGET)

clean:
	rm -f *.o *~ core tags $(TARGET)

tags:
	ctags *
