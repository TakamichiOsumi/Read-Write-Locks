CC	= gcc
CFLAGS	= -O0 -Wall
PROGRAM	= executable

$(PROGRAM):
	$(CC) $(CFLAGS) test_rw_locks.c rw_locks.c -o $@

.PHONY: clean

clean:
	rm -rf $(PROGRAM)

test: $(PROGRAM)
	@./$(PROGRAM) && echo "Successful if the result is zero >>> $$?"
