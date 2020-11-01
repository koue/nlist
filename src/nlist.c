/*
 * Copyright (c) 2011-2020 Nikola Kolev <koue@chaosophia.net>
 * Copyright (c) 2004-2006 Daniel Hartmeier. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <errno.h>
#include <fts.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include <cez_core_pool.h>
#include <cez_queue.h>
#include <cez_misc.h>

#include "nlist.h"

typedef void (*render_cb)(const char *, const struct entry *);

static void render_main(const char *html_fn, render_cb r, const struct entry *e);
static void render_macro(const char *m, const struct entry *e);
static void render_main_simple(const char *m, const struct entry *e);
static void render_story_list(const char *m, const struct entry *e);
static void render_link(const char *m, const struct entry *e);
static void render_body(const char *m, const struct entry *e);
static void render_date(const char *m, const struct entry *e);
static void render_baseurl(const char *m, const struct entry *e);
static void render_ctype(const char *m, const struct entry *e);
static void render_title(const char *m, const struct entry *e);
static void render_article(const char *m, const struct entry *e);

static const struct {
	const char *name;
	const char *file;
	render_cb render;
} Macro[] = {
	{ "HEADER",		"header.html",	&render_main_simple },
	{ "FOOTER",		"footer.html",	&render_main_simple },
	{ "STORIES_HTML",	"story.html",	&render_story_list },
	{ "STORIES_RSS",	"story.rss",	&render_story_list },
	{ "ARTICLE",		NULL,		&render_article },
	{ "BASEURL",		NULL,		&render_baseurl },
	{ "BODY",		NULL,		&render_body },
	{ "CTYPE",		NULL,		&render_ctype },
	{ "DATE",		NULL,		&render_date },
	{ "LINK",		NULL,		&render_link },
	{ "TITLE",		NULL,		&render_title },
	{ NULL,			NULL,		NULL },
};

static const char *params[] = { "datadir", "htmldir", "logfile", "excludefile",
    "baseurl", "ct_html", NULL };
static const char *valgrindme[] = { "datadir", "htmldir", "logfile", "excludefile",
    NULL };

static struct cez_queue config;
static struct feed feed;

static gzFile	gz = NULL;

static void
msg(const char *fmt, ...)
{
	extern char *__progname;
	FILE *f;
	va_list ap;
	time_t t = time(NULL);
	struct tm *tm = gmtime(&t);

	if ((f = fopen(cqg(&config, "logfile"), "ae")) == NULL) {
		fprintf(stderr, "%s: cannot open logfile: %s\n", __func__,
		    cqg(&config, "logfile"));
		return;
	}
	fprintf(f, "%4.4d.%2.2d.%2.2d %2.2d:%2.2d:%2.2d %s %s %s v%d [%u] ",
	    tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
	    tm->tm_hour, tm->tm_min, tm->tm_sec, getenv("REMOTE_ADDR"), "-" ,
	    __progname, VERSION, (unsigned)getpid());
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fprintf(f, "\n");
	fflush(f);
	fclose(f);
}

static void
feed_add(struct entry *entry)
{
	TAILQ_INSERT_TAIL(&feed.head, entry, item);
}

static int
vf_parent(const char *p, int l, const char *f)
{
	return(((strlen(p) == 0) && (l == 1)) || (strcmp(p, f)) == 0);
}

static int
file_is_excluded(const char *name)
{
	FILE *f;
	char s[8192], *p;

	if (( f = fopen(cqg(&config, "excludefile"), "re")) == NULL) {
		msg("Cannot open exclude file.");
		return (0);
	}
	while (fgets(s, sizeof(s), f)) {
		s[strlen(s) - 1] = 0;
		if ((p = strstr(name, s)) != NULL) {
			fclose(f);
			return (1);
		}
	}
	fclose(f);
	return (0);
}

static int
vf_article(const char *a, const char *fp, const char *fn)
{
	return(((strlen(a) == 0) && (file_is_excluded(fp) == 0)) ||
	    (strcmp(a, fn) == 0));
}

static int
file_is_txt(const char *name)
{
	if ((name[strlen(name)-1] == 't') && (name[strlen(name)-2] == 'x') &&
	    (name[strlen(name)-3] == 't') && (name[strlen(name)-4] == '.')) {
		return (0);
	} else {
		return (-1);
	}
}

static int
file_read(struct pool *pool, struct entry *e)
{
	FILE *f;
	char s[8192];

	if ((f = fopen(e->fn, "re")) == NULL) {
		return (-1);
	}

	/* read first line - article title */
	fgets(s, sizeof(s), f);
	if (s[0]) {
		s[strlen(s) - 1] = 0;
		e->title = pool_strdup(pool, s);
	}

	fclose(f);
	return (0);
}

