PROG=	bunyan-rotate
SRCS=	rotate.c
MAN=	bunyan-rotate.8

CFLAGS+=-Wall -Werror
DEBUG=-g

DPADD=	${LIBZ} ${LIBUTIL} ${LIBCRYPTO}
LDADD=	-lz -lutil -lcrypto

BINDIR=/opt/local/bin

.include <bsd.prog.mk>
