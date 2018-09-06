#
CFLAGS+=	-Wall -Wstrict-prototypes -g -O0 -static -I/usr/local/include
CHROOT=		/usr/local/www/nlist
DATA=		/data
WEB=		/www
CGI=		index.cgi

all: nlist

nlist: nlist.c
	$(CC) $(CFLAGS) -o nlist nlist.c -lz /usr/local/lib/libcezconfig.a

clean:
	rm -f nlist *.core

install:
	rm -rf $(CHROOT)$(WEB)/$(CGI)
	rm -rf $(CHROOT)$(DATA)${WEB}/html
	rm -rf $(CHROOT)$(DATA)${WEB}/nlist.conf
	cp nlist $(CHROOT)$(WEB)/$(CGI)
	cp -r nlist.conf html $(CHROOT)$(DATA)${WEB}

test:
	chroot -u www -g www $(CHROOT) $(WEB)/$(CGI)

#testquery:
#	QUERY_STRING='/action/submit' chroot -u www -g www $(CHROOT) $(WEB)/$(CGI)
