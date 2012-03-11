CC = clang
CFLAGS = -Wall -Wextra -std=c99 -pedantic -O3
LDFLAGS = -lm
OBJECTS = mjolnir.o
TARGET = mjolnir

release: $(TARGET)

%.o: %.c
	$(CC) -c $(CFLAGS) $<

$(TARGET): $(OBJECTS)

install: release
	echo "Installing... (not implemented)"

clean:
	rm -f *.o *~ core tags $(TARGET)

tags:
	ctags *