static struct entry *
file_get_attr(struct pool *pool, FTSENT *fent)
{
	char *pos = fent->fts_name;
	struct entry *current = pool_alloc(pool, sizeof(struct entry));

	/* Make sure cleared out */
	memset(current, 0, sizeof(struct entry));

	current->fn = pool_strdup(pool, fent->fts_path);

	if (file_read(pool, current) == -1) {
		return (NULL);
	}
	/* if article in main directory don't use the parent */
	if (strcmp(fent->fts_parent->fts_name, "data") != 0) {
		current->parent = pool_strdup(pool, fent->fts_parent->fts_name);
	} else {
		current->parent = NULL;
	}
	while (*pos && *pos != '.') {
		pos++;
	}
	*pos = 0;
	current->name = pool_strdup(pool, fent->fts_name);
	current->pubdate = fent->fts_statp->st_mtime;

	return(current);
}

static void
d_printf(const char *fmt, ...)
{
	static char s[65536];
	va_list ap;
	int r;

	va_start(ap, fmt);
	r = vsnprintf(s, sizeof(s), fmt, ap);
	va_end(ap);
	if (r < 0 || r >= sizeof(s)) {
		msg("error d_printf: vsnprintf: r %d (%d)", r, (int)sizeof(s));
	}
	if (gz != NULL) {
		r = gzputs(gz, s);
		if (r != strlen(s)) {
			msg("error d_printf: gzputs: r %d (%d)",
			    r, (int)strlen(s));
		}
	} else {
		fprintf(stdout, "%s", s);
	}
}

static void
render_error(const char *fmt, ...)
{
	va_list ap;
	char s[8192];

	va_start(ap, fmt);
	vsnprintf(s, sizeof(s), fmt, ap);
	va_end(ap);
	printf("%s\r\n\r\n", cqg(&config, "ct_html"));
	fflush(stdout);
	d_printf("<html><head><title>Error</title></head><body>\n");
	d_printf("<h2>Error</h2><p><b>%s</b><p>\n", s);
	d_printf("Time: <b>%s</b><br>\n", rfc822_time(time(0)));
	d_printf("</body></html>\n");
}

static int
compare_name_des_fts(const FTSENT * const *a, const FTSENT * const *b)
{
	return (strcmp((*b)->fts_name, (*a)->fts_name));
}

static void
find_articles(struct pool *pool, const char *path, int size)
{
	FTS *fts;
	FTSENT *e;
	char * const path_argv[] = { (char*)path, NULL };
	int i = 0;
	char *tmp, *pos, article[128], parent[128], query[256];
	struct entry *entry;

	parent[0] = article[0] = 0;

	if ((fts = fts_open(path_argv, FTS_LOGICAL, compare_name_des_fts))
	    == NULL) {
		d_printf("fts_open: %s: %s<br>\n", path, strerror(errno));
		return;
	} else if ((e = fts_read(fts)) == NULL || (e->fts_info != FTS_D)) {
		d_printf("fts_read: fts_info %s: %s<br>\n", path,
							strerror(errno));
		return;
	} else if ((e = fts_children(fts, FTS_NAMEONLY)) == NULL) {
		if (errno != 0) {
			d_printf("fts_children: %s: %s<br>\n", path,
							strerror(errno));
		}
		return;
	}

	if((tmp = getenv("QUERY_STRING")) != NULL) {
		snprintf(query, sizeof(query), "%s", tmp);
		pos = query;
		while ((tmp = strsep(&pos, "/")) != NULL) {
			if(strlen(tmp)) {
				if(strstr(tmp, ".html") != NULL) {
					strlcpy(article, tmp, sizeof(article));
					if ((tmp = strstr(article, ".")) != NULL) {
						*tmp = 0;
					}
					strlcat(article,".txt",sizeof(article));
				}
				else {
					strlcpy(parent, tmp, sizeof(parent));
				}
			}
		}
	}

	/*
	If there is parent but not article selected then show only index.txt
	content. If the main directory should be shown then show all *.txt
	files in the current directory.
	*/
	/* if rss generate rss from main directory */
	if((strncmp(parent, "rss", 3)) == 0) {
		parent[0] = 0;
	}
	if(parent[0] && !article[0]) {
		strlcpy(article, "index.txt", sizeof(article));
	}
	while ((( e = fts_read(fts)) != NULL) && (i < size)) {
		if ((e->fts_info == FTS_F)
		    && vf_parent(parent, e->fts_level, e->fts_parent->fts_name)
		    && vf_article(article, e->fts_path, e->fts_name)
		    && (file_is_txt(e->fts_name) == 0 )) {
			if ((entry = file_get_attr(pool, e)) == NULL) {
				continue;
			} else {
				feed_add(entry);
			}
			i++;
		}
	}
	fts_close(fts);
}

