/*
 * ADVAPI32 -- a real registry, small and persisted, not a refusal.
 *
 * WHY IT IS REAL. The first version of this file answered every lookup
 * ERROR_FILE_NOT_FOUND, which was truthful while cg.dll's single
 * RegQueryValueA was the only caller: nothing had ever written a value, so
 * there was nothing to find. That stops being truthful the moment the game
 * WRITES. Across the modules the import set is the whole API --
 * RegCreateKeyA/ExA, RegSetValueExA, RegOpenKeyA/ExA, RegQueryValueA/ExA,
 * RegEnumKeyExA, RegEnumValueA, RegCloseKey -- and a host that accepts a write
 * and then reports the key missing does not "fail safely": it makes the game
 * re-ask a question it already answered, every run, and silently discards
 * whatever the player chose.
 *
 * WHAT IT IS. A flat map from a full path (HIVE\key\subkey) plus a value name
 * to a typed blob, persisted as one line per value in a text file under the
 * save directory, loaded at startup and rewritten on change. Flat rather than
 * a tree because the API is path-addressed: a key handle is a path, and
 * enumeration is a prefix scan. A tree buys nothing here and is a second thing
 * to get wrong.
 *
 * WHAT IT IS NOT. There is no security, no access-rights enforcement, no
 * REG_EXPAND_SZ expansion, no remote registry, no transactions. Rights are
 * accepted and ignored: this host has one user and no ACLs, so enforcing them
 * would be inventing a denial rather than modelling one. Every value type is
 * stored and returned as raw bytes with its type code, so REG_DWORD and
 * REG_BINARY are exact; only the interpretation is absent, and nothing here
 * interprets.
 *
 * THE PERSISTENCE FILE is text on purpose: when the game's settings are wrong
 * the first question is what it thinks it stored, and a binary hive would need
 * a tool to answer that.
 */
#include "x86rt.h"
#include "x86rt_native.h"
#include "advapi32.h"
#include "shell32.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define A(i)  RD32(C->esp + 4u + (uint32_t)(i) * 4u)

#define ERROR_SUCCESS          0u
#define ERROR_FILE_NOT_FOUND   2u
#define ERROR_ACCESS_DENIED    5u
#define ERROR_MORE_DATA      234u
#define ERROR_NO_MORE_ITEMS  259u

#define REG_SZ      1u
#define REG_BINARY  3u
#define REG_DWORD   4u

static void ret_std(CPU *C, uint32_t eax, int nargs)
{
    C->eax = eax;
    C->esp += 4u + (uint32_t)nargs * 4u;
}

/* ---- the store ---------------------------------------------------------- */

#define MAX_VALUES 256
#define MAX_PATH_  256
#define MAX_NAME_   96
#define MAX_DATA_  512

typedef struct {
    int      used;
    char     path[MAX_PATH_];            /* HIVE\key\subkey, no trailing sep */
    char     name[MAX_NAME_];            /* "" is the key's default value */
    uint32_t type;
    uint32_t len;
    unsigned char data[MAX_DATA_];
} RegValue;

static RegValue g_val[MAX_VALUES];
static int      g_dirty;
static unsigned long g_reads, g_misses, g_writes;

/* Keys that exist but hold no value still have to be findable, or a
   RegCreateKey followed by RegOpenKey reports the key missing. They are
   recorded as a value row with a name of "\x01" that nothing enumerates. */
#define KEY_MARK "\001"

static const char *hive_name(uint32_t h)
{
    switch (h) {
    case 0x80000000u: return "HKEY_CLASSES_ROOT";
    case 0x80000001u: return "HKEY_CURRENT_USER";
    case 0x80000002u: return "HKEY_LOCAL_MACHINE";
    case 0x80000003u: return "HKEY_USERS";
    case 0x80000005u: return "HKEY_CURRENT_CONFIG";
    default:          return NULL;
    }
}

/* ---- open key handles --------------------------------------------------- */

#define KEY_TOK  0x0E000000u
/* Where the table starts. It GROWS; this is not a limit. */
#define KEYS_INITIAL 32

