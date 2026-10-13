#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 4096
#define MAX_TEXT 512
#define MAX_TXNS 10000

typedef struct { char bank[MAX_TEXT], identifier[MAX_TEXT], opening[MAX_TEXT], closing[MAX_TEXT], currency[16]; } Pack;
typedef struct { char date[11], description[MAX_TEXT]; int64_t amount, balance; size_t line; } Transaction;

/* Opt-in compact storage preserves the parser and CSV contract. Each stored
 * row keeps numeric facts and an offset into a shared description arena.
 * Offsets survive realloc; default Transaction storage remains unchanged. */
typedef struct {
    char date[11];
    size_t description_offset;
    int64_t amount, balance;
    size_t line;
} CompactTransaction;
typedef struct {
    CompactTransaction *rows;
    size_t count, capacity;
    char *descriptions;
    size_t used, text_capacity;
} CompactStore;

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return s;
}

static int copy_field(char *dst, size_t size, const char *src, const char *name) {
    if (strlen(src) >= size) { fprintf(stderr, "%s is too long\n", name); return 0; }
    strcpy(dst, src);
    return 1;
}

static int load_pack(const char *path, Pack *pack) {
    memset(pack, 0, sizeof(*pack));
    strcpy(pack->currency, "USD");
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open pack %s: %s\n", path, strerror(errno)); return 0; }
    char line[MAX_LINE]; size_t number = 0; int ok = 1;
    while (fgets(line, sizeof(line), f)) {
        number++;
        if (!strchr(line, '\n') && !feof(f)) { fprintf(stderr, "pack line %zu is too long\n", number); ok = 0; break; }
        char *entry = trim(line);
        if (!*entry || *entry == '#') continue;
        char *equals = strchr(entry, '=');
        if (!equals) { fprintf(stderr, "pack line %zu is not key=value\n", number); ok = 0; break; }
        *equals = '\0'; char *key = trim(entry); char *value = trim(equals + 1);
        if (!*value) { fprintf(stderr, "pack line %zu has an empty value\n", number); ok = 0; break; }
        if (!strcmp(key, "bank")) ok = copy_field(pack->bank, sizeof(pack->bank), value, key);
        else if (!strcmp(key, "identifier")) ok = copy_field(pack->identifier, sizeof(pack->identifier), value, key);
        else if (!strcmp(key, "opening_prefix")) ok = copy_field(pack->opening, sizeof(pack->opening), value, key);
        else if (!strcmp(key, "closing_prefix")) ok = copy_field(pack->closing, sizeof(pack->closing), value, key);
        else if (!strcmp(key, "currency")) ok = copy_field(pack->currency, sizeof(pack->currency), value, key);
        else { fprintf(stderr, "unknown pack key %s at line %zu\n", key, number); ok = 0; }
        if (!ok) break;
    }
    fclose(f);
    if (ok && (!*pack->bank || !*pack->identifier || !*pack->opening || !*pack->closing)) {
        fprintf(stderr, "pack requires bank, identifier, opening_prefix, and closing_prefix\n"); ok = 0;
    }
    return ok;
}

static int parse_money(const char *input, int64_t *result) {
    char clean[128]; size_t count = 0; int negative = 0;
    while (isspace((unsigned char)*input)) input++;
    size_t length = strlen(input);
    while (length && isspace((unsigned char)input[length - 1])) length--;
    if (length >= 2 && input[0] == '(' && input[length - 1] == ')') { negative = 1; input++; length -= 2; }
    for (size_t i = 0; i < length; i++) {
        char c = input[i];
        if (c == '$' || c == ',' || isspace((unsigned char)c)) continue;
        if (count + 1 >= sizeof(clean)) return 0;
        clean[count++] = c;
    }
    clean[count] = '\0';
    char *p = clean;
    if (*p == '-' || *p == '+') { if (*p == '-') negative = !negative; p++; }
    if (!isdigit((unsigned char)*p)) return 0;
    int64_t whole = 0;
    while (isdigit((unsigned char)*p)) {
        int digit = *p++ - '0';
        if (whole > (INT64_MAX - digit) / 10) return 0;
        whole = whole * 10 + digit;
    }
    int cents = 0;
    if (*p == '.') {
        p++;
        if (!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1]) || p[2]) return 0;
        cents = (p[0] - '0') * 10 + p[1] - '0'; p += 2;
    }
    if (*p || whole > (INT64_MAX - cents) / 100) return 0;
    int64_t value = whole * 100 + cents;
    *result = negative ? -value : value;
    return 1;
}

