/* pgdevboxtest.c - host test for pgdevbox.h, the engines' half of the rule
   TPsiLink::DevBox is for the apps (psilinktest.cpp checks that one): where
   PsiProxy's services are, the name for the Host: line, and which answers
   are PsiProxy's. PsiDevBox.ini is a file of that name in the current
   folder (the backslashes are part of the name on a PC).
     in ssh/: make -f test/Makefile.host pgdevboxtest && ./pgdevboxtest */
#include <stdio.h>
#include <string.h>
#include "../pgdevbox.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static void devbox_file(const char *text)
{
	FILE *f;
	remove(PG_DEVBOX_FILE);
	if (!text)
		return;
	f = fopen(PG_DEVBOX_FILE, "wb");
	fputs(text, f);
	fclose(f);
}

int main(void)
{
	const char *h = 0;
	int port = 0;

	devbox_file(0);
	/* no proxy: nowhere, PsiDevBox.ini or not */
	CHECK(!pg_devbox("", 8081, &h, &port));
	CHECK(!pg_devbox(0, 8081, &h, &port));
	/* a real proxy: itself */
	CHECK(pg_devbox("proxy.example", 8016, &h, &port) && !strcmp(h, "proxy.example") && port == 8016);
	CHECK(pg_devbox("proxy.example", 0, &h, &port) && port == PG_DEVBOX_PORT);
	/* the Atom's own (either name, any case), no PsiDevBox.ini: the Atom's */
	CHECK(pg_devbox("192.168.7.1", 8080, &h, &port) && !strcmp(h, "192.168.7.1") && port == 8080);
	CHECK(pg_devbox("PsiProxy", 8080, &h, &port) && !strcmp(h, "PsiProxy") && port == 8080);
	CHECK(!pg_is_atom_proxy("192.168.7.10") && !pg_is_atom_proxy("psiproxy2"));
	/* ...with PsiDevBox.ini: that; a real proxy still wins over it */
	devbox_file("devbox.example\r\n8090\r\n");
	CHECK(pg_devbox("psiproxy", 8080, &h, &port) && !strcmp(h, "devbox.example") && port == 8090);
	CHECK(pg_devbox("10.0.0.5", 8016, &h, &port) && !strcmp(h, "10.0.0.5") && port == 8016);
	CHECK(!pg_devbox("", 8016, &h, &port));
	/* a damaged one is ignored: the Atom's */
	devbox_file("ho st\r\n8081\r\n");
	CHECK(pg_devbox("192.168.7.1", 8080, &h, &port) && !strcmp(h, "192.168.7.1") && port == 8080);
	devbox_file("host\r\n70000\r\n");
	CHECK(pg_devbox("192.168.7.1", 8080, &h, &port) && !strcmp(h, "192.168.7.1") && port == 8080);
	devbox_file(0);

	/* the Host: line: "psiproxy" to the Atom's (an older Atom then answers
	   itself), else the name as it is */
	CHECK(!strcmp(pg_devbox_hostname("192.168.7.1"), "psiproxy"));
	CHECK(!strcmp(pg_devbox_hostname("PSIPROXY"), "psiproxy"));
	CHECK(!strcmp(pg_devbox_hostname("devbox.example"), "devbox.example"));

	/* the Atom's "no PsiProxy here" header, in any case and spacing */
	CHECK(pg_no_psiproxy_line("X-PsiConnect: no-psiproxy"));
	CHECK(pg_no_psiproxy_line("x-psiconnect:NO-PSIPROXY"));
	CHECK(!pg_no_psiproxy_line("X-PsiConnect: other"));
	CHECK(!pg_no_psiproxy_line("X-PsiConnec"));
	CHECK(!pg_no_psiproxy_line("Content-Type: text/plain"));

	/* the answers: PsiProxy's own; the Atom's 501; an older Atom's status
	   page (200, not PsiProxy's words); PsiProxy's failures as they are */
	CHECK(pg_psiproxy_answer(200, 0, pg_putlog_saved("Log saved: PsiTerm-1.log (10 bytes)")) == PG_ANSWER_OK);
	CHECK(pg_psiproxy_answer(501, 1, 0) == PG_ANSWER_NEEDS);
	CHECK(pg_psiproxy_answer(200, 1, 1) == PG_ANSWER_NEEDS);
	CHECK(pg_psiproxy_answer(200, 0, pg_putlog_saved("<html><head><title>PsiConnect web proxy")) == PG_ANSWER_NEEDS);
	CHECK(pg_psiproxy_answer(200, 0, pg_putlog_saved("")) == PG_ANSWER_NEEDS);
	CHECK(pg_psiproxy_answer(413, 0, 0) == PG_ANSWER_FAILED);
	CHECK(pg_psiproxy_answer(501, 0, 0) == PG_ANSWER_FAILED);
	CHECK(!pg_putlog_saved(0));

	printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
