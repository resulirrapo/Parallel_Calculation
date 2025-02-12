#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include "hashmap.h"

typedef struct {
	char filename[256];
	int file_id;
} FileEntry;

typedef struct {
	FileEntry* file_entries;
	int files_count;
	int curent_file_id;
	pthread_mutex_t map_mutex;
	pthread_mutex_t task_mutex;
	pthread_barrier_t barrier;
	HashMapEntry* word_hashmap;
} DataSync;

typedef struct {
	int thread_id;
	int mappers_count;
	int reducers_count;
	DataSync* data_sync;
} ThreadParams;

// Functie pentru formatarea cuvintelor
void format_word(char* word) {
	size_t len = strlen(word);
	size_t j = 0;
	for (size_t i = 0; i < len; i++) {
		if (isalpha(word[i])) {
			word[j++] = tolower(word[i]);
		}
	}
	word[j] = '\0'; // Terminator de sir
}

int compare_int(const void* a, const void* b) {
	return (*(int*)a - *(int*)b);
}

// Comparator pentru qsort
int compare_word_file_counts(const void* a, const void* b) {
	HashMapEntry* entry1 = *(HashMapEntry**)a;
	HashMapEntry* entry2 = *(HashMapEntry**)b;

	// Comparatie descrescatoare dupa numarul de fisiere
	if (entry1->file_count != entry2->file_count) {
		return entry2->file_count - entry1->file_count;
	}
	// Comparatie alfabetica daca numarul de fisiere este acelasi
	return strcmp(entry1->word, entry2->word);
}

void* map_reduce_function(void* args) {
	ThreadParams* thread_params = (ThreadParams*)args;
	DataSync* data_sync = thread_params->data_sync;

	if (thread_params->thread_id < thread_params->mappers_count) { // Mapper
		while (1) {
			int file_index;

			// Preluarea unui fisier de procesat
			pthread_mutex_lock(&data_sync->task_mutex);
			if (data_sync->curent_file_id >= data_sync->files_count) {
				pthread_mutex_unlock(&data_sync->task_mutex);
				break; // Toate fisierele au fost procesate
			}
			file_index = data_sync->curent_file_id++;
			pthread_mutex_unlock(&data_sync->task_mutex);

			FileEntry* task = &data_sync->file_entries[file_index];
			FILE* input = fopen(task->filename, "r");
			if (!input) {
				continue;
			}

			HashMapEntry* local_hashmap = NULL;
			char word[256];

			// Citim cuvintele din fisier
			while (fscanf(input, "%255s", word) != EOF) {
				format_word(word);
				if (strlen(word) > 0) {
					hashmap_insert(&local_hashmap, word, task->file_id);
				}
			}
			fclose(input);

			// Combinam rezultatele in word_hashmap
			pthread_mutex_lock(&data_sync->map_mutex);
			HashMapEntry* entry, *tmp;
			HASH_ITER(hh, local_hashmap, entry, tmp) {
				hashmap_insert(&data_sync->word_hashmap, entry->word, entry->file_ids[0]);
			}
			pthread_mutex_unlock(&data_sync->map_mutex);

			// Curatam word_hashmap ul local
			free_hashmap(local_hashmap);
		}

		// Asteptam terminarea Mapperilor
		pthread_barrier_wait(&data_sync->barrier);
	} else { // Reducer
		// Asteptam Mapperii sa termine
		pthread_barrier_wait(&data_sync->barrier);

		int reducer_id = thread_params->thread_id - thread_params->mappers_count;
		int total_reducers = thread_params->reducers_count;

		for (int offset = 0; offset + reducer_id < 26; offset += total_reducers) {
			char curent_char = 'a' + reducer_id + offset;
			HashMapEntry* local_hashmap = NULL;

			// Selectam cuvintele care incep cu litera curenta
			pthread_mutex_lock(&data_sync->map_mutex);
			HashMapEntry* entry, *tmp;
			HASH_ITER(hh, data_sync->word_hashmap, entry, tmp) {
				if (tolower(entry->word[0]) == curent_char) {
					for (size_t i = 0; i < entry->file_count; i++) {
						hashmap_insert(&local_hashmap, entry->word, entry->file_ids[i]);
					}
				}
			}
			pthread_mutex_unlock(&data_sync->map_mutex);

			// Colectam datele pentru sortare
			size_t count = HASH_COUNT(local_hashmap);
			HashMapEntry** sorted_entries = malloc(count * sizeof(HashMapEntry*));
			size_t index = 0;
			HASH_ITER(hh, local_hashmap, entry, tmp) {
				sorted_entries[index++] = entry;
			}

			// Sortam cuvintele
			qsort(sorted_entries, count, sizeof(HashMapEntry*), compare_word_file_counts);

			// Scriem rezultatele in fisierul pentru litera curenta
			char filename[16];
			snprintf(filename, sizeof(filename), "%c.txt", curent_char);
			FILE* output = fopen(filename, "w");
			if (!output) {
				free(sorted_entries);
				continue;
			}

			for (size_t i = 0; i < count; i++) {
				fprintf(output, "%s:[", sorted_entries[i]->word);
				for (size_t j = 0; j < sorted_entries[i]->file_count; j++) {
					fprintf(output, "%d%s", sorted_entries[i]->file_ids[j],
							(j + 1 < sorted_entries[i]->file_count) ? " " : "");
				}
				fprintf(output, "]\n");
			}
			fclose(output);
			free(sorted_entries);

			// Curatam word_hashmap ul local
			free_hashmap(local_hashmap);
		}
	}

	return NULL;
}

int main(int argc, char* argv[]) {
	int mappers_count = atoi(argv[1]);
	int reducers_count = atoi(argv[2]);
	char* input_file_list = argv[3];

	FILE* input_file = fopen(input_file_list, "r");
	if (!input_file) {
		return 1;
	}

	int files_count;
	if (fscanf(input_file, "%d", &files_count) != 1 || files_count <= 0) {
		fclose(input_file);
		return 1;
	}

	DataSync data_sync;
	data_sync.file_entries = malloc(files_count * sizeof(FileEntry));
	data_sync.files_count = files_count;
	data_sync.word_hashmap = NULL;
	pthread_mutex_init(&data_sync.map_mutex, NULL);
	pthread_barrier_init(&data_sync.barrier, NULL, mappers_count + reducers_count);
	pthread_mutex_init(&data_sync.task_mutex, NULL);
	data_sync.curent_file_id = 0;

	for (int i = 0; i < files_count; i++) {
		if (fscanf(input_file, "%255s", data_sync.file_entries[i].filename) == 1) {
			data_sync.file_entries[i].file_id = i + 1;
		}
	}

	fclose(input_file);

	pthread_t threads[mappers_count + reducers_count];
	ThreadParams thread_params[mappers_count + reducers_count];

	for (int i = 0; i < mappers_count + reducers_count; i++) {
		thread_params[i].thread_id = i;
		thread_params[i].mappers_count = mappers_count;
		thread_params[i].reducers_count = reducers_count;
		thread_params[i].data_sync = &data_sync;

		pthread_create(&threads[i], NULL, map_reduce_function, &thread_params[i]);
	}

	for (int i = 0; i < mappers_count + reducers_count; i++) {
		pthread_join(threads[i], NULL);
	}

	pthread_barrier_destroy(&data_sync.barrier);
	pthread_mutex_destroy(&data_sync.map_mutex);
	pthread_mutex_destroy(&data_sync.task_mutex);

	free(data_sync.file_entries);
	free_hashmap(data_sync.word_hashmap);

	return 0;
}