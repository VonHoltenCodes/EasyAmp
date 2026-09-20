#include "../src/json.h"
#include <stdio.h>
#include <string.h>
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
int main(void)
{
    const char *txt = "{\"id\":42,\"code\":\"B62Y\",\"nested\":{\"a\":[1,2,{\"x\":\"y\"}],\"b\":true},"
                      "\"title\":\"Beyonc\\u00e9 \\u2013 D\xc3\xa9j\xc3\xa0 Vu \\u201cLive\\u201d \xe6\x97\xa5\",\"after\":\"ok\",\"authToken\":null}";
    ea_json j;
    char s[128];
    int r, n, a, e, count = 0;
    CHECK(json_parse(&j, txt, (int)strlen(txt)), "parse");
    r = json_root(&j);
    CHECK(json_int(&j, json_get(&j, r, "id")) == 42, "int");
    json_str(&j, json_get(&j, r, "code"), s, sizeof s); CHECK(!strcmp(s, "B62Y"), "code=%s", s);
    /* a key AFTER a nested object/array must still be found: skipping subtrees is the classic jsmn bug */
    json_str(&j, json_get(&j, r, "after"), s, sizeof s); CHECK(!strcmp(s, "ok"), "after=%s", s);
    n = json_get(&j, r, "nested"); a = json_get(&j, n, "a");
    CHECK(json_is_array(&j, a) && json_size(&j, a) == 3, "array size %d", json_size(&j, a));
    for (e = json_first(&j, a); e >= 0 && count < 3; e = json_next(&j, e)) count++;
    CHECK(count == 3, "iterated %d", count);
    CHECK(json_true(&j, json_get(&j, n, "b")), "bool");
    json_str(&j, json_get(&j, r, "title"), s, sizeof s);
    CHECK(!strcmp(s, "Beyonce - Deja Vu \"Live\" ?"), "fold=%s", s);
    CHECK(json_get(&j, r, "missing") == -1, "missing key");
    json_str(&j, json_get(&j, r, "authToken"), s, sizeof s); CHECK(!strcmp(s, "null"), "null reads as %s", s);
    json_free(&j);
    printf(fails ? "%d FAILED\n" : "all json checks passed\n", fails);
    return fails != 0;
}
