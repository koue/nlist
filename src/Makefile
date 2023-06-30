#
PROG=		nlist

CFLAGS+=	-Werror \
		-I/usr/local/include
LDFLAGS+=	-L/usr/local/lib
LDADD=		-lqueue -lcezmisc -lpool -lrender

MAN=

MK_DEBUG_FILES=	no

MAKEOBJDIR=	.

install:

test:

.include <bsd.prog.mk>
