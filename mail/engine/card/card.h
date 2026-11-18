/* card.h - contacts: CardDAV (carddav.c) and vCards (vcard.c), on the
 * calendar engine's HTTP, XML and port (core/mail/engine/cal: http.c,
 * xmlscan.c, calport.h).
 *
 * The Psion's Contacts program reads and writes vCard 2.1 (its Versit
 * engine, CContactDatabase::ImportContactsL / ExportSelectedContactsL) in
 * its own character set, Windows-1252. Servers keep vCard 3.0 (RFC 2426)
 * or 4.0 (RFC 6350) in UTF-8, with much the Psion can't hold: photos,
 * labels, instant messaging, anniversaries, X- properties. So:
 *   - the server's card is kept as it came (the engine's files);
 *   - the Psion is given a 2.1 card of only what Contacts holds
 *     (vc_to_psion);
 *   - a change made on the Psion goes back into the server's own card:
 *     each kind of property (the phone numbers, the addresses ...) the
 *     Psion's card has differently replaces that kind, the rest is left as
 *     it was, byte for byte (vc_merge).
 * The kinds the Psion holds: N, FN, TEL, EMAIL, ADR, ORG, TITLE, NOTE, URL,
 * BDAY. Everything else stays on the server.
 */
#ifndef PM_CARD_H
#define PM_CARD_H

/* a server card (3.0 or 4.0, UTF-8; 2.1 is read too) -> the Psion's vCard
   2.1 (cp1252, quoted-printable where needed). Returns its length, -1 if
   it doesn't fit or isn't a vCard. */
int vc_to_psion(const char *in, int len, char *out, int max);

/* the server's card with the Psion's (2.1, as Contacts exported it) merged
   in, in the server card's own version; orig NULL or "" makes a new 3.0
   card with this uid. Returns the length, -1 if it doesn't fit. */
int vc_merge(const char *orig, int olen, const char *psion, int plen, const char *uid, char *out, int max);

/* a hash of what the Psion holds of a card (its vc_to_psion text, without
   the line ends): what tells a change on the server that the Psion would
   see from one it would not */
unsigned long vc_psion_hash(const char *in, int len);

/* the three above work in 194 KB of work memory (psiwork.h), taken at the
   first call (which fails, -1 or 0, if there is none). vc_work(0) gives it
   back when the sync or the import is over; vc_work(1) takes it ahead:
   0, or -1 if there is not enough memory */
int vc_work(int on);

/* ---- CardDAV (carddav.c): like cal_sync, the address books' changes
   fetched and the Psion's sent; files in calp_dir()'s card\ (carddav.c) */
int  card_sync(int list_only, char *why, int whymax);

#endif