static int iso_date(const char *s) {
    if (strlen(s) < 10 || s[4] != '-' || s[7] != '-') return 0;
    for (int i = 0; i < 10; i++) if (i != 4 && i != 7 && !isdigit((unsigned char)s[i])) return 0;
    int month = (s[5]-'0')*10+s[6]-'0', day = (s[8]-'0')*10+s[9]-'0';
    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

static char *pop_token(char *s) {
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    char *token = end;
    while (token > s && !isspace((unsigned char)token[-1])) token--;
    if (token == s) return NULL;
    char *split = token;
    while (split > s && isspace((unsigned char)split[-1])) split--;
    *split = '\0';
    return token;
}

static int parse_transaction(char *line, size_t number, Transaction *tx) {
    if (!iso_date(line) || line[10] != ' ') return 0;
    char *rest = trim(line + 11);
    char *balance = pop_token(rest), *amount = balance ? pop_token(rest) : NULL;
    char *description = trim(rest);
    if (!balance || !amount || !*description || !parse_money(amount, &tx->amount) || !parse_money(balance, &tx->balance)) return -1;
    memcpy(tx->date, line, 10); tx->date[10] = '\0';
    if (!copy_field(tx->description, sizeof(tx->description), description, "description")) return -1;
    tx->line = number;
    return 1;
}


/* A description is copied once into a shared arena rather than reserving 512
 * bytes in every transaction. The 10,000-row bound is unchanged. */
static int append_compact(CompactStore *store, const Transaction *tx) {
    if (store->count >= MAX_TXNS) return 0;
    size_t text_len = strlen(tx->description) + 1;
    if (store->used > SIZE_MAX - text_len) return 0;
    size_t needed = store->used + text_len;
    if (store->count == store->capacity) {
        size_t next = store->capacity ? store->capacity * 2 : 16;
        if (next > MAX_TXNS) next = MAX_TXNS;
        if (next <= store->capacity || next > SIZE_MAX / sizeof(*store->rows)) return 0;
        CompactTransaction *grown = realloc(store->rows, next * sizeof(*store->rows));
        if (!grown) return 0;
        store->rows = grown;
        store->capacity = next;
    }
    if (needed > store->text_capacity) {
        size_t next = store->text_capacity ? store->text_capacity : 1024;
        while (next < needed) {
            size_t increment = next < (1u << 20) ? next : (1u << 18);
            if (next > SIZE_MAX - increment) return 0;
            next += increment;
        }
        char *grown = realloc(store->descriptions, next);
        if (!grown) return 0;
        store->descriptions = grown;
        store->text_capacity = next;
    }
    CompactTransaction *dst = &store->rows[store->count++];
    memcpy(dst->date, tx->date, sizeof(dst->date));
    dst->description_offset = store->used;
    dst->amount = tx->amount;
    dst->balance = tx->balance;
    dst->line = tx->line;
    memcpy(store->descriptions + store->used, tx->description, text_len);
    store->used = needed;
    return 1;
}

static uint64_t fnv1a(uint64_t hash, const unsigned char *data, size_t length) {
    for (size_t i = 0; i < length; i++) { hash ^= data[i]; hash *= UINT64_C(1099511628211); }
    return hash;
}

static const char *filename_only(const char *path) {
    const char *name = path;
    for (const char *p = path; *p; p++) if (*p == '/' || *p == '\\') name = p + 1;
    return name;
}

static void write_money(FILE *f, int64_t cents) {
    uint64_t n = cents < 0 ? (uint64_t)(-(cents + 1)) + 1 : (uint64_t)cents;
    if (cents < 0) fputc('-', f);
    fprintf(f, "%llu.%02llu", (unsigned long long)(n / 100), (unsigned long long)(n % 100));
}

static void write_csv_string(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) { if (*s == '"') fputc('"', f); fputc(*s, f); }
    fputc('"', f);
}

static void write_csv_row(FILE *f, const char *date, const char *description,
                          int64_t amount, int64_t balance, const char *currency, size_t line) {
    fprintf(f, "%s,", date); write_csv_string(f, description); fputc(',', f);
    write_money(f, amount); fputc(',', f); write_money(f, balance);
    fprintf(f, ",%s,%zu\n", currency, line);
}

