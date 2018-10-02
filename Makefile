#
SUBDIR=		src

LOCALBASE=	/var/www
DATADIR=	/opt/koue.chaosophia.net/nlist
WEBDIR=		/htdocs/koue.chaosophia.net
TMPDIR=		/tmp
LIBEXECDIR=	/libexec
CGI=		index.cgi

.if exists(src/nlist)
CGILDD=		ldd src/nlist | grep "=>" | cut -d ' ' -f 3

CGIlibs=	${CGILDD:sh}
.endif

chroot:
	mkdir -p $(LOCALBASE)$(LIBDIR)
	mkdir -p $(LOCALBASE)$(DATADIR)/data
	mkdir -p $(LOCALBASE)$(WEBDIR)
	mkdir -p $(LOCALBASE)$(CONFDIR)
	mkdir -p $(LOCALBASE)$(LIBEXECDIR)
	mkdir -p $(LOCALBASE)$(TMPDIR)
	cp /libexec/ld-elf.so.1 $(LOCALBASE)$(LIBEXECDIR)/
.	for l in ${CGIlibs}
	cp -f ${l} $(LOCALBASE)$(LIBDIR)/
.	endfor

install:
	rm -rf $(LOCALBASE)$(DATADIR)/html
	rm -rf $(LOCALBASE)$(WEBDIR)/css
	rm -rf $(LOCALBASE)$(DATADIR)/nlist.conf
	cp src/nlist $(LOCALBASE)$(WEBDIR)/$(CGI)
	cp -r etc/nlist.conf html $(LOCALBASE)$(DATADIR)
	cp -r css $(LOCALBASE)$(WEBDIR)
	chown -R www:www $(LOCALBASE)$(DATADIR)/

update:
	cp src/nlist $(LOCALBASE)$(WEBDIR)/$(CGI)

test:
	chroot -u www -g www $(LOCALBASE) $(WEBDIR)/$(CGI)

#testquery:
#	QUERY_STRING='/action/submit' chroot -u www -g www $(LOCALBASE) $(WEBDIR)/$(CGI)
.include <bsd.subdir.mk>
