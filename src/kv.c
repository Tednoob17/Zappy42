// kv.c - KV minimal en mémoire
#include "kv.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct kv_node {
    char* k;
    char* v;
    struct kv_node* next;
} kv_node_t;

struct kv {
    kv_node_t** buckets;
    size_t cap;
    size_t n;
};

/* -------------------------------------------------------------------
   Hash : FNV-1a 64-bit (rapide, simple)
   ------------------------------------------------------------------- */
static uint64_t fnv1a(const char* s) {
    uint64_t h = 1469598103934665603ULL;
    for (; *s; ++s) {
        h ^= (unsigned char)(*s);
        h *= 1099511628211ULL;
    }
    return h;
}

/* -------------------------------------------------------------------
   Gestion des nœuds
   ------------------------------------------------------------------- */
static kv_node_t* kv_node_new(const char* k, const char* v) {
    kv_node_t* n = (kv_node_t*)malloc(sizeof(*n));
    if (!n) return NULL;
    n->k = strdup(k);
    n->v = strdup(v ? v : "");
    n->next = NULL;
    if (!n->k || !n->v) {
        free(n->k); free(n->v); free(n);
        return NULL;
    }
    return n;
}

static void kv_node_free(kv_node_t* n) {
    if (!n) return;
    free(n->k);
    free(n->v);
    free(n);
}

/* -------------------------------------------------------------------
   Création / destruction
   ------------------------------------------------------------------- */
kv_t* kv_new(size_t capacity) {
    if (capacity < 8) capacity = 8;
    kv_t* kv = (kv_t*)calloc(1, sizeof(*kv));
    if (!kv) return NULL;
    kv->buckets = (kv_node_t**)calloc(capacity, sizeof(kv_node_t*));
    if (!kv->buckets) { free(kv); return NULL; }
    kv->cap = capacity;
    kv->n = 0;
    return kv;
}

void kv_free(kv_t* kv) {
    if (!kv) return;
    for (size_t i = 0; i < kv->cap; ++i) {
        kv_node_t* cur = kv->buckets[i];
        while (cur) {
            kv_node_t* nx = cur->next;
            kv_node_free(cur);
            cur = nx;
        }
    }
    free(kv->buckets);
    free(kv);
}

/* -------------------------------------------------------------------
   Effacer tout le contenu
   ------------------------------------------------------------------- */
void kv_clear(kv_t* kv) {
    if (!kv) return;
    for (size_t i = 0; i < kv->cap; ++i) {
        kv_node_t* cur = kv->buckets[i];
        while (cur) {
            kv_node_t* nx = cur->next;
            kv_node_free(cur);
            cur = nx;
        }
        kv->buckets[i] = NULL;
    }
    kv->n = 0;
}

/* -------------------------------------------------------------------
   Insertion / mise à jour
   ------------------------------------------------------------------- */
int kv_set(kv_t* kv, const char* key, const char* val) {
    if (!kv || !key || !val) return -1;
    size_t idx = (size_t)(fnv1a(key) % kv->cap);

    kv_node_t* cur = kv->buckets[idx];
    while (cur) {
        if (strcmp(cur->k, key) == 0) {
            char* nv = strdup(val);
            if (!nv) return -1;
            free(cur->v);
            cur->v = nv;
            return 0;
        }
        cur = cur->next;
    }

    kv_node_t* n = kv_node_new(key, val);
    if (!n) return -1;
    n->next = kv->buckets[idx];
    kv->buckets[idx] = n;
    kv->n++;
    return 0;
}

/* -------------------------------------------------------------------
   Récupération
   ------------------------------------------------------------------- */
const char* kv_get(kv_t* kv, const char* key) {
    if (!kv || !key) return NULL;
    size_t idx = (size_t)(fnv1a(key) % kv->cap);
    kv_node_t* cur = kv->buckets[idx];
    while (cur) {
        if (strcmp(cur->k, key) == 0)
            return cur->v;
        cur = cur->next;
    }
    return NULL;
}

/* -------------------------------------------------------------------
   Suppression
   ------------------------------------------------------------------- */
int kv_del(kv_t* kv, const char* key) {
    if (!kv || !key) return 0;
    size_t idx = (size_t)(fnv1a(key) % kv->cap);
    kv_node_t* cur = kv->buckets[idx];
    kv_node_t* prev = NULL;
    while (cur) {
        if (strcmp(cur->k, key) == 0) {
            if (prev) prev->next = cur->next;
            else kv->buckets[idx] = cur->next;
            kv_node_free(cur);
            kv->n--;
            return 1;
        }
        prev = cur;
        cur = cur->next;
    }
    return 0;
}

/* -------------------------------------------------------------------
   Infos
   ------------------------------------------------------------------- */
size_t kv_size(kv_t* kv)     { return kv ? kv->n   : 0; }
size_t kv_capacity(kv_t* kv) { return kv ? kv->cap : 0; }