static int finish_csv(FILE *f, const char *path) {
    int ok = !ferror(f);
    if (fclose(f) != 0) ok = 0;
    if (!ok) fprintf(stderr, "failed writing CSV %s\n", path);
    return ok;
}

static int write_csv(const char *path, const Transaction *txs, size_t count, const char *currency) {
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "cannot write CSV %s: %s\n", path, strerror(errno)); return 0; }
    fputs("date,description,amount,balance,currency,source_line\n", f);
    for (size_t i = 0; i < count; i++)
        write_csv_row(f, txs[i].date, txs[i].description, txs[i].amount,
                      txs[i].balance, currency, txs[i].line);
    return finish_csv(f, path);
}

static int write_csv_compact(const char *path, const CompactStore *store, const char *currency) {
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "cannot write CSV %s: %s\n", path, strerror(errno)); return 0; }
    fputs("date,description,amount,balance,currency,source_line\n", f);
    for (size_t i = 0; i < store->count; i++) {
        const CompactTransaction *tx = &store->rows[i];
        write_csv_row(f, tx->date, store->descriptions + tx->description_offset,
                      tx->amount, tx->balance, currency, tx->line);
    }
    return finish_csv(f, path);
}

static void write_json_string(FILE *f, const char *s) {
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') fprintf(f, "\\%c", *p);
        else if (*p < 0x20) fprintf(f, "\\u%04x", *p);
        else fputc(*p, f);
    }
    fputc('"', f);
}

static int write_receipt(const char *path, const Pack *pack, const char *source, uint64_t checksum,
                         size_t bytes, size_t count, int64_t opening, int64_t closing) {
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "cannot write receipt %s: %s\n", path, strerror(errno)); return 0; }
    fputs("{\n  \"schema\": \"bonfyre.statement.verification.v1\",\n  \"valid\": true,\n  \"bank\": ", f);
    write_json_string(f, pack->bank); fputs(",\n  \"currency\": ", f); write_json_string(f, pack->currency);
    fputs(",\n  \"source\": ", f); write_json_string(f, source);
    fprintf(f, ",\n  \"source_bytes\": %zu,\n  \"source_fnv1a64\": \"%016llx\",\n", bytes, (unsigned long long)checksum);
    fprintf(f, "  \"transaction_count\": %zu,\n  \"opening_balance\": \"", count); write_money(f, opening);
    fputs("\",\n  \"closing_balance\": \"", f); write_money(f, closing);
    fputs("\",\n  \"invariants\": {\"running_balances\": true, \"closing_total\": true}\n}\n", f);
    int ok = !ferror(f) && fclose(f) == 0;
    if (!ok) fprintf(stderr, "failed writing receipt %s\n", path);
    return ok;
}

static void usage(const char *program) {
    fprintf(stderr, "usage: %s --pack PACK --input TEXT --csv OUTPUT --receipt RECEIPT [--compact]\n", program);
}

