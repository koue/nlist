/*
 * Copyright (c) 2011-2025 Nikola Kolev <koue@chaosophia.net>
 * All rights reserved.
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

#include <ctype.h>
#include <errno.h>
#include <fts.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libpool.h>
#include <libqueue.h>
#include <render.h>

#include "nlist.h"

#define radd render_add

static void render_nlist_init(struct pool *pool);
static void render_main(const char *macro, void *arg);
static void render_items_list(const char *macro, void *arg);
static void render_body(const char *macro, void *arg);
static void render_print(const char *macro, void *arg);

static const char *params[] = { "datadir", "htmldir", "excludefile", "baseurl",
	"ct_html", "entries", NULL };
static const char *valgrindme[] = { "datadir", "htmldir", "excludefile", NULL };

static struct queue config;
static struct render render;
static struct feed feed;
static struct request *request;

static int RSS = 0;

static void
feed_add(struct entry *entry)
{
	TAILQ_INSERT_TAIL(&feed.head, entry, item);
}

static int
vf_parent(const char *p, int l, const char *f)
{
	if (p == NULL) {
		if (l == 1)
			return (1);
		else
			return (0);
	}
	return((strcmp(p, f)) == 0);
}

static int
file_is_excluded(const char *name)
{
	FILE *f;
	char s[8192], *p;

	if ((f = fopen(qg(&config, "excludefile"), "re")) == NULL) {
		fprintf(stderr, "Cannot open exclude file.");
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
	// No print for excluded files in the main directory if no article
	if (a == NULL) {
		if (file_is_excluded(fp))
			return (0);
		else
			return (1);
	}
	return((strcmp(a, fn) == 0));
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
	char *token, *pos = fent->fts_name;
	struct entry *current = pool_alloc(pool, sizeof(struct entry));

	/* Make sure cleared out */
	memset(current, 0, sizeof(struct entry));

	current->fn = pool_strdup(pool, fent->fts_path);

	if (file_read(pool, current) == -1) {
		return (NULL);
	}
	/* if article in main directory don't use the parent */
	token = strrchr(qg(&config, "datadir"), '/') + 1;
	if (strcmp(fent->fts_parent->fts_name, token) != 0) {
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
render_400(const char *fmt, ...)
{
	va_list ap;
	char s[8192];

	va_start(ap, fmt);
	vsnprintf(s, sizeof(s), fmt, ap);
	va_end(ap);

	printf("Status: 400");
	printf("\r\n\r\n");
	printf("%s\n", s);
}

static void
render_error(const char *fmt, ...)
{
	va_list ap;
	char s[8192];

	va_start(ap, fmt);
	vsnprintf(s, sizeof(s), fmt, ap);
	va_end(ap);
	printf("%s", qg(&config, "ct_html"));
	printf("\r\n\r\n");
	fflush(stdout);
	printf("<html><head><title>Error</title></head><body>\n");
	printf("<h2>Error</h2><p><b>%s</b><p>\n", s);
	printf("</body></html>\n");
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
	struct entry *entry;

	if ((fts = fts_open(path_argv, FTS_LOGICAL, compare_name_des_fts))
	    == NULL) {
		printf("fts_open: %s: %s<br>\n", path, strerror(errno));
		return;
	} else if ((e = fts_read(fts)) == NULL || (e->fts_info != FTS_D)) {
		printf("fts_read: fts_info %s: %s<br>\n", path,
							strerror(errno));
		return;
	} else if ((e = fts_children(fts, FTS_NAMEONLY)) == NULL) {
		if (errno != 0) {
			printf("fts_children: %s: %s<br>\n", path,
							strerror(errno));
		}
		return;
	}

	/*
	If there is parent but not article selected then show only index.txt
	content. If the main directory should be shown then show all *.txt
	files in the current directory.
	*/
	/* if rss generate rss from main directory */
	if (RSS) {
		request->parent = NULL;
	}
	if (request->parent && (request->article == NULL)) {
		request->article = pool_strdup(pool, "index.txt");
	}
	while ((( e = fts_read(fts)) != NULL) && (i < size)) {
		if (e->fts_info != FTS_F)
			continue;
		if (vf_parent(request->parent, e->fts_level, e->fts_parent->fts_name) != 1)
			continue;
		if (vf_article(request->article, e->fts_path, e->fts_name) != 1)
			continue;
		if (file_is_txt(e->fts_name) == -1 )
			continue;
		if ((entry = file_get_attr(pool, e)) == NULL)
			continue;
		feed_add(entry);
		i++;
	}
	fts_close(fts);
}

static int
http_query_check(const char *s)
{
	if (strlen(s) > 64) {
		fprintf(stderr, "warning main: long query '%s'", s);
		render_400("You are trying to send very long query!");
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
			render_400("You are trying to send wrong query!");
			fflush(stdout);
			return (-1);
		}
	}

	return (0);
}

void
request_set(struct request *current, char *query)
{
	char *article, *parent;

	while ((parent = strsep(&query, "/")) != NULL) {
		if (strlen(parent) == 0)
			continue;
		if (strstr(parent, ".html") != NULL) {
			article = parent;
			while (*parent && *parent != '.') {
				parent++;
			}
			*parent = 0;
			current->article = pool_printf(current->pool, "%s.txt", article);
		} else {
			current->parent = pool_strdup(current->pool, parent);
		}
	}
}


