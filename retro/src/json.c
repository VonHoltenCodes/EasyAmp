#include "json.h"
#include <stdlib.h>
#include <string.h>

#define JSMN_STATIC
#include "../third_party/jsmn.h"

#define T(j) ((jsmntok_t *)(j)->tok)

int json_parse(ea_json *j, const char *text, int len)
{
    jsmn_parser p;
    int n;
    j->js = text; j->tok = 0; j->n = 0;
    jsmn_init(&p);
    n = jsmn_parse(&p, text, (size_t)len, 0, 0);          /* first pass: count */
    if (n <= 0) return 0;
    j->tok = malloc(sizeof(jsmntok_t) * (size_t)n);
    if (!j->tok) return 0;
    jsmn_init(&p);
    j->n = jsmn_parse(&p, text, (size_t)len, T(j), (unsigned)n);
    if (j->n <= 0) { json_free(j); return 0; }
    return 1;
}

void json_free(ea_json *j) { free(j->tok); j->tok = 0; j->n = 0; }
int json_root(const ea_json *j) { return j->n > 0 ? 0 : -1; }
int json_is_object(const ea_json *j, int t) { return t >= 0 && t < j->n && T(j)[t].type == JSMN_OBJECT; }
int json_is_array(const ea_json *j, int t) { return t >= 0 && t < j->n && T(j)[t].type == JSMN_ARRAY; }
int json_is_string(const ea_json *j, int t) { return t >= 0 && t < j->n && T(j)[t].type == JSMN_STRING; }
int json_size(const ea_json *j, int t) { return t >= 0 && t < j->n ? T(j)[t].size : 0; }
int json_first(const ea_json *j, int t) { return t >= 0 && t + 1 < j->n && T(j)[t].size > 0 ? t + 1 : -1; }

int json_next(const ea_json *j, int t)
{
    int end, i;
    if (t < 0 || t >= j->n) return -1;
    end = T(j)[t].end;
    for (i = t + 1; i < j->n && T(j)[i].start < end; i++) ;
    return i < j->n ? i : -1;
}

int json_get(const ea_json *j, int obj, const char *key)
{
    int k, i, n;
    size_t kl = strlen(key);
    if (!json_is_object(j, obj)) return -1;
    n = T(j)[obj].size;
    for (k = 0, i = obj + 1; k < n && i >= 0 && i < j->n; k++) {
        const jsmntok_t *kt = &T(j)[i];
        int val = i + 1;
        if (kt->type == JSMN_STRING && (size_t)(kt->end - kt->start) == kl && !memcmp(j->js + kt->start, key, kl)) return val < j->n ? val : -1;
        i = json_next(j, val);
    }
    return -1;
}

long json_int(const ea_json *j, int t) { return t >= 0 && t < j->n ? atol(j->js + T(j)[t].start) : 0; }
int json_true(const ea_json *j, int t) { return t >= 0 && t < j->n && j->js[T(j)[t].start] == 't'; }

/* ---- folding -------------------------------------------------------------------- */

static const char *fold_cp(unsigned cp)
{
    /* Latin-1 supplement and Latin Extended-A, by base letter */
    static const char *l1[] = { /* 0xC0.. */
        "A","A","A","A","A","A","AE","C","E","E","E","E","I","I","I","I","D","N","O","O","O","O","O","x","O","U","U","U","U","Y","Th","ss",
        "a","a","a","a","a","a","ae","c","e","e","e","e","i","i","i","i","d","n","o","o","o","o","o","/","o","u","u","u","u","y","th","y" };
    static const char lxa[] = "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiJjJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
    static char one[2];
    if (cp >= 0xC0 && cp <= 0xFF) return l1[cp - 0xC0];
    if (cp >= 0x100 && cp <= 0x17F) { one[0] = lxa[cp - 0x100]; one[1] = 0; return one; }
    switch (cp) {
    case 0xA0: return " ";  case 0xA9: return "(c)"; case 0xAE: return "(R)"; case 0xB0: return "deg"; case 0xBD: return "1/2";
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: case 0xB4: return "'";
    case 0x201C: case 0x201D: case 0x201E: case 0x2033: return "\"";
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212: return "-";
    case 0x2026: return "...";  case 0x2022: case 0xB7: return "*";  case 0x2122: return "(TM)";
    }
    return "?";
}

static int put(char *out, int o, int cap, const char *s) { while (*s && o < cap - 1) out[o++] = *s++; return o; }

static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : 0; }

/* in: JSON string contents (still escaped) or plain UTF-8; out: printable ASCII */
void ea_fold_utf8(const char *in, int len, char *out, int cap)
{
    int i = 0, o = 0;
    while (i < len && o < cap - 1) {
        unsigned char c = (unsigned char)in[i];
        unsigned cp;
        if (c == '\\' && i + 1 < len) {
            char e = in[i + 1];
            i += 2;
            if (e == 'u' && i + 4 <= len) {
                cp = (unsigned)(hexv(in[i]) << 12 | hexv(in[i + 1]) << 8 | hexv(in[i + 2]) << 4 | hexv(in[i + 3]));
                i += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= len && in[i] == '\\' && in[i + 1] == 'u') { i += 6; cp = 0xFFFD; }   /* surrogate pair */
            } else cp = e == 'n' || e == 't' || e == 'r' ? ' ' : (unsigned char)e;
        } else if (c < 0x80) { cp = c; i++; }
        else {
            int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0, k;
            cp = extra == 3 ? c & 7u : extra == 2 ? c & 15u : extra == 1 ? c & 31u : 0xFFFD;
            i++;
            for (k = 0; k < extra && i < len && ((unsigned char)in[i] & 0xC0) == 0x80; k++, i++) cp = (cp << 6) | ((unsigned char)in[i] & 63u);
        }
        if (cp < 32) out[o++] = ' ';
        else if (cp < 127) out[o++] = (char)cp;
        else o = put(out, o, cap, fold_cp(cp));
    }
    out[o] = 0;
}

int json_str(const ea_json *j, int t, char *out, int cap)
{
    if (t < 0 || t >= j->n || cap <= 0) { if (cap > 0) out[0] = 0; return 0; }
    ea_fold_utf8(j->js + T(j)[t].start, T(j)[t].end - T(j)[t].start, out, cap);
    return 1;
}
