#include <stdio.h>
#include <string.h>
#include "hashmap.h"

void add_file_id(HashMapEntry* entry, int file_id) {
	if (entry->file_capacity == 0) {
		entry->file_capacity = 10;
		entry->file_ids = malloc(entry->file_capacity * sizeof(int));
	} else if (entry->file_count == entry->file_capacity) {
		entry->file_capacity *= 2;
		entry->file_ids = realloc(entry->file_ids, entry->file_capacity * sizeof(int));
	}

	int i = entry->file_count - 1;
	while (i >= 0 && entry->file_ids[i] > file_id) {
		entry->file_ids[i + 1] = entry->file_ids[i];
		i--;
	}
	entry->file_ids[i + 1] = file_id;
	entry->file_count++;
}

void hashmap_insert(HashMapEntry** hashmap, const char* word, int file_id) {
	HashMapEntry* entry;
	HASH_FIND_STR(*hashmap, word, entry);
	if (!entry) {
		entry = malloc(sizeof(HashMapEntry));
		entry->word = strdup(word);
		entry->file_ids = NULL;
		entry->file_count = 0;
		entry->file_capacity = 0;
		HASH_ADD_STR(*hashmap, word, entry);
	}
	add_file_id(entry, file_id);
}

void free_hashmap(HashMapEntry* hashmap) {
	HashMapEntry* curent, *tmp;
	HASH_ITER(hh, hashmap, curent, tmp) {
		HASH_DEL(hashmap, curent);
		free(curent->word);
		free(curent->file_ids);
		free(curent);
	}
}