void
http_accept_encoding(void)
{
	const char *s;

	if ((s = getenv("HTTP_ACCEPT_ENCODING")) != NULL) {
		char *p = strstr(s, "gzip");
		if (p != NULL && ((strncmp(p, "gzip;q=0", 8) != 0) ||
		    strtol(p + 7, (char **)NULL, 10) > 0.0)) {
			gz = gzdopen(fileno(stdout), "wb9");
			if (gz == NULL) {
				msg("error main: gzdopen");
			} else {
				printf("Content-Encoding: gzip\r\n");
			}
		}
	}
}

static int
http_query_check(const char *s)
{
	if (strlen(s) > 64) {
		printf("Status: 400\r\n\r\n You are trying to send very "
		    "long query!\n");
		fflush(stdout);
		return (-1);
	}

	if (strstr(s, "&amp;") != NULL) {
		msg("warning main: escaped query '%s'", s);
		printf("Status: 400\r\n\r\nHTML escaped ampersand in cgi "
		    "query string \"%s\"\n", s);
		fflush(stdout);
		return (-1);
	}

	/*
	Sanity check of the query string, accepts only alpha,
	'/' and '_' and '.' if its on 5 position before the end of the string

	Correct: /follow/this/path/
		/or/this/
		/and/this/if/its/single/article.html
	*/
	for (int i = 0; i < strlen(s); i++) {
		if ((!isalpha(s[i])) && (!isdigit(s[i]))
		    && (s[i] != '/') && (s[i] != '_')) {
			if ((i == (strlen(s)-5)) && (s[i] == '.')) {
				continue;
			}
			printf("Status: 400\r\n\r\nYou are trying "
			    "to send wrong query!\n");
			fflush(stdout);
			return (-1);
		}
	}

	return (0);
}

static void
http_gz_close(void)
{
	if (gz != NULL) {
		if (gzclose(gz) != Z_OK) {
			msg("error main: gzclose");
		}
		gz = NULL;
	} else {
		fflush(stdout);
	}
}


int
main(int argc, const char **argv)
{
	struct pool *pool = pool_create(64);

	const char *s;
	char *valgrindstr, *conffile;
	static struct timeval tx;
	int i, query = 0, valgrind = 0;

	gettimeofday(&tx, NULL);
	if (chdir("/tmp")) {
		fprintf(stderr, "%s: chdir: /tmp: %s", __func__, strerror(errno));
		pool_free(pool);
		return (0);
	}
	umask(007);

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--valgrind") == 0) {
			valgrind = 1;
		}
	}

	cez_queue_init(&config);
	if (valgrind) {
		conffile = pool_printf(pool, "%s/%s", CHROOT, CONFFILE);
	} else {
		conffile = pool_printf(pool, "%s", CONFFILE);
	}
	if (configfile_parse(conffile, &config) == -1) {
		fprintf(stderr, "error load_conf: file '%s'\n", conffile );
		goto purge;
	}
	if ((s = cez_queue_check(&config, params)) != NULL) {
		fprintf(stderr, "config check: %s is missing\n", s);
		goto purge;
	}

	if (valgrind) {
		for (i = 0; valgrindme[i] != NULL; ++i) {
			valgrindstr = pool_printf(pool, "%s/%s",
				    CHROOT, cqg(&config, valgrindme[i]));
			if (cqu(&config, params[i], valgrindstr) == -1) {
				fprintf(stderr, "Cannot adjust %s\n. Exit.",
					    valgrindme[i]);
				pool_free(pool);
				exit (1);
			}
		}
	}

	http_accept_encoding();
	if ((s = getenv("QUERY_STRING")) != NULL) {
		query = 1;
		if (http_query_check(s) == -1) {
			goto done;
		}
	}

	TAILQ_INIT(&feed.head);

	char *fn;
	struct entry e;
	memset(&e, 0, sizeof(e));
	fn = pool_printf(pool, "%s", cqg(&config, "datadir"));
	find_articles(pool, fn, 10);
	if (query && !strncmp(getenv("QUERY_STRING"), "/rss", 4)) {
		printf("Content-Type: application/rss+xml; charset=utf-8\r\n\r\n");
		fn = pool_printf(pool, "%s/summary.rss", cqg(&config, "htmldir"));
	} else {
		printf("%s\r\n\r\n", cqg(&config, "ct_html"));
		fn = pool_printf(pool, "%s/main.html", cqg(&config, "htmldir"));
	}
	render_main(fn, &render_macro, &e);
	fflush(stdout);

