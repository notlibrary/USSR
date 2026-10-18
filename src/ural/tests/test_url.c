#include "../native/ural_url.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check_url(const char *base, const char *leaf, const char *expected)
{
    char *url = ural_ftp_url_join(base, leaf);
    int matches = url != NULL && strcmp(url, expected) == 0;
    if (!matches)
        fprintf(stderr, "FAIL %s + %s: got %s, expected %s\n",
                base, leaf, url != NULL ? url : "(null)", expected);
    free(url);
    return matches ? 0 : 1;
}

int main(void)
{
    if (check_url("ftp://tpmme.me/var/www/html/USSR", "ussr.boxes",
                  "ftp://tpmme.me//var/www/html/USSR/ussr.boxes") != 0 ||
        check_url("ftp://tpmme.me/var/www/html/USSR/", "ural-0.1.0.ural",
                  "ftp://tpmme.me//var/www/html/USSR/ural-0.1.0.ural") != 0 ||
        check_url("ftp://tpmme.me//var/www/html/USSR", "ussr.boxes",
                  "ftp://tpmme.me//var/www/html/USSR/ussr.boxes") != 0 ||
        check_url("ftp://tpmme.me", "ussr.boxes",
                  "ftp://tpmme.me//ussr.boxes") != 0 ||
        ural_ftp_url_join("invalid", "file") != NULL)
        return 1;
    puts("ural_url tests: OK");
    return 0;
}
