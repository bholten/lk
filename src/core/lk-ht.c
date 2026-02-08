#include <stdlib.h>

#include "lk-data.h"
#include "lk-int.h"

#define INITIAL_CAPACITY 64

struct lk_ht_entry {
  const char *key;
  void *value;  
};

struct lk_ht {
  struct lk_ht_entry *entries;
  size_t capacity;
  size_t length;
};

lk_ht *lk_ht_new(size_t elem_size) {
  lk_ht *ht = NULL;
  
  if (elem_size <= 0) {
    return NULL;
  }

  ht = (lk_ht *)malloc(elem_size);

  if (!ht) {
    return NULL;
  }

  ht->capacity = INITIAL_CAPACITY;
  ht->length = 0;

  ht->entries = calloc(ht->capacity, sizeof(struct lk_ht_entry));

  if (!ht->entries) {
    free(ht);
    return NULL;
  }

  return ht;
}

int lk_ht_delete(lk_ht *ht) {
  size_t i;

  for (i = 0; i < ht->capacity; i++) {
    free((void*)ht->entries[i].key);
  }

  free(ht->entries);
  free(ht);

  return 0;
}

/* TODO: hash_key, lk_ht_get, lk_ht_set, lk_ht_length, lk_hti_iterator, lk_hti_next */
