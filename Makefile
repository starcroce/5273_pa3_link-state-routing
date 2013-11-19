CC=gcc
CFLAGS= -O1 -Wall
all:
	$(CC) $(CFLAGS) routed_LS.c -o routed_LS

router:
	$(CC) $(CFLAGS) routed_LS.c -o routed_LS

clean:
	$(RM) routed_LS
