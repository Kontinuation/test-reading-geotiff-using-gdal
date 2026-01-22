CC = gcc
CFLAGS = -Wall -O2 $(shell gdal-config --cflags)
LDFLAGS = $(shell gdal-config --libs)
TARGET = gdal_test
TARGET_LIFETIME = gdal_vrt_lifetime_test
ASAN_CFLAGS = -g -O1 -fsanitize=address -fno-omit-frame-pointer
CLANG_FORMAT ?= clang-format

FORMAT_FILES = gdal_test.c gdal_vrt_lifetime_test.c

all: $(TARGET) $(TARGET_LIFETIME)

$(TARGET): gdal_test.c
	$(CC) $(CFLAGS) -o $(TARGET) gdal_test.c $(LDFLAGS)

$(TARGET_LIFETIME): gdal_vrt_lifetime_test.c
	$(CC) $(CFLAGS) -o $(TARGET_LIFETIME) gdal_vrt_lifetime_test.c $(LDFLAGS)

$(TARGET_LIFETIME)_asan: gdal_vrt_lifetime_test.c
	$(CC) $(CFLAGS) $(ASAN_CFLAGS) -o $(TARGET_LIFETIME)_asan gdal_vrt_lifetime_test.c $(LDFLAGS)

clean:
	rm -f $(TARGET) $(TARGET_LIFETIME) $(TARGET_LIFETIME)_asan *.o

format:
	$(CLANG_FORMAT) -i $(FORMAT_FILES)

.PHONY: all clean format
