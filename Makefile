#
# $FreeBSD: head/tools/tools/netmap/Makefile 260700 2014-01-16 00:20:42Z luigi $
#
# For multiple programs using a single source file each,
# we can just define 'progs' and create custom targets.
PROGS	=	pkt-gen bridge vale-ctl testpcap tee libnetmap.so

CLEANFILES = $(PROGS) pcap.o nm_util.o
NO_MAN=
CFLAGS += -Werror -Wall -nostdinc -I/usr/include -I../../../sys
CFLAGS += -Wextra
#CFLAGS += -Wextra -DNO_SWAP
#CFLAGS += -Wextra -g

LDFLAGS += -lpthread
.ifdef WITHOUT_PCAP
CFLAGS += -DNO_PCAP
.else
LDFLAGS += -lpcap
.endif

.include <bsd.prog.mk>
.include <bsd.lib.mk>

all: $(PROGS)

pkt-gen bridge tee: nm_util.o
	$(CC) $(CFLAGS) -o ${.TARGET} ${.TARGET:=.c} nm_util.o $(LDFLAGS)

testpcap: pcap.c libnetmap.so
	$(CC) $(CFLAGS) -DTEST -L. -lnetmap -o ${.TARGET} pcap.c
	
libnetmap.so:	pcap.c nm_util.c
	$(CC) $(CFLAGS) -fpic -c ${.ALLSRC}
	$(CC) -shared -o ${.TARGET} ${.ALLSRC:.c=.o}
