CFLAGS = -O2 -DHAVE_INTTYPES_H -Wall -g

BIN = mkv3update
SRCS = main.c md5.c
OBJS = $(patsubst %.c,%.o,$(SRCS))

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) -o $@ $(LDFLAGS) $^ $(LOADLIBES) $(LDLIBS)

clean:
	rm -f $(BIN) $(OBJS)

