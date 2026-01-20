CC = gcc
CFLAGS = -Wall -O2 $(shell gdal-config --cflags)
LDFLAGS = $(shell gdal-config --libs)
TARGET = gdal_test

all: $(TARGET)

$(TARGET): gdal_test.c
	$(CC) $(CFLAGS) -o $(TARGET) gdal_test.c $(LDFLAGS)

clean:
	rm -f $(TARGET) *.o

.PHONY: all clean
