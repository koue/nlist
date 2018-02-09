CC=cc -Wall -Wstrict-prototypes -g -O0 -static
CHROOT=/data/www
DATA=/opt/nlist
WEB=/koue.chaosophia.net
CGI=index.cgi

all: nlist

nlist: nlist.c
	$(CC) -o nlist nlist.c -lz /usr/lib/libcezconfig.a

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