/*
 * The open-key table grows, because Windows has no small limit and the guest
 * leaks handles.
 *
 * It was a fixed 32, and the game opens more than that without closing them:
 * once the table was full every RegOpenKeyA returned 0, the game gave up on
 * its settings and quit before it ever reached CreateDevice -- an early exit
 * that looked intermittent and environmental for a whole session (issue #54)
 * because it depends on how many keys the stored registry makes it walk.
 *
 * The leak is REAL and still reported, once, with the count: a guest that
 * never calls RegCloseKey is worth knowing about. It is just not this host's
 * business to enforce a limit the platform does not have.
 */
typedef struct { int used; char path[MAX_PATH_]; } KeyRec;
static KeyRec *g_key;
static int     g_key_cap;

/*
 * Resolve an HKEY to its path. A predefined hive is its own name; anything
 * else must be a handle this layer issued.
 *
 * Returns NULL for a handle that names nothing, and the caller turns that into
 * ERROR_ACCESS_DENIED rather than following it -- a stale HKEY is a real bug
 * in the caller and inventing a path for it would hide it.
 */
static const char *key_path(uint32_t h)
{
    const char *hv = hive_name(h);
    if (hv) return hv;
    if (h >= KEY_TOK && h < KEY_TOK + (uint32_t)g_key_cap &&
        g_key[h - KEY_TOK].used)
        return g_key[h - KEY_TOK].path;
    return NULL;
}

static uint32_t key_open(const char *path)
{
    int i, cap;
    KeyRec *grown;

    for (i = 0; i < g_key_cap; i++)
        if (!g_key[i].used) {
            g_key[i].used = 1;
            snprintf(g_key[i].path, sizeof g_key[i].path, "%s", path);
            return KEY_TOK + (uint32_t)i;
        }
    cap = g_key_cap ? g_key_cap * 2 : KEYS_INITIAL;
    grown = (KeyRec *)realloc(g_key, (size_t)cap * sizeof *grown);
    if (!grown) {
        fprintf(stderr, "advapi32: out of memory growing the open-key table to "
                        "%d; RegOpenKey will now FAIL and the guest will treat "
                        "that as a missing setting.\n", cap);
        return 0;
    }
    memset(grown + g_key_cap, 0,
           (size_t)(cap - g_key_cap) * sizeof *grown);
    g_key = grown;
    if (g_key_cap) {
        static int told;
        if (!told++)
            fprintf(stderr, "advapi32: more than %d registry keys are open at "
                            "once -- the guest is not calling RegCloseKey. The "
                            "table GROWS rather than failing (Windows has no "
                            "such limit); the leak is counted at exit.\n",
                    g_key_cap);
    }
    g_key_cap = cap;
    return key_open(path);
}

/* Join a parent path and a subkey, tolerating a NULL or empty subkey (which
   means "the parent itself") and stray separators at the join. */
static void path_join(char *out, size_t cap, const char *parent, const char *sub)
{
    if (!sub || !*sub) { snprintf(out, cap, "%s", parent); return; }
    while (*sub == '\\' || *sub == '/') sub++;
    snprintf(out, cap, "%s\\%s", parent, sub);
    {   size_t n = strlen(out);
        while (n && (out[n - 1] == '\\' || out[n - 1] == '/')) out[--n] = 0;
    }
}

/* ---- persistence -------------------------------------------------------- */

static const char *store_path(void)
{
    static char p[1024];
    if (!p[0]) snprintf(p, sizeof p, "%s/registry.txt", x2_save_dir());
    return p;
}

static void store_save(void)
{
    FILE *f;
    int i;
    if (!g_dirty) return;
    f = fopen(store_path(), "w");
    if (!f) {
        fprintf(stderr, "advapi32: cannot write \"%s\" -- everything the game "
                        "put in the registry this run is LOST at exit.\n",
                store_path());
        return;
    }
    fprintf(f, "# x2native registry. One value per line:\n"
               "#   <path>|<value name>|<type>|<hex bytes>\n"
               "# Delete this file to reset the game's stored settings.\n");
    for (i = 0; i < MAX_VALUES; i++) {
        uint32_t j;
        if (!g_val[i].used) continue;
        fprintf(f, "%s|%s|%u|", g_val[i].path, g_val[i].name, g_val[i].type);
        for (j = 0; j < g_val[i].len; j++) fprintf(f, "%02x", g_val[i].data[j]);
        fputc('\n', f);
    }
    fclose(f);
    g_dirty = 0;
}

