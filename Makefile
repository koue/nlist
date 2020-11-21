#
SUBDIR=		src

MYHEADER=	src/nlist.h
DOMAINCMD=	grep DOMAIN $(MYHEADER) | cut -d '"' -f2
DOMAIN=		${DOMAINCMD:sh}
CHROOTCMD=	grep CHROOT $(MYHEADER) | cut -d '"' -f2
LOCALBASE=	${CHROOTCMD:sh}
DATADIR=	/opt/${DOMAIN}/nlist
WEBDIR=		/htdocs/${DOMAIN}
ETCDIR=		/etc
TMPDIR=		/tmp
LIBEXECDIR=	/libexec
CGI=		index.cgi
TESTTITLE1=	My first title
TESTLINE1=	My first line
TESTTITLE2=	My second title
TESTLINE2=	My second line
VALGRINDCMD=	valgrind -q --tool=memcheck --leak-check=yes --show-leak-kinds=all --num-callers=20
MYUSERCMD=	grep MYUSER $(MYHEADER) | cut -d '"' -f 2
MYUSER=		${MYUSERCMD:sh}
MYGROUPCMD=	grep MYGROUP $(MYHEADER) | cut -d '"' -f 2
MYGROUP=	${MYGROUPCMD:sh}
# TESTS
TESTCMD=	chroot -u $(MYUSER) -g $(MYGROUP) $(LOCALBASE) $(WEBDIR)/$(CGI)
LONG=		iamveryverylongqueryxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

.if exists(src/nlist)
CGILDD=		ldd src/nlist | grep "=>" | cut -d ' ' -f 3

CGIlibs=	${CGILDD:sh}
.endif

chroot:
	mkdir -p $(LOCALBASE)$(LIBDIR)
	mkdir -p $(LOCALBASE)$(DATADIR)/data
	mkdir -p $(LOCALBASE)$(WEBDIR)
	mkdir -p $(LOCALBASE)$(ETCDIR)
	mkdir -p $(LOCALBASE)$(LIBEXECDIR)
	mkdir -p $(LOCALBASE)$(TMPDIR)
	cp /libexec/ld-elf.so.1 $(LOCALBASE)$(LIBEXECDIR)/
.	for l in ${CGIlibs}
	cp -f ${l} $(LOCALBASE)$(LIBDIR)/
.	endfor

install:
	rm -rf $(LOCALBASE)$(DATADIR)/html
	rm -rf $(LOCALBASE)$(WEBDIR)/css
	rm -rf $(LOCALBASE)$(ETC)/nlist.conf
	cp src/nlist $(LOCALBASE)$(WEBDIR)/$(CGI)
	cp etc/nlist.conf $(LOCALBASE)$(ETCDIR)/
	sed -i.bak "s/%%DOMAIN%%/$(DOMAIN)/g" $(LOCALBASE)$(ETCDIR)/nlist.conf
	cp -r html $(LOCALBASE)$(DATADIR)/
	cp -r css $(LOCALBASE)$(WEBDIR)/
	chown -R $(MYUSER):$(MYGROUP) $(LOCALBASE)$(DATADIR)/

update:
	cp src/nlist $(LOCALBASE)$(WEBDIR)/$(CGI)

first:
	printf "$(TESTTITLE1)\n$(TESTLINE1)" > $(LOCALBASE)$(DATADIR)/data/first.txt
	printf "$(TESTTITLE2)\n$(TESTLINE2)" > $(LOCALBASE)$(DATADIR)/data/second.txt

test:	chroot install first
	# default html
	QUERY_STRING='' $(TESTCMD) > tests/test.file
	diff -q -I 'Posted on <time datetime' tests/default.html tests/test.file
	# first item
	QUERY_STRING='/first.html' $(TESTCMD) > tests/test.file
	diff -q -I 'Posted on <time datetime' tests/first.html tests/test.file
	# second item
	QUERY_STRING='/second.html' $(TESTCMD) > tests/test.file
	diff -q -I 'Posted on <time datetime' tests/second.html tests/test.file
	# default rss
	QUERY_STRING='/rss' $(TESTCMD) > tests/test.file
	diff -q -I '<pubDate>' tests/default.xml tests/test.file
	# not exist
	QUERY_STRING='iammissing' $(TESTCMD) > tests/test.file
	diff -q tests/notexist.html tests/test.file
	QUERY_STRING='/iammissing' $(TESTCMD) > tests/test.file
	diff -q tests/notexist.html tests/test.file
	QUERY_STRING='/iammissing.html' $(TESTCMD) > tests/test.file
	diff -q tests/notexist.html tests/test.file
	# long query
	QUERY_STRING='$(LONG)' $(TESTCMD) > tests/test.file
	diff -q tests/longquery.html tests/test.file
	# &amp;
	QUERY_STRING='/startme&amp;here.html' $(TESTCMD) > tests/test.file
	diff -q tests/escaped.html tests/test.file
	# wrong query
	QUERY_STRING='/i_am_wrong-1' $(TESTCMD) > tests/test.file
	diff -q tests/wrongquery.html tests/test.file
	QUERY_STRING='/i_am_wrong#my.html' $(TESTCMD) > tests/test.file
	diff -q tests/wrongquery.html tests/test.file
	QUERY_STRING='/i_am_wrong.my.html' $(TESTCMD) > tests/test.file
	diff -q tests/wrongquery.html tests/test.file
	QUERY_STRING='/i_am_wrong"my.html' $(TESTCMD) > tests/test.file
	diff -q tests/wrongquery.html tests/test.file
	QUERY_STRING='/i_am_wrong`my.html' $(TESTCMD) > tests/test.file
	diff -q tests/wrongquery.html tests/test.file
	QUERY_STRING='/i_am_wrong*my.html' $(TESTCMD) > tests/test.file
	diff -q tests/wrongquery.html tests/test.file
	QUERY_STRING='/i_am_wrong my.html' $(TESTCMD) > tests/test.file
	diff -q tests/wrongquery.html tests/test.file
	QUERY_STRING="/i_am_wrong'my.html" $(TESTCMD) > tests/test.file
	diff -q tests/wrongquery.html tests/test.file
	QUERY_STRING='/i_am_wrong/../etc/hosts' $(TESTCMD) > tests/test.file
	diff -q tests/wrongquery.html tests/test.file
	QUERY_STRING='/i_am_wrong/../etc/hosts.html' $(TESTCMD) > tests/test.file
	diff -q tests/wrongquery.html tests/test.file
	QUERY_STRING='/i_am_wrong/..html' $(TESTCMD) > tests/test.file
	diff -q tests/wrongquery.html tests/test.file
	# remove test file
	rm -f tests/test.file

valgrind: chroot install first
	QUERY_STRING='' $(VALGRINDCMD) ./src/nlist --valgrind | grep "$(TESTTITLE1)"
	QUERY_STRING='$(LONG)' $(VALGRINDCMD) ./src/nlist --valgrind | grep "Status: 400"

.include <bsd.subdir.mk>
