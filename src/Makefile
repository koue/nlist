#
PROG=		nlist

CFLAGS+=	-Werror \
		-I/usr/local/include
LDFLAGS+=	-L/usr/local/lib
LDADD=		-lz -lcezqueue -lcezmisc -lcezcorepool

MAN=

MK_DEBUG_FILES=	no

MAKEOBJDIR=	.

install:

test:

.include <bsd.prog.mk>