static void store_load(void)
{
    static int done;
    FILE *f;
    char line[2048];
    if (done++) return;
    f = fopen(store_path(), "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *p1, *p2, *p3, *hex;
        uint32_t n = 0;
        int i;
        if (line[0] == '#' || line[0] == '\n') continue;
        line[strcspn(line, "\r\n")] = 0;
        p1 = strchr(line, '|'); if (!p1) continue;
        *p1++ = 0;
        p2 = strchr(p1, '|');   if (!p2) continue;
        *p2++ = 0;
        p3 = strchr(p2, '|');   if (!p3) continue;
        *p3++ = 0;
        hex = p3;
        for (i = 0; i < MAX_VALUES; i++) if (!g_val[i].used) break;
        if (i == MAX_VALUES) {
            fprintf(stderr, "advapi32: \"%s\" holds more than %d values; the "
                            "rest were NOT loaded and the game will not see "
                            "them.\n", store_path(), MAX_VALUES);
            break;
        }
        memset(&g_val[i], 0, sizeof g_val[i]);
        g_val[i].used = 1;
        snprintf(g_val[i].path, sizeof g_val[i].path, "%s", line);
        snprintf(g_val[i].name, sizeof g_val[i].name, "%s", p1);
        g_val[i].type = (uint32_t)strtoul(p2, NULL, 10);
        while (hex[0] && hex[1] && n < MAX_DATA_) {
            char b[3] = { hex[0], hex[1], 0 };
            g_val[i].data[n++] = (unsigned char)strtoul(b, NULL, 16);
            hex += 2;
        }
        g_val[i].len = n;
    }
    fclose(f);
}

/* ---- lookup ------------------------------------------------------------- */

static RegValue *find(const char *path, const char *name)
{
    int i;
    for (i = 0; i < MAX_VALUES; i++)
        if (g_val[i].used && strcasecmp(g_val[i].path, path) == 0
            && strcmp(g_val[i].name, name) == 0)
            return &g_val[i];
    return NULL;
}

static RegValue *put(const char *path, const char *name)
{
    RegValue *v = find(path, name);
    int i;
    if (v) return v;
    for (i = 0; i < MAX_VALUES; i++) if (!g_val[i].used) break;
    if (i == MAX_VALUES) {
        fprintf(stderr, "advapi32: the registry holds its maximum of %d values; "
                        "this write is REFUSED rather than replacing an "
                        "unrelated one.\n", MAX_VALUES);
        return NULL;
    }
    memset(&g_val[i], 0, sizeof g_val[i]);
    g_val[i].used = 1;
    snprintf(g_val[i].path, sizeof g_val[i].path, "%s", path);
    snprintf(g_val[i].name, sizeof g_val[i].name, "%s", name);
    return &g_val[i];
}

/* Does any value live at or under this path? That is what "the key exists"
   means in a flat store. */
static int key_exists(const char *path)
{
    size_t n = strlen(path);
    int i;
    for (i = 0; i < MAX_VALUES; i++) {
        if (!g_val[i].used) continue;
        if (strncasecmp(g_val[i].path, path, n) == 0
            && (g_val[i].path[n] == 0 || g_val[i].path[n] == '\\'))
            return 1;
    }
    return 0;
}

/* ---- the API ------------------------------------------------------------ */

/* LONG RegOpenKeyExA(HKEY, LPCSTR sub, DWORD opts, REGSAM sam, PHKEY out) */
static void reg_open(CPU *C, int ex)
{
    const char *parent = key_path(A(0));
    const char *sub = A(1) ? (const char *)(uintptr_t)A(1) : NULL;
    uint32_t out = ex ? A(4) : A(2);
    char full[MAX_PATH_];
    int nargs = ex ? 5 : 3;

    store_load();
    if (!parent) { ret_std(C, ERROR_ACCESS_DENIED, nargs); return; }
    path_join(full, sizeof full, parent, sub);
    if (!key_exists(full)) {
        g_misses++;
        if (out) WR32(out, 0);
        ret_std(C, ERROR_FILE_NOT_FOUND, nargs);
        return;
    }
    {   uint32_t h = key_open(full);
        if (!h) { ret_std(C, ERROR_ACCESS_DENIED, nargs); return; }
        if (out) WR32(out, h);
    }
    ret_std(C, ERROR_SUCCESS, nargs);
}

