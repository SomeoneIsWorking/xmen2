/*
 * ADVAPI32 -- the Windows registry, as a real persistent store.
 *
 * The engine wraps it in Gap::Core::igWin32Registry (openKey, createKey,
 * readValue, writeValue, enumKeyName, enumValueName, closeKey) and uses it for
 * settings. A registry is a persistent key/value store and nothing more exotic,
 * so this implements one rather than stubbing it: keys and values really are
 * created, read back, enumerated and persisted.
 *
 * That matters more than it looks. The tempting shortcut is to return success
 * from RegSetValueExA and ERROR_FILE_NOT_FOUND from every read -- the game
 * would start, take its defaults, and appear to work, while every setting it
 * saved vanished. That is a bug the player finds, not the developer.
 *
 * WHERE IT LIVES: $X2_REGISTRY if set, else ./x2registry.txt in the working
 * directory. Deliberately NOT inside the game install -- the install is never
 * written to, and a port keeping its state there is how a "read-only game
 * directory" assumption breaks silently.
 *
 * A key that does not exist reads back as ERROR_FILE_NOT_FOUND, which is what
 * Windows does and what the engine already handles as "not configured yet".
 * That is the honest first-run answer, not a stand-in for one.
 */
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define A(i)  RD32(C->esp + 4u + (uint32_t)(i) * 4u)
#define ACS(i) ((const char *)(uintptr_t)A(i))

static void ret_std(CPU *C, uint32_t eax, int nargs)
{
    C->eax = eax;
    C->esp += 4u + (uint32_t)nargs * 4u;
}

#define ERROR_SUCCESS          0u
#define ERROR_FILE_NOT_FOUND   2u
#define ERROR_MORE_DATA        234u
#define ERROR_NO_MORE_ITEMS    259u
#define ERROR_INVALID_HANDLE   6u

/* ---- the store --------------------------------------------------------- */

#define MAX_VALUES 512
#define KEYLEN 256
#define NAMELEN 128
#define DATALEN 512

typedef struct {
    int  used;
    char key[KEYLEN];       /* "HKLM\\Software\\..." with the root spelled out */
    char name[NAMELEN];
    uint32_t type;
    uint32_t len;
    unsigned char data[DATALEN];
} RegVal;

static RegVal g_v[MAX_VALUES];
static int g_loaded, g_dirty;

static const char *reg_path(void)
{
    const char *p = getenv("X2_REGISTRY");
    return (p && *p) ? p : "x2registry.txt";
}

/* One line per value: key<TAB>name<TAB>type<TAB>hex. Text on purpose -- this
   is state a developer will want to read and edit by hand. */
static void reg_load(void)
{
    FILE *f;
    char line[2048];
    if (g_loaded) return;
    g_loaded = 1;
    f = fopen(reg_path(), "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *k, *n, *t, *h, *nl;
        RegVal *v;
        unsigned i;
        nl = strchr(line, '\n');
        if (nl) *nl = 0;
        k = line;
        n = strchr(k, '\t'); if (!n) continue; *n++ = 0;
        t = strchr(n, '\t'); if (!t) continue; *t++ = 0;
        h = strchr(t, '\t'); if (!h) continue; *h++ = 0;
        for (i = 0; i < MAX_VALUES; i++) if (!g_v[i].used) break;
        if (i == MAX_VALUES) {
            fprintf(stderr, "advapi32: %s holds more than %d values; the rest "
                            "were NOT loaded\n", reg_path(), MAX_VALUES);
            break;
        }
        v = &g_v[i];
        v->used = 1;
        snprintf(v->key, sizeof v->key, "%s", k);
        snprintf(v->name, sizeof v->name, "%s", n);
        v->type = (uint32_t)strtoul(t, NULL, 10);
        v->len = 0;
        while (h[0] && h[1] && v->len < DATALEN) {
            char b[3] = { h[0], h[1], 0 };
            v->data[v->len++] = (unsigned char)strtoul(b, NULL, 16);
            h += 2;
        }
    }
    fclose(f);
}

