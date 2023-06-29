CC=gcc
CFLAGS=-Wall -fPIC -Werror -ggdb -Og
LIBS=-lpulse
TARGET=libpulse_dotnet.so
SRC=pulse_dotnet.c

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -shared -o $@ $^ $(LIBS)

clean:
	rm -f $(TARGET)
