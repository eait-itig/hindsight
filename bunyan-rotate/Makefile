PROG=	bunyan-rotate
SRCS=	rotate.c
MAN=

CFLAGS+=-Wall -Werror
DEBUG=-g

DPADD=	${LIBZ} ${LIBUTIL}
LDADD=	-lz -lutil

BINDIR=/opt/local/bin

.include <bsd.prog.mk>
