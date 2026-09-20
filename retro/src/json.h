/* json - thin helpers over the jsmn tokenizer, plus the text folding the
 * retro client needs: its fonts are ASCII, the world's music metadata is
 * UTF-8, so strings are folded on the way in (e -> e, curly quotes -> ', ...). */
#ifndef EA_JSON_H
#define EA_JSON_H

typedef struct { const char *js; void *tok; int n; } ea_json;

int  json_parse(ea_json *j, const char *text, int len);   /* 1 on success */
void json_free(ea_json *j);
int  json_root(const ea_json *j);
int  json_is_object(const ea_json *j, int t);
int  json_is_array(const ea_json *j, int t);
int  json_is_string(const ea_json *j, int t);             /* false for null / numbers */
int  json_size(const ea_json *j, int t);                  /* members / elements */
int  json_first(const ea_json *j, int t);                 /* first child token */
int  json_next(const ea_json *j, int t);                  /* sibling after t's subtree */
int  json_get(const ea_json *j, int obj, const char *key); /* value token or -1 */
int  json_str(const ea_json *j, int t, char *out, int cap);
long json_int(const ea_json *j, int t);
double json_num(const ea_json *j, int t);                 /* for values past 32 bits */
int  json_true(const ea_json *j, int t);

void ea_fold_utf8(const char *in, int len, char *out, int cap);

#endif