static void reg_save(void)
{
    FILE *f;
    int i;
    uint32_t j;
    if (!g_dirty) return;
    f = fopen(reg_path(), "w");
    if (!f) {
        fprintf(stderr, "advapi32: could not write %s -- settings saved this "
                        "run will NOT persist\n", reg_path());
        return;
    }
    for (i = 0; i < MAX_VALUES; i++) {
        if (!g_v[i].used) continue;
        fprintf(f, "%s\t%s\t%u\t", g_v[i].key, g_v[i].name, g_v[i].type);
        for (j = 0; j < g_v[i].len; j++) fprintf(f, "%02x", g_v[i].data[j]);
        fputc('\n', f);
    }
    fclose(f);
    g_dirty = 0;
}

/* ---- open keys --------------------------------------------------------- */

#define MAX_KEYS 64
#define HKEY_BASE 0x40000000u          /* our own handles, distinct from the
                                          predefined HKEY_* which are 0x8000000x */
static struct { int used; char path[KEYLEN]; } g_k[MAX_KEYS];

static const char *root_name(uint32_t h)
{
    switch (h) {
    case 0x80000000u: return "HKCR";
    case 0x80000001u: return "HKCU";
    case 0x80000002u: return "HKLM";
    case 0x80000003u: return "HKU";
    case 0x80000005u: return "HKCC";
    default: return NULL;
    }
}

/* The full path a handle names, or NULL if the handle is not one. */
static const char *key_path(uint32_t h)
{
    const char *r = root_name(h);
    if (r) return r;
    if (h >= HKEY_BASE && h < HKEY_BASE + MAX_KEYS) {
        int i = (int)(h - HKEY_BASE);
        if (g_k[i].used) return g_k[i].path;
    }
    return NULL;
}

static uint32_t key_open(const char *parent, const char *sub)
{
    int i;
    for (i = 0; i < MAX_KEYS; i++) if (!g_k[i].used) break;
    if (i == MAX_KEYS) {
        fprintf(stderr, "advapi32: more than %d registry keys open at once\n",
                MAX_KEYS);
        return 0;
    }
    g_k[i].used = 1;
    if (sub && *sub) snprintf(g_k[i].path, KEYLEN, "%s\\%s", parent, sub);
    else            snprintf(g_k[i].path, KEYLEN, "%s", parent);
    return HKEY_BASE + (uint32_t)i;
}

/* Does any value live under this key, or under a subkey of it? That is what
   "the key exists" means in a store that only records values. */
static int key_exists(const char *path)
{
    size_t n = strlen(path);
    int i;
    reg_load();
    for (i = 0; i < MAX_VALUES; i++)
        if (g_v[i].used && strncasecmp(g_v[i].key, path, n) == 0
            && (g_v[i].key[n] == 0 || g_v[i].key[n] == '\\'))
            return 1;
    return 0;
}

static RegVal *val_find(const char *path, const char *name)
{
    int i;
    reg_load();
    for (i = 0; i < MAX_VALUES; i++)
        if (g_v[i].used && strcasecmp(g_v[i].key, path) == 0
            && strcasecmp(g_v[i].name, name ? name : "") == 0)
            return &g_v[i];
    return NULL;
}

/* ---- the API ----------------------------------------------------------- */

static void open_common(CPU *C, uint32_t root, const char *sub, uint32_t outp,
                        int create, int nargs)
{
    const char *parent = key_path(root);
    uint32_t h;
    char full[KEYLEN];
    if (!parent) { ret_std(C, ERROR_INVALID_HANDLE, nargs); return; }
    if (sub && *sub) snprintf(full, sizeof full, "%s\\%s", parent, sub);
    else             snprintf(full, sizeof full, "%s", parent);
    if (!create && !key_exists(full)) {
        /* Windows' answer for a key that was never written, and the answer the
           engine already treats as "not configured". */
        ret_std(C, ERROR_FILE_NOT_FOUND, nargs);
        return;
    }
    h = key_open(parent, sub);
    if (!h) { ret_std(C, ERROR_INVALID_HANDLE, nargs); return; }
    if (outp) WR32(outp, h);
    ret_std(C, ERROR_SUCCESS, nargs);
}