void imp_ADVAPI32_RegOpenKeyA(CPU *C)   { reg_open(C, 0); }
void imp_ADVAPI32_RegOpenKeyExA(CPU *C) { reg_open(C, 1); }

/*
 * RegCreateKeyExA(HKEY, sub, res, class, opts, sam, sa, PHKEY, PDWORD disp).
 * RegCreateKeyA(HKEY, sub, PHKEY) is the short form.
 *
 * Creating a key that holds no value has to leave a trace, or the next
 * RegOpenKey on it reports it missing -- see KEY_MARK.
 */
static void reg_create(CPU *C, int ex)
{
    const char *parent = key_path(A(0));
    const char *sub = A(1) ? (const char *)(uintptr_t)A(1) : NULL;
    uint32_t out = ex ? A(7) : A(2), disp = ex ? A(8) : 0;
    char full[MAX_PATH_];
    int nargs = ex ? 9 : 3, existed;

    store_load();
    if (!parent) { ret_std(C, ERROR_ACCESS_DENIED, nargs); return; }
    path_join(full, sizeof full, parent, sub);
    existed = key_exists(full);
    if (!existed) {
        RegValue *v = put(full, KEY_MARK);
        if (!v) { ret_std(C, ERROR_ACCESS_DENIED, nargs); return; }
        v->type = REG_BINARY;
        v->len = 0;
        g_dirty = 1;
        store_save();
    }
    {   uint32_t h = key_open(full);
        if (!h) { ret_std(C, ERROR_ACCESS_DENIED, nargs); return; }
        if (out) WR32(out, h);
    }
    if (disp) WR32(disp, existed ? 2u : 1u);   /* OPENED : CREATED */
    ret_std(C, ERROR_SUCCESS, nargs);
}

void imp_ADVAPI32_RegCreateKeyA(CPU *C)   { reg_create(C, 0); }
void imp_ADVAPI32_RegCreateKeyExA(CPU *C) { reg_create(C, 1); }

/* LONG RegQueryValueExA(HKEY, name, res, PDWORD type, BYTE *data, PDWORD cb) */
void imp_ADVAPI32_RegQueryValueExA(CPU *C)
{
    const char *path = key_path(A(0));
    const char *name = A(1) ? (const char *)(uintptr_t)A(1) : "";
    uint32_t ptype = A(3), data = A(4), pcb = A(5);
    RegValue *v;

    store_load();
    g_reads++;
    if (!path) { ret_std(C, ERROR_ACCESS_DENIED, 6); return; }
    v = find(path, name);
    if (!v) {
        g_misses++;
        ret_std(C, ERROR_FILE_NOT_FOUND, 6);
        return;
    }
    if (ptype) WR32(ptype, v->type);
    if (!data) {                          /* size query */
        if (pcb) WR32(pcb, v->len);
        ret_std(C, ERROR_SUCCESS, 6);
        return;
    }
    if (pcb && RD32(pcb) < v->len) {
        WR32(pcb, v->len);
        ret_std(C, ERROR_MORE_DATA, 6);
        return;
    }
    memcpy((void *)(uintptr_t)data, v->data, v->len);
    if (pcb) WR32(pcb, v->len);
    ret_std(C, ERROR_SUCCESS, 6);
}

/*
 * RegQueryValueA(HKEY, lpSubKey, LPSTR data, PLONG cb) -- the Win16-compatible
 * form. lpSubKey is a SUBKEY, not a value name, and it reads that key's
 * DEFAULT value. Treating it as a value name is the obvious mistake and it
 * would look up the wrong thing without failing.
 */
void imp_ADVAPI32_RegQueryValueA(CPU *C)
{
    const char *parent = key_path(A(0));
    const char *sub = A(1) ? (const char *)(uintptr_t)A(1) : NULL;
    uint32_t data = A(2), pcb = A(3);
    char full[MAX_PATH_];
    RegValue *v;

    store_load();
    g_reads++;
    if (!parent) { ret_std(C, ERROR_ACCESS_DENIED, 4); return; }
    path_join(full, sizeof full, parent, sub);
    v = find(full, "");
    if (!v) {
        g_misses++;
        if (pcb) WR32(pcb, 0);
        ret_std(C, ERROR_FILE_NOT_FOUND, 4);
        return;
    }
    if (!data) { if (pcb) WR32(pcb, v->len); ret_std(C, ERROR_SUCCESS, 4); return; }
    if (pcb && RD32(pcb) < v->len) {
        WR32(pcb, v->len);
        ret_std(C, ERROR_MORE_DATA, 4);
        return;
    }
    memcpy((void *)(uintptr_t)data, v->data, v->len);
    if (pcb) WR32(pcb, v->len);
    ret_std(C, ERROR_SUCCESS, 4);
}

