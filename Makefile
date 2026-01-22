CC = gcc
CFLAGS = -Wall -O2 $(shell gdal-config --cflags)
LDFLAGS = $(shell gdal-config --libs)
TARGET = gdal_test
CLANG_FORMAT ?= clang-format

FORMAT_FILES = gdal_test.c

all: $(TARGET)

$(TARGET): gdal_test.c
	$(CC) $(CFLAGS) -o $(TARGET) gdal_test.c $(LDFLAGS)

clean:
	rm -f $(TARGET) *.o

format:
	$(CLANG_FORMAT) -i $(FORMAT_FILES)

.PHONY: all clean format