void imp_ADVAPI32_RegOpenKeyA(CPU *C)
{
    open_common(C, A(0), ACS(1), A(2), 0, 3);
}

void imp_ADVAPI32_RegOpenKeyExA(CPU *C)
{
    /* (root, sub, options, sam, phkResult) */
    open_common(C, A(0), ACS(1), A(4), 0, 5);
}

void imp_ADVAPI32_RegCreateKeyA(CPU *C)
{
    open_common(C, A(0), ACS(1), A(2), 1, 3);
}

void imp_ADVAPI32_RegCreateKeyExA(CPU *C)
{
    /* (root, sub, res, class, options, sam, sa, phkResult, pdwDisposition) */
    uint32_t disp = A(8);
    const char *parent = key_path(A(0));
    char full[KEYLEN];
    int existed = 0;
    if (parent) {
        if (ACS(1) && *ACS(1)) snprintf(full, sizeof full, "%s\\%s", parent, ACS(1));
        else                   snprintf(full, sizeof full, "%s", parent);
        existed = key_exists(full);
    }
    if (disp) WR32(disp, existed ? 2u : 1u);   /* OPENED_EXISTING : CREATED */
    open_common(C, A(0), ACS(1), A(7), 1, 9);
}

void imp_ADVAPI32_RegCloseKey(CPU *C)
{
    uint32_t h = A(0);
    if (h >= HKEY_BASE && h < HKEY_BASE + MAX_KEYS) g_k[h - HKEY_BASE].used = 0;
    reg_save();
    ret_std(C, ERROR_SUCCESS, 1);
}

void imp_ADVAPI32_RegQueryValueExA(CPU *C)
{
    /* (hKey, name, reserved, pType, pData, pcbData) */
    const char *path = key_path(A(0));
    RegVal *v;
    uint32_t cap;
    if (!path) { ret_std(C, ERROR_INVALID_HANDLE, 6); return; }
    v = val_find(path, ACS(1));
    if (!v) { ret_std(C, ERROR_FILE_NOT_FOUND, 6); return; }
    if (A(3)) WR32(A(3), v->type);
    cap = A(5) ? RD32(A(5)) : 0;
    if (A(5)) WR32(A(5), v->len);
    if (!A(4)) { ret_std(C, ERROR_SUCCESS, 6); return; }   /* size query */
    if (cap < v->len) { ret_std(C, ERROR_MORE_DATA, 6); return; }
    memcpy((void *)(uintptr_t)A(4), v->data, v->len);
    ret_std(C, ERROR_SUCCESS, 6);
}

void imp_ADVAPI32_RegSetValueExA(CPU *C)
{
    /* (hKey, name, reserved, type, pData, cbData) */
    const char *path = key_path(A(0));
    const char *name = ACS(1);
    uint32_t len = A(5);
    RegVal *v;
    int i;
    if (!path) { ret_std(C, ERROR_INVALID_HANDLE, 6); return; }
    if (len > DATALEN) {
        fprintf(stderr, "advapi32: RegSetValueExA(%s\\%s) is %u bytes, more "
                        "than this store holds (%d) -- REFUSING rather than "
                        "truncating a setting\n",
                path, name ? name : "", len, DATALEN);
        ret_std(C, ERROR_MORE_DATA, 6);
        return;
    }
    v = val_find(path, name);
    if (!v) {
        for (i = 0; i < MAX_VALUES; i++) if (!g_v[i].used) break;
        if (i == MAX_VALUES) {
            fprintf(stderr, "advapi32: the registry store is full (%d values)\n",
                    MAX_VALUES);
            ret_std(C, ERROR_MORE_DATA, 6);
            return;
        }
        v = &g_v[i];
        v->used = 1;
        snprintf(v->key, sizeof v->key, "%s", path);
        snprintf(v->name, sizeof v->name, "%s", name ? name : "");
    }
    v->type = A(3);
    v->len = len;
    if (len && A(4)) memcpy(v->data, (const void *)(uintptr_t)A(4), len);
    g_dirty = 1;
    reg_save();
    ret_std(C, ERROR_SUCCESS, 6);
}