/* LONG RegSetValueExA(HKEY, name, res, type, const BYTE *data, DWORD cb) */
void imp_ADVAPI32_RegSetValueExA(CPU *C)
{
    const char *path = key_path(A(0));
    const char *name = A(1) ? (const char *)(uintptr_t)A(1) : "";
    uint32_t type = A(3), data = A(4), cb = A(5);
    RegValue *v;

    store_load();
    if (!path) { ret_std(C, ERROR_ACCESS_DENIED, 6); return; }
    if (cb > MAX_DATA_) {
        /* Refused, not truncated: a truncated REG_BINARY blob reads back as a
           valid short one and the caller has no way to tell. */
        fprintf(stderr, "advapi32: RegSetValueExA(\"%s\\%s\") is %u bytes and "
                        "this store holds %d -- REFUSED rather than truncated, "
                        "because a short blob reads back as a valid one.\n",
                path, name, cb, MAX_DATA_);
        ret_std(C, ERROR_ACCESS_DENIED, 6);
        return;
    }
    v = put(path, name);
    if (!v) { ret_std(C, ERROR_ACCESS_DENIED, 6); return; }
    v->type = type;
    v->len = cb;
    if (cb && data) memcpy(v->data, (const void *)(uintptr_t)data, cb);
    g_writes++;
    g_dirty = 1;
    store_save();                        /* durable now, not at exit: a crash
                                            must not lose a setting the game
                                            has already told the player it saved */
    ret_std(C, ERROR_SUCCESS, 6);
}

/*
 * RegEnumKeyExA(HKEY, index, name, pcname, res, class, pcclass, PFILETIME)
 *
 * The subkeys of a path, in the flat store, are the distinct next components
 * of every value path under it. Counted by index, which means this is O(n) per
 * call -- fine for a store of a few hundred values and not worth an index.
 */
void imp_ADVAPI32_RegEnumKeyExA(CPU *C)
{
    const char *path = key_path(A(0));
    uint32_t idx = A(1), out = A(2), pcname = A(3);
    size_t n;
    int i, seen = 0;

    store_load();
    if (!path) { ret_std(C, ERROR_ACCESS_DENIED, 8); return; }
    n = strlen(path);
    for (i = 0; i < MAX_VALUES; i++) {
        const char *p, *sep;
        char child[MAX_NAME_];
        int j, dup = 0;
        if (!g_val[i].used) continue;
        if (strncasecmp(g_val[i].path, path, n) != 0 || g_val[i].path[n] != '\\')
            continue;
        p = g_val[i].path + n + 1;
        sep = strchr(p, '\\');
        snprintf(child, sizeof child, "%.*s",
                 sep ? (int)(sep - p) : (int)strlen(p), p);
        /* Distinct children only: two values under the same subkey must not
           enumerate it twice, or the caller sees a subkey that is not there
           and one that is goes unvisited. */
        for (j = 0; j < i; j++) {
            const char *q, *qs;
            char other[MAX_NAME_];
            if (!g_val[j].used) continue;
            if (strncasecmp(g_val[j].path, path, n) != 0
                || g_val[j].path[n] != '\\') continue;
            q = g_val[j].path + n + 1;
            qs = strchr(q, '\\');
            snprintf(other, sizeof other, "%.*s",
                     qs ? (int)(qs - q) : (int)strlen(q), q);
            if (strcasecmp(other, child) == 0) { dup = 1; break; }
        }
        if (dup) continue;
        if ((uint32_t)seen++ != idx) continue;
        {   uint32_t len = (uint32_t)strlen(child) + 1u;
            if (pcname && RD32(pcname) < len) { ret_std(C, ERROR_MORE_DATA, 8); return; }
            if (out) memcpy((void *)(uintptr_t)out, child, len);
            if (pcname) WR32(pcname, len - 1u);
        }
        ret_std(C, ERROR_SUCCESS, 8);
        return;
    }
    ret_std(C, ERROR_NO_MORE_ITEMS, 8);
}

