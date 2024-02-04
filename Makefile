CC	= gcc
CFLAGS	= -O0 -Wall
PROGRAM1	= exec_basic_tests
PROGRAM2	= exec_advanced_tests
OUTPUT_LIB	= librw_lock.a

all: $(PROGRAM1) $(PROGRAM2) $(OUTPUT_LIB)

$(PROGRAM1): rw_locks.o
	$(CC) $(CFLAGS) test_rw_locks.c $^ -o $@

$(PROGRAM2): rw_locks.o
	$(CC) $(CFLAGS) test_rw_locks_assertion.c $^ -o $@

rw_locks.o:
	$(CC) $(CFLAGS) rw_locks.c -c

$(OUTPUT_LIB): rw_locks.o
	ar rs $@ $<

.PHONY: clean test

clean:
	rm -rf $(PROGRAM1) $(PROGRAM2) $(OUTPUT_LIB) rw_locks.o

test: $(PROGRAM1) $(PROGRAM2)
	@./$(PROGRAM1) && echo "Successful when the result is zero >>> $$?"
	@./$(PROGRAM2) && echo "Successful when the result is zero >>> $$?"