void imp_ADVAPI32_RegEnumValueA(CPU *C)
{
    /* (hKey, index, name, pcchName, reserved, pType, pData, pcbData) */
    const char *path = key_path(A(0));
    uint32_t want = A(1), seen = 0;
    int i;
    if (!path) { ret_std(C, ERROR_INVALID_HANDLE, 8); return; }
    reg_load();
    for (i = 0; i < MAX_VALUES; i++) {
        RegVal *v = &g_v[i];
        if (!v->used || strcasecmp(v->key, path) != 0) continue;
        if (seen++ != want) continue;
        {
            uint32_t cap = A(3) ? RD32(A(3)) : 0;
            uint32_t n = (uint32_t)strlen(v->name);
            if (A(2) && cap > n) memcpy((void *)(uintptr_t)A(2), v->name, n + 1);
            if (A(3)) WR32(A(3), n);
            if (A(5)) WR32(A(5), v->type);
            if (A(7)) {
                uint32_t dcap = RD32(A(7));
                WR32(A(7), v->len);
                if (A(6) && dcap >= v->len)
                    memcpy((void *)(uintptr_t)A(6), v->data, v->len);
            }
        }
        ret_std(C, ERROR_SUCCESS, 8);
        return;
    }
    ret_std(C, ERROR_NO_MORE_ITEMS, 8);
}

void imp_ADVAPI32_RegEnumKeyExA(CPU *C)
{
    /* (hKey, index, name, pcchName, reserved, class, pcchClass, pftLastWrite)
       Immediate subkey names, derived from the value paths -- this store keeps
       values, and a key exists exactly when something lives under it. */
    const char *path = key_path(A(0));
    uint32_t want = A(1), seen = 0;
    size_t plen;
    char seen_names[32][NAMELEN];
    int nseen = 0, i, j;
    if (!path) { ret_std(C, ERROR_INVALID_HANDLE, 8); return; }
    plen = strlen(path);
    reg_load();
    for (i = 0; i < MAX_VALUES; i++) {
        const char *rest, *slash;
        char child[NAMELEN];
        if (!g_v[i].used) continue;
        if (strncasecmp(g_v[i].key, path, plen) != 0) continue;
        if (g_v[i].key[plen] != '\\') continue;
        rest = g_v[i].key + plen + 1;
        slash = strchr(rest, '\\');
        snprintf(child, sizeof child, "%.*s",
                 slash ? (int)(slash - rest) : (int)strlen(rest), rest);
        for (j = 0; j < nseen; j++)
            if (strcasecmp(seen_names[j], child) == 0) break;
        if (j < nseen) continue;                       /* already reported */
        if (nseen < 32) snprintf(seen_names[nseen++], NAMELEN, "%s", child);
        if (seen++ != want) continue;
        {
            uint32_t cap = A(3) ? RD32(A(3)) : 0;
            uint32_t n = (uint32_t)strlen(child);
            if (A(2) && cap > n) memcpy((void *)(uintptr_t)A(2), child, n + 1);
            if (A(3)) WR32(A(3), n);
        }
        ret_std(C, ERROR_SUCCESS, 8);
        return;
    }
    ret_std(C, ERROR_NO_MORE_ITEMS, 8);
}
