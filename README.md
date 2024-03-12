# Recursive Read/Write Locks

Rewrite read/write locks that follow below properties to gain a deeper understanding of its internal and application:

1. When reader thread is executing in the critical section, the other reader threads can enter the critical section as well.

2. When writer thread is executing in the critical section, no other reader or writer thread are allowed to enter the critical section.

3. The lock supports the property of recursiveness, same thread can grab the lock multiple times if required.

4. When the lock is released, let O.S. scheduling policy decides which waiting thread should enter the critical section next.

5. Cause the assertion failure if a thread tries to unlock already-unlocked Read/Write lock, or tries to unlock a lock held by some other thread.
