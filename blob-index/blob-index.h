#include <stddef.h> /* for size_t */
#include "term-index/term-index.h" /* for doc_id_t */

enum blob_open_mode {
	BLOB_OPEN_RD,
	BLOB_OPEN_RMA_RANDOM,
	BLOB_OPEN_WR
};

typedef void * blob_index_t;

blob_index_t blob_index_open(const char*, enum blob_open_mode);

size_t blob_index_write(blob_index_t, doc_id_t, const void *, size_t);

size_t blob_index_read(blob_index_t, doc_id_t, void **);

void blob_free(void *);

void blob_index_close(blob_index_t);

size_t blob_index_append(blob_index_t, doc_id_t, const void*, size_t);
size_t blob_index_replace(blob_index_t, doc_id_t, const void*);
