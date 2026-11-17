// setuptest.cpp - host test for the setup assistant's choices (pgsetup.h):
// what it shows first for a new Psion on each machine and for each kind of
// PsiLink.ini already there, that it writes only the fields it should, that
// the Atom modem helper goes with the Atom modem only, PsiProxy's address
// checks and the privacy question, and that every annotation fits its label.
//   in ssh/: make -f test/Makefile.host setuptest && ./setuptest
#include <stdio.h>
#include <string.h>
#define PG_SU_HOST
#include "pgsetup.h"

static int gFail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); gFail++; } } while (0)

static PgSuLink Link(int net, int baud, int use, const char* host, int port)
	{
	PgSuLink l;
	l.net_mode = net; l.baud_index = baud; l.use_proxy = use; l.port = port;
	strcpy(l.host, host);
	return l;
	}

int main()
	{
	PgSuChoice c;
	PgSuLink std = Link(0, 4, 0, "", 8081);

	// a new Psion: the netBook offers the network card, the 5mx the Atom
	pg_su_from_link(&std, 0, 1, &c);
	CHECK(c.conn == PG_SU_CONN_NET && c.helper == PG_SU_HELPER_DIRECT && c.port == 8081 && !c.host[0]);
	pg_su_from_link(&std, 0, 0, &c);
	CHECK(c.conn == PG_SU_CONN_ATOM && c.helper == PG_SU_HELPER_DIRECT && c.baud_index == 4);

	// files already there: what they say wins over the machine
	PgSuLink l = Link(1, 4, 0, "", 8081);
	pg_su_from_link(&l, 1, 0, &c);
	CHECK(c.conn == PG_SU_CONN_NET);
	l = Link(0, 4, 1, "psiproxy", 8080);
	pg_su_from_link(&l, 1, 1, &c);
	CHECK(c.conn == PG_SU_CONN_ATOM && c.helper == PG_SU_HELPER_ATOM && !c.host[0] && c.port == 8081);
	l = Link(0, 2, 0, "", 8081);
	pg_su_from_link(&l, 1, 0, &c);
	CHECK(c.conn == PG_SU_CONN_MODEM && c.baud_index == 2 && c.helper == PG_SU_HELPER_DIRECT);
	l = Link(1, 4, 1, "10.0.0.5", 8099);
	pg_su_from_link(&l, 1, 0, &c);
	CHECK(c.conn == PG_SU_CONN_NET && c.helper == PG_SU_HELPER_PSIPROXY && !strcmp(c.host, "10.0.0.5") && c.port == 8099);
	l = Link(1, 4, 0, "10.0.0.5", 8099);          // PsiProxy set but off: Direct, address kept
	pg_su_from_link(&l, 1, 0, &c);
	CHECK(c.helper == PG_SU_HELPER_DIRECT && !strcmp(c.host, "10.0.0.5"));
	l = Link(1, 4, 1, "192.168.7.1", 8080);        // the Atom's PPP address on the TCP/IP route
	pg_su_from_link(&l, 1, 0, &c);
	CHECK(c.conn == PG_SU_CONN_NET && c.helper == PG_SU_HELPER_PSIPROXY && !c.host[0]);
	l = Link(0, 9, 0, "", 0);                       // damaged speed
	pg_su_from_link(&l, 1, 0, &c);
	CHECK(c.baud_index == 4 && c.port == 8081);

	// writing: only the fields it owns
	PgSuLink out = Link(0, 1, 1, "10.0.0.5", 8099);
	c.conn = PG_SU_CONN_NET; c.helper = PG_SU_HELPER_DIRECT; c.baud_index = 0; c.host[0] = 0; c.port = 8081;
	pg_su_to_link(&c, &out);
	CHECK(out.net_mode == 1 && out.baud_index == 1 && out.use_proxy == 0 && !strcmp(out.host, "10.0.0.5") && out.port == 8099);
	c.conn = PG_SU_CONN_ATOM; c.helper = PG_SU_HELPER_ATOM; c.baud_index = 0;
	pg_su_to_link(&c, &out);
	CHECK(out.net_mode == 0 && out.baud_index == 4 && out.use_proxy == 1 && !strcmp(out.host, "psiproxy") && out.port == 8080);
	c.conn = PG_SU_CONN_ATOM; c.helper = PG_SU_HELPER_PSIPROXY; strcpy(c.host, "10.0.0.9");
	pg_su_to_link(&c, &out);
	CHECK(out.use_proxy == 1 && !strcmp(out.host, "psiproxy") && out.port == 8080);   // the modem passes on (AT$PU)
	c.conn = PG_SU_CONN_MODEM; c.helper = PG_SU_HELPER_PSIPROXY; c.baud_index = 3; strcpy(c.host, "10.0.0.9"); c.port = 0;
	pg_su_to_link(&c, &out);
	CHECK(out.net_mode == 0 && out.baud_index == 3 && out.use_proxy == 1 && !strcmp(out.host, "10.0.0.9") && out.port == 8081);

	// a round trip gives back what was chosen
	PgSuChoice back;
	pg_su_from_link(&out, 1, 0, &back);
	CHECK(back.conn == PG_SU_CONN_MODEM && back.helper == PG_SU_HELPER_PSIPROXY && back.baud_index == 3 && !strcmp(back.host, "10.0.0.9"));

	// the Atom modem helper only with the Atom modem
	c.conn = PG_SU_CONN_NET; c.helper = PG_SU_HELPER_ATOM;
	CHECK(pg_su_check(&c) == 2);
	pg_su_fix(&c);
	CHECK(c.helper == PG_SU_HELPER_DIRECT && pg_su_check(&c) == 0);

	// PsiProxy needs an address, except through the Atom
	c.conn = PG_SU_CONN_NET; c.helper = PG_SU_HELPER_PSIPROXY; c.host[0] = 0;
	CHECK(pg_su_check(&c) == 1 && pg_su_needs_privacy_query(&c) && !pg_su_address_dimmed(&c));
	c.conn = PG_SU_CONN_ATOM;
	CHECK(pg_su_check(&c) == 0 && pg_su_address_dimmed(&c));
	c.helper = PG_SU_HELPER_DIRECT;
	CHECK(!pg_su_needs_privacy_query(&c) && pg_su_address_dimmed(&c));

	// speed only for another modem
	c.conn = PG_SU_CONN_NET; CHECK(pg_su_baud_dimmed(&c));
	c.conn = PG_SU_CONN_ATOM; CHECK(pg_su_baud_dimmed(&c));
	c.conn = PG_SU_CONN_MODEM; CHECK(!pg_su_baud_dimmed(&c));

	// every annotation is no longer than the longest one (the label's size)
	// and fits the label's 72 characters; no full stops, no "proxy" alone
	for (int conn = 0; conn < 3; conn++)
		for (int helper = 0; helper < 3; helper++)
			for (int tall = 0; tall < 2; tall++)
				{
				c.conn = conn; c.helper = helper;
				const char* a = pg_su_conn_note(&c, tall);
				const char* b = pg_su_helper_note(&c, tall);
				CHECK(strlen(a) <= strlen(PG_SU_CONN_NOTE_LONGEST) && strlen(a) <= 72);
				CHECK(strlen(b) <= strlen(PG_SU_HELPER_NOTE_LONGEST) && strlen(b) <= 72);
				CHECK(a[strlen(a) - 1] != '.' && b[strlen(b) - 1] != '.');
				}

	// the route's name
	CHECK(!strcmp(pg_su_route_name(0, "psiproxy"), "Direct"));
	CHECK(!strcmp(pg_su_route_name(1, ""), "Direct"));
	CHECK(!strcmp(pg_su_route_name(1, "PsiProxy"), "Atom modem helper"));
	CHECK(!strcmp(pg_su_route_name(1, "10.0.0.5"), "PsiProxy on a computer"));

	printf(gFail ? "setuptest: %d failed\n" : "setuptest: all passed\n", gFail);
	return gFail != 0;
	}
