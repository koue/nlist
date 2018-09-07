#
CFLAGS+=	-Wall -Wstrict-prototypes -g -O0 -static -I/usr/local/include
CHROOT=		/var/www
DATA=		/opt/koue.chaosophia.net/nlist
WEB=		/htdocs/koue.chaosophia.net
CGI=		index.cgi

all: nlist

nlist: nlist.c
	$(CC) $(CFLAGS) -o nlist nlist.c -lz /usr/local/lib/libcezconfig.a

clean:
	rm -f nlist *.core

install:
	rm -rf $(CHROOT)$(DATA)/html
	rm -rf $(CHROOT)$(WEB)/css
	rm -rf $(CHROOT)$(DATA)/nlist.conf
	mkdir -p $(CHROOT)$(WEB)/
	cp nlist $(CHROOT)$(WEB)/$(CGI)
	mkdir -p $(CHROOT)$(DATA)/
	cp -r nlist.conf html $(CHROOT)$(DATA)
	cp -r css $(CHROOT)$(WEB)
	chown -R www:www $(CHROOT)$(DATA)/

test:
	chroot -u www -g www $(CHROOT) $(WEB)/$(CGI)

#testquery:
#	QUERY_STRING='/action/submit' chroot -u www -g www $(CHROOT) $(WEB)/$(CGI)
