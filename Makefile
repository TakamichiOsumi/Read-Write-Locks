CC	= gcc
CFLAGS	= -O0 -Wall
PROGRAM	= executable
OUTPUT_LIB	= librw_lock.a

all: $(PROGRAM) $(OUTPUT_LIB)

$(PROGRAM): rw_locks.o
	$(CC) $(CFLAGS) test_rw_locks.c rw_locks.c -o $@

rw_locks.o:
	$(CC) $(CFLAGS) rw_locks.c -c

$(OUTPUT_LIB): rw_locks.o
	ar rs $@ $<

.PHONY: clean test

clean:
	rm -rf $(PROGRAM) $(OUTPUT_LIB) rw_locks.o

test: $(PROGRAM)
	@./$(PROGRAM) && echo "Successful if the result is zero >>> $$?"