struct request *
request_parse(struct pool *pool)
{
	struct request *current;
	char *tmp, *pos;

	current = pool_alloc(pool, sizeof(struct request));
	memset(current, 0, sizeof(struct request));

	current->pool = pool;
	current->parent = NULL;
	current->article = NULL;
	if ((tmp = getenv("QUERY_STRING")) != NULL) {
		if (http_query_check(tmp) == -1) {
			return (NULL);
		}
		if (strncmp(tmp, "/rss", 4) == 0) {
			RSS = 1;
		} else {
			request_set(current, tmp);
		}
	}
	return (current);
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

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--valgrind") == 0) {
			valgrind = 1;
		}
	}

	queue_init(&config);
	if (valgrind) {
		conffile = pool_printf(pool, "%s/%s", CHROOTTEST, CONFFILE);
	} else {
		conffile = pool_printf(pool, "%s", CONFFILE);
	}
	if (queue_file(conffile, &config) == -1) {
		fprintf(stderr, "error load_conf: file '%s'\n", conffile );
		goto purge;
	}
	if ((s = queue_check(&config, params)) != NULL) {
		fprintf(stderr, "config check: %s is missing\n", s);
		goto purge;
	}

	if (valgrind) {
		for (i = 0; valgrindme[i] != NULL; ++i) {
			valgrindstr = pool_printf(pool, "%s/%s", CHROOTTEST,
			    qg(&config, valgrindme[i]));
			if (qu(&config, valgrindme[i], valgrindstr) == -1) {
				fprintf(stderr, "Cannot adjust %s\n. Exit.",
				    valgrindme[i]);
				pool_free(pool);
				exit (1);
			}
		}
	}

	if ((request = request_parse(pool)) == NULL) {
		goto done;
	}

	TAILQ_INIT(&feed.head);
	render_nlist_init(pool);

	char *fn;
	fn = pool_printf(pool, "%s", qg(&config, "datadir"));
	find_articles(pool, fn, strtol(qg(&config, "entries"), (char **)NULL, 10));
	if (RSS) {
		printf("Content-Type: application/rss+xml; charset=utf-8\r\n\r\n");
		render_run(&render, "MAINRSS", NULL);
	} else {
		printf("%s", qg(&config, "ct_html"));
		printf("\r\n\r\n");
		render_run(&render, "MAINHTML", NULL);
	}

done:
	fflush(stdout);
	render_purge(&render);
purge:
	pool_free(pool);
	queue_purge(&config);
	return (0);
}

/*
 * Render functions
 *
 */

static char *
radjust(struct pool *pool, const char *file)
{
	char *fn;

	if (file == NULL) {
		return (NULL);
	}
	fn = pool_printf(pool, "%s/%s", qg(&config, "htmldir"), file);
	return (fn);
}

static void
render_nlist_init(struct pool *pool)
{
	render_init(&render);
	radd(&render, "MAINHTML", radjust(pool, "main.html"), (struct entry *)render_main);
	radd(&render, "MAINRSS", radjust(pool, "main.rss"), (struct entry *)render_main);
	radd(&render, "HEADER", radjust(pool, "header.html"), (struct entry *)render_main);
	radd(&render, "FOOTER", radjust(pool,"footer.html"), (struct entry *)render_main);
	radd(&render, "ITEMSLIST", NULL, (struct entry *)render_items_list);
	radd(&render, "ITEMHTML", radjust(pool, "item.html"), (struct entry *)render_main);
	radd(&render, "ITEMRSS", radjust(pool, "item.rss"), (struct entry *)render_main);
	radd(&render, "ARTICLE", NULL, (struct entry *)render_print);
	radd(&render, "BASEURL", NULL, (struct entry *)render_print);
	radd(&render, "BODY", NULL, (struct entry *)render_body);
	radd(&render, "CTYPE", NULL, (struct entry *)render_print);
	radd(&render, "TOPIC", NULL, (struct entry *)render_print);
	radd(&render, "DATE", NULL, (struct entry *)render_print);
	radd(&render, "LINK", NULL, (struct entry *)render_print);
	radd(&render, "TITLE", NULL, (struct entry *)render_print);
}

static void
render_main(const char *macro, void *arg)
{
	render_run(&render, macro, arg);
}

static void
render_items_list(const char *macro, void *e)
{
	struct entry *current;

	TAILQ_FOREACH(current, &feed.head, item) {
		if (RSS == 1) {
			render_run(&render, "ITEMRSS", (void *)current);
		} else {
			render_run(&render, "ITEMHTML", (void *)current);
		}
	}
}

static void
render_body(const char *macro, void *arg)
{
	FILE *f;
	char s[8192];
	struct entry *e = (struct entry *)arg;

	if (e == NULL || e->fn == NULL) {
		return;
	}

	if ((f = fopen(e->fn, "re")) == NULL) {
		printf("%s: fopen %s: %s\n", __func__, e->fn, strerror(errno));
		return;
	}
	/* skip first line */
	fgets(s, sizeof(s), f);
	while (fgets(s, sizeof(s), f)) {
		printf("%s", s);
	}
	fclose(f);
}

static void
render_print(const char *macro, void *arg)
{
	struct entry *e = (struct entry *)arg;

	if (strcmp(macro, "BASEURL") == 0) {
		printf("%s", qg(&config, "baseurl"));
	} else if (strcmp(macro, "CTYPE") == 0) {
		printf("%s", qg(&config, "ct_html"));
	} else if (strcmp(macro, "TOPIC") == 0) {
		printf("%s", qg(&config, "topic"));
	} else if (e == NULL) {
		return;
	} else if (strcmp(macro, "ARTICLE") == 0) {
		if (e->name) {
			(e->parent) && printf("%s/", e->parent);
			printf("%s", e->name);
		}
	} else if (strcmp(macro, "DATE") == 0) {
		(e->pubdate) && printf("%.24s", ctime(&e->pubdate));
	} else if (strcmp(macro, "LINK") == 0) {
		(e->name) && printf("%s/%s.html", qg(&config, "baseurl"), e->name);
	} else if (strcmp(macro, "TITLE") == 0) {
		(e->title) && printf("%s", e->title);
	}
}
