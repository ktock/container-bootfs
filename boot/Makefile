CC = gcc
CFLAGS = -O0 -g -Wall -Wextra -static
BOOT_BIN = boot
DBCLIENT_Y_BIN = dbclient_y

all: $(BOOT_BIN) $(DBCLIENT_Y_BIN)

$(BOOT_BIN): boot.c parson/parson.c
	$(CC) $(CFLAGS) -o $@ $^

$(DBCLIENT_Y_BIN): dbclient_y.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(BOOT_BIN) $(DBCLIENT_Y_BIN)

