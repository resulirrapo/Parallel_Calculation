#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdlib.h>
#include "uthash.h"

typedef struct HashMapEntry {
	char* word;			  // Cheia
	int* file_ids;		  // Vector de fisiere
	size_t file_count;	  // Numarul de fisiere
	size_t file_capacity; // Capacitatea vectorului de fisiere
	UT_hash_handle hh;	  // Handle pentru uthash
} HashMapEntry;

// Functii pentru operare pe HashMap
void add_file_id(HashMapEntry* entry, int file_id);
void hashmap_insert(HashMapEntry** hashmap, const char* word, int file_id);
void free_hashmap(HashMapEntry* hashmap);

#endif // HASHMAP_H