/* RegEnumValueA(HKEY, index, name, pcname, res, ptype, data, pcb) */
void imp_ADVAPI32_RegEnumValueA(CPU *C)
{
    const char *path = key_path(A(0));
    uint32_t idx = A(1), out = A(2), pcname = A(3);
    uint32_t ptype = A(5), data = A(6), pcb = A(7);
    int i, seen = 0;

    store_load();
    if (!path) { ret_std(C, ERROR_ACCESS_DENIED, 8); return; }
    for (i = 0; i < MAX_VALUES; i++) {
        RegValue *v = &g_val[i];
        if (!v->used || strcasecmp(v->path, path) != 0) continue;
        if (strcmp(v->name, KEY_MARK) == 0) continue;   /* not a real value */
        if ((uint32_t)seen++ != idx) continue;
        {   uint32_t len = (uint32_t)strlen(v->name) + 1u;
            if (pcname && RD32(pcname) < len) { ret_std(C, ERROR_MORE_DATA, 8); return; }
            if (out) memcpy((void *)(uintptr_t)out, v->name, len);
            if (pcname) WR32(pcname, len - 1u);
        }
        if (ptype) WR32(ptype, v->type);
        if (data) {
            if (pcb && RD32(pcb) < v->len) { ret_std(C, ERROR_MORE_DATA, 8); return; }
            memcpy((void *)(uintptr_t)data, v->data, v->len);
        }
        if (pcb) WR32(pcb, v->len);
        ret_std(C, ERROR_SUCCESS, 8);
        return;
    }
    ret_std(C, ERROR_NO_MORE_ITEMS, 8);
}

void imp_ADVAPI32_RegCloseKey(CPU *C)
{
    uint32_t h = A(0);
    if (h >= KEY_TOK && h < KEY_TOK + (uint32_t)g_key_cap)
        g_key[h - KEY_TOK].used = 0;
    ret_std(C, ERROR_SUCCESS, 1);
}

void advapi32_install(void)
{
    x86_native_export("ADVAPI32.DLL", "RegOpenKeyA",     imp_ADVAPI32_RegOpenKeyA);
    x86_native_export("ADVAPI32.DLL", "RegOpenKeyExA",   imp_ADVAPI32_RegOpenKeyExA);
    x86_native_export("ADVAPI32.DLL", "RegCreateKeyA",   imp_ADVAPI32_RegCreateKeyA);
    x86_native_export("ADVAPI32.DLL", "RegCreateKeyExA", imp_ADVAPI32_RegCreateKeyExA);
    x86_native_export("ADVAPI32.DLL", "RegQueryValueA",  imp_ADVAPI32_RegQueryValueA);
    x86_native_export("ADVAPI32.DLL", "RegQueryValueExA",imp_ADVAPI32_RegQueryValueExA);
    x86_native_export("ADVAPI32.DLL", "RegSetValueExA",  imp_ADVAPI32_RegSetValueExA);
    x86_native_export("ADVAPI32.DLL", "RegEnumKeyExA",   imp_ADVAPI32_RegEnumKeyExA);
    x86_native_export("ADVAPI32.DLL", "RegEnumValueA",   imp_ADVAPI32_RegEnumValueA);
    x86_native_export("ADVAPI32.DLL", "RegCloseKey",     imp_ADVAPI32_RegCloseKey);
}

void advapi32_report(void)
{
    int i, n = 0, leaked = 0;
    for (i = 0; i < MAX_VALUES; i++)
        if (g_val[i].used && strcmp(g_val[i].name, KEY_MARK) != 0) n++;
    for (i = 0; i < g_key_cap; i++) if (g_key[i].used) leaked++;
    store_save();
    if (!g_reads && !g_writes) {
        printf("  advapi32: the registry was never touched.\n");
        return;
    }
    printf("  advapi32: %lu read(s) (%lu found nothing), %lu write(s); %d "
           "value(s) now stored in %s\n",
           g_reads, g_misses, g_writes, n, store_path());
    if (leaked)
        printf("         %d key handle(s) were never closed -- the guest is "
               "leaking them.\n", leaked);
}