done:
	http_gz_close();
	msg("total %.1f ms query [%s]", timelapse(&tx), getenv("QUERY_STRING"));
purge:
	pool_free(pool);
	cez_queue_purge(&config);
	return (0);
}

/*
 * Render functions
 *
 */

static void
render_main(const char *html_fn, render_cb r, const struct entry *e)
{
	FILE *f;
	char s[8192];

	if ((f = fopen(html_fn, "re")) == NULL) {
		d_printf("ERROR: fopen: %s: %s<br>\n", html_fn,
		    strerror(errno));
		return;
	}
	while (fgets(s, sizeof(s), f)) {
		char *a, *b;

		for (a = s; (b = strstr(a, "%%")) != NULL;) {
			*b = 0;
			d_printf("%s", a);
			a = b + 2;
			if ((b = strstr(a, "%%")) != NULL) {
				*b = 0;
				if (r != NULL) {
					(*r)(a, e);
				}
				a = b + 2;
			}
		}
		d_printf("%s", a);
	}
	fclose(f);
}

static void
render_macro(const char *m, const struct entry *e)
{
	int i;
	char fn[1024];

	for (i = 0; Macro[i].name; i++) {
		if (strcmp(Macro[i].name, m) == 0) {
			if (Macro[i].file != NULL) {
				snprintf(fn, sizeof(fn), "%s/%s",
					cqg(&config, "htmldir"), Macro[i].file);
				(*Macro[i].render)(fn, e);
			} else {
				(*Macro[i].render)(NULL, e);
			}
			break;
		}
	}
	if (Macro[i].name == NULL)
		d_printf("%s: unknown macro '%s'<br>\n", __func__, m);
}

static void
render_story_list(const char *m, const struct entry *e)
{
	struct entry *current;

	TAILQ_FOREACH(current, &feed.head, item) {
		render_main(m, &render_macro, current);
	}
}

static void
render_body(const char *m, const struct entry *e)
{
	FILE *f;
	char s[8192];

	if ((f = fopen(e->fn, "re")) == NULL) {
		d_printf("%s: fopen %s: %s\n", __func__, e->fn, strerror(errno));
		return;
	}
	/* skip first line */
	fgets(s, sizeof(s), f);
	while (fgets(s, sizeof(s), f)) {
		d_printf("%s", s);
	}
	fclose(f);
}

static void
render_main_simple(const char *m, const struct entry *e)
{
	render_main(m, &render_macro, NULL);
}

static void
render_baseurl(const char *m, const struct entry *e)
{
	d_printf("%s", cqg(&config, "baseurl"));
}

static void
render_date(const char *m, const struct entry *e)
{
	d_printf("%s", ctime(&e->pubdate));
}

static void
render_title(const char *m, const struct entry *e)
{
	d_printf("%s", e->title);
}

static void
render_ctype(const char *m, const struct entry *e)
{
	d_printf("%s", cqg(&config, "ct_html"));
}

static void
render_article(const char *m, const struct entry *e)
{
	if (e->parent) {
		d_printf("%s/", e->parent);
	}
	d_printf("%s", e->name);
}

static void
render_link(const char *m, const struct entry *e)
{
	d_printf("%s/%s.html", cqg(&config, "baseurl"), e->name);
}