int main(int argc, char **argv) {
    const char *pack_path = NULL, *input_path = NULL, *csv_path = NULL, *receipt_path = NULL;
    int compact = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--compact")) { if (compact) { usage(argv[0]); return 2; } compact = 1; continue; }
        if (i + 1 >= argc) { usage(argv[0]); return 2; }
        if (!strcmp(argv[i], "--pack")) pack_path = argv[++i];
        else if (!strcmp(argv[i], "--input")) input_path = argv[++i];
        else if (!strcmp(argv[i], "--csv")) csv_path = argv[++i];
        else if (!strcmp(argv[i], "--receipt")) receipt_path = argv[++i];
        else { usage(argv[0]); return 2; }
    }
    if (!pack_path || !input_path || !csv_path || !receipt_path) { usage(argv[0]); return 2; }
    Pack pack;
    if (!load_pack(pack_path, &pack)) return 2;
    FILE *input = fopen(input_path, "rb");
    if (!input) { fprintf(stderr, "cannot open input %s: %s\n", input_path, strerror(errno)); return 2; }
    /* Statements with a few rows need only a few Transaction slots, not
     * an eager 10,000-row (~5 MiB) allocation. Retain the exact hard cap. */
    Transaction *txs = NULL;
    CompactStore compact_store = {0};
    size_t capacity = 0;
    int resource_failed = 0;

    char line[MAX_LINE]; size_t number = 0, count = 0, bytes = 0; uint64_t checksum = UINT64_C(14695981039346656037);
    int identified = 0, saw_opening = 0, saw_closing = 0, failed = 0; int64_t opening = 0, closing = 0;
    while (fgets(line, sizeof(line), input)) {
        number++; size_t raw = strlen(line); bytes += raw; checksum = fnv1a(checksum, (unsigned char *)line, raw);
        if (!strchr(line, '\n') && !feof(input)) { fprintf(stderr, "input line %zu is too long\n", number); failed = 1; break; }
        char *entry = trim(line);
        if (strstr(entry, pack.identifier)) identified = 1;
        if (!strncmp(entry, pack.opening, strlen(pack.opening))) {
            if (saw_opening || !parse_money(trim(entry + strlen(pack.opening)), &opening)) { fprintf(stderr, "invalid opening balance at line %zu\n", number); failed = 1; break; }
            saw_opening = 1; continue;
        }
        if (!strncmp(entry, pack.closing, strlen(pack.closing))) {
            if (saw_closing || !parse_money(trim(entry + strlen(pack.closing)), &closing)) { fprintf(stderr, "invalid closing balance at line %zu\n", number); failed = 1; break; }
            saw_closing = 1; continue;
        }
        if (iso_date(entry)) {
            if (count >= MAX_TXNS) { fprintf(stderr, "transaction limit exceeded\n"); failed = 1; break; }
            Transaction single;
            Transaction *target = &single;
            if (!compact) {
                if (count == capacity) {
                    size_t next = capacity ? capacity * 2 : 16;
                    if (next > MAX_TXNS) next = MAX_TXNS;
                    if (next < capacity || next > SIZE_MAX / sizeof(*txs)) {
                        fprintf(stderr, "transaction capacity overflow\n");
                        resource_failed = failed = 1; break;
                    }
                    Transaction *grown = realloc(txs, next * sizeof(*txs));
                    if (!grown) {
                        fprintf(stderr, "cannot allocate transaction storage\n");
                        resource_failed = failed = 1; break;
                    }
                    txs = grown;
                    capacity = next;
                }
                target = &txs[count];
            }
            int status = parse_transaction(entry, number, target);
            if (status < 0) { fprintf(stderr, "invalid transaction at line %zu\n", number); failed = 1; break; }
            if (status > 0) {
                if (compact && !append_compact(&compact_store, target)) {
                    fprintf(stderr, "cannot allocate compact transaction storage\n");
                    resource_failed = failed = 1; break;
                }
                count++;
            }
        }
    }
    if (ferror(input)) failed = 1;
    fclose(input);
    if (!failed && (!identified || !saw_opening || !saw_closing || !count)) { fprintf(stderr, "statement is missing identifier, balances, or transactions\n"); failed = 1; }
    int64_t expected = opening;
    for (size_t i = 0; !failed && i < count; i++) {
        int64_t amount = compact ? compact_store.rows[i].amount : txs[i].amount;
        int64_t balance = compact ? compact_store.rows[i].balance : txs[i].balance;
        size_t line_number = compact ? compact_store.rows[i].line : txs[i].line;
        if ((amount > 0 && expected > INT64_MAX - amount) || (amount < 0 && expected < INT64_MIN - amount)) {
            fprintf(stderr, "balance overflow at line %zu\n", line_number); failed = 1; break;
        }
        expected += amount;
        if (expected != balance) { fprintf(stderr, "running balance mismatch at line %zu\n", line_number); failed = 1; }
    }
    if (!failed && expected != closing) { fprintf(stderr, "closing balance mismatch\n"); failed = 1; }
    if (failed) {
        free(txs); free(compact_store.rows); free(compact_store.descriptions);
        return resource_failed ? 2 : 1;
    }
    int ok = (compact ? write_csv_compact(csv_path, &compact_store, pack.currency)
                      : write_csv(csv_path, txs, count, pack.currency))
        && write_receipt(receipt_path, &pack, filename_only(input_path), checksum, bytes, count, opening, closing);
    if (ok) printf("verified %zu transactions for %s\n", count, pack.bank);
    free(txs); free(compact_store.rows); free(compact_store.descriptions);
    return ok ? 0 : 2;
}
