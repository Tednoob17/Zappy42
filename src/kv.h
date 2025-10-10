// kv.h - KV en mémoire minimaliste (thread-safe côté externe, lecture seule pour FaaS)
#ifndef KV_H
#define KV_H

#include <stddef.h>

/* Type opaque */
typedef struct kv kv_t;

/* Création / destruction */
kv_t* kv_new(size_t capacity);     /* crée une table avec "capacity" buckets */
void  kv_free(kv_t* kv);           /* libère toute la mémoire */

/* CRUD */
int   kv_set(kv_t* kv, const char* key, const char* val);   /* upsert */
const char* kv_get(kv_t* kv, const char* key);              /* NULL si absent */
int   kv_del(kv_t* kv, const char* key);                    /* 1 supprimé, 0 absent */

/* Utilitaires */
size_t kv_size(kv_t* kv);        /* nb de paires stockées */
size_t kv_capacity(kv_t* kv);    /* nb de buckets */
void   kv_clear(kv_t* kv);       /* vide toutes les clés */

#endif /* KV_H */
