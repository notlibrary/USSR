#ifndef _LIBINTL_H
#define _LIBINTL_H
/* message catalogs are not read: every message is its own translation */
#define gettext(msgid) ((char *)(msgid))
#define dgettext(domain, msgid) ((char *)(msgid))
#define dcgettext(domain, msgid, category) ((char *)(msgid))
#define ngettext(msgid1, msgid2, n) ((char *)((n) == 1 ? (msgid1) : (msgid2)))
#define dngettext(domain, msgid1, msgid2, n) ngettext(msgid1, msgid2, n)
#define dcngettext(domain, msgid1, msgid2, n, category) ngettext(msgid1, msgid2, n)
#define textdomain(domain) ((char *)(domain))
#define bindtextdomain(domain, dirname) ((char *)(dirname))
#define bind_textdomain_codeset(domain, codeset) ((char *)(codeset))
#endif
