/**
 * SDL_shader_tools; tools for SDL GPU shader support.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 */

#define __SDL_SHADER_INTERNAL__ 1
#include "SDL_shader_internal.h"

/* Convenience functions for allocators... */

static char zeromalloc = 0;
void * SDLCALL SDL_SHADER_internal_malloc(size_t bytes, void *d)
{
    return (bytes == 0) ? &zeromalloc : SDL_malloc(bytes);
}

void SDLCALL SDL_SHADER_internal_free(void *ptr, void *d)
{
    if ((ptr != &zeromalloc) && (ptr != NULL)) {
        SDL_free(ptr);
    }
}

SDL_SHADER_Error SDL_SHADER_out_of_mem_error = {
    SDL_TRUE, "Out of memory", NULL, SDL_SHADER_POSITION_NONE
};

typedef struct HashItem
{
    const void *key;
    const void *value;
    struct HashItem *next;
} HashItem;

struct HashTable
{
    HashItem **table;
    Uint32 table_len;
    SDL_bool stackable;
    void *data;
    HashTable_HashFn hash;
    HashTable_KeyMatchFn keymatch;
    HashTable_NukeFn nuke;
    SDL_SHADER_Malloc m;
    SDL_SHADER_Free f;
    void *d;
};

static inline Uint32 calc_hash(const HashTable *table, const void *key)
{
    return table->hash(key, table->data) & (table->table_len-1);
}

SDL_bool hash_find(const HashTable *table, const void *key, const void **_value)
{
    HashItem *i;
    void *data = table->data;
    const Uint32 hash = calc_hash(table, key);
    HashItem *prev = NULL;
    for (i = table->table[hash]; i != NULL; i = i->next) {
        if (table->keymatch(key, i->key, data)) {
            if (_value != NULL) {
                *_value = i->value;
            }

            /* Matched! Move to the front of list for faster lookup next time. (stackable tables have to remain in the same order, though!) */
            if ((!table->stackable) && (prev != NULL)) {
                SDL_assert(prev->next == i);
                prev->next = i->next;
                i->next = table->table[hash];
                table->table[hash] = i;
            }

            return SDL_TRUE;
        }

        prev = i;
    }

    return SDL_FALSE;
}

SDL_bool hash_iter(const HashTable *table, const void *key, const void **_value, void **iter)
{
    HashItem *item = (*iter == NULL) ? table->table[calc_hash(table, key)] : ((HashItem *) *iter)->next;
    while (item != NULL) {
        if (table->keymatch(key, item->key, table->data)) {
            *_value = item->value;
            *iter = item;
            return SDL_TRUE;
        }
        item = item->next;
    }

    /* no more matches. */
    *_value = NULL;
    *iter = NULL;
    return SDL_FALSE;
}

SDL_bool hash_iter_keys(const HashTable *table, const void **_key, void **iter)
{
    HashItem *item = (HashItem *) *iter;
    Uint32 idx = 0;

    if (item != NULL) {
        const HashItem *orig = item;
        item = item->next;
        if (item == NULL) {
            idx = calc_hash(table, orig->key) + 1;
        }
    }

    while (!item && (idx < table->table_len)) {
        item = table->table[idx++];  /* skip empty buckets... */
    }

    if (item == NULL) {  /* no more matches? */
        *_key = NULL;
        *iter = NULL;
        return SDL_FALSE;
    }

    *_key = item->key;
    *iter = item;
    return SDL_TRUE;
}

int hash_insert(HashTable *table, const void *key, const void *value)
{
    HashItem *item = NULL;
    const Uint32 hash = calc_hash(table, key);
    if ( (!table->stackable) && (hash_find(table, key, NULL)) ) {
        return 0;
    }

    /* !!! FIXME: grow and rehash table if it gets too saturated. */
    item = (HashItem *) table->m(sizeof (HashItem), table->d);
    if (item == NULL) {
        return -1;
    }

    item->key = key;
    item->value = value;
    item->next = table->table[hash];
    table->table[hash] = item;

    return 1;
}

HashTable *hash_create(void *data, const HashTable_HashFn hashfn,
              const HashTable_KeyMatchFn keymatchfn,
              const HashTable_NukeFn nukefn,
              const SDL_bool stackable,
              SDL_SHADER_Malloc m, SDL_SHADER_Free f, void *d)
{
    const Uint32 initial_table_size = 256;
    const Uint32 alloc_len = sizeof (HashItem *) * initial_table_size;
    HashTable *table = (HashTable *) m(sizeof (HashTable), d);
    if (table == NULL) {
        return NULL;
    }
    SDL_zerop(table);

    table->table = (HashItem **) m(alloc_len, d);
    if (table->table == NULL) {
        f(table, d);
        return NULL;
    }

    SDL_memset(table->table, '\0', alloc_len);
    table->table_len = initial_table_size;
    table->stackable = stackable;
    table->data = data;
    table->hash = hashfn;
    table->keymatch = keymatchfn;
    table->nuke = nukefn;
    table->m = m;
    table->f = f;
    table->d = d;
    return table;
}

void hash_destroy(HashTable *table)
{
    Uint32 i;
    void *data = table->data;
    SDL_SHADER_Free f = table->f;
    void *d = table->d;
    for (i = 0; i < table->table_len; i++) {
        HashItem *item = table->table[i];
        while (item != NULL) {
            HashItem *next = item->next;
            table->nuke(item->key, item->value, data);
            f(item, d);
            item = next;
        }
    }

    f(table->table, d);
    f(table, d);
}

SDL_bool hash_remove(HashTable *table, const void *key)
{
    HashItem *item = NULL;
    HashItem *prev = NULL;
    void *data = table->data;
    const Uint32 hash = calc_hash(table, key);
    for (item = table->table[hash]; item != NULL; item = item->next) {
        if (table->keymatch(key, item->key, data)) {
            if (prev != NULL) {
                prev->next = item->next;
            } else {
                table->table[hash] = item->next;
            }
            table->nuke(item->key, item->value, data);
            table->f(item, table->d);
            return SDL_TRUE;
        }

        prev = item;
    }

    return SDL_FALSE;
}

/* !!! FIXME: this is in another source file too! */
/* this is djb's xor hashing function. */
static inline Uint32 hash_string_djbxor(const char *str, size_t len)
{
    Uint32 hash = 5381;
    while (len--) {
        hash = ((hash << 5) + hash) ^ *(str++);
    }
    return hash;
}

static inline Uint32 hash_string(const char *str, size_t len)
{
    return hash_string_djbxor(str, len);
}

Uint32 hash_hash_string(const void *sym, void *data)
{
    (void) data;
    return hash_string((const char*) sym, SDL_strlen((const char *) sym));
}

int hash_keymatch_string(const void *a, const void *b, void *data)
{
    (void) data;
    return (SDL_strcmp((const char *) a, (const char *) b) == 0);
}


/* string -> string map... */

static void stringmap_nuke_noop(const void *key, const void *val, void *d) {}

static void stringmap_nuke(const void *key, const void *val, void *d)
{
    StringMap *smap = (StringMap *) d;
    smap->f((void *) key, smap->d);
    smap->f((void *) val, smap->d);
}

StringMap *stringmap_create(const int copy, SDL_SHADER_Malloc m,
                            SDL_SHADER_Free f, void *d)
{
    HashTable_NukeFn nuke = copy ? stringmap_nuke : stringmap_nuke_noop;
    StringMap *smap;
    smap = hash_create(NULL, hash_hash_string, hash_keymatch_string, nuke, SDL_FALSE, m, f, d);
    if (smap != NULL)
        smap->data = smap;
    return smap;
}

void stringmap_destroy(StringMap *smap)
{
    hash_destroy(smap);
}

int stringmap_insert(StringMap *smap, const char *key, const char *value)
{
    int rc = -1;
    SDL_assert(key != NULL);
    if (smap->nuke == stringmap_nuke_noop) {  /* no copy? */
        return hash_insert(smap, key, value);
    } else {
        const size_t keylen = SDL_strlen(key) + 1;
        const size_t vallen = SDL_strlen(value) + 1;
        char *k = (char *) smap->m(keylen, smap->d);
        char *v = (char *) (value ? smap->m(vallen, smap->d) : NULL);
        SDL_bool failed = ( (!k) || ((!v) && (value)) ) ? SDL_TRUE : SDL_FALSE;

        if (!failed) {
            SDL_strlcpy(k, key, keylen);
            if (value != NULL) {
                SDL_strlcpy(v, value, vallen);
            }
            rc = hash_insert(smap, k, v);
            failed = (rc <= 0) ? SDL_TRUE : SDL_FALSE;
        }

        if (failed) {
            smap->f(k, smap->d);
            smap->f(v, smap->d);
        }
    }

    return rc;
}

int stringmap_remove(StringMap *smap, const char *key)
{
    return hash_remove(smap, key);
}

SDL_bool stringmap_find(const StringMap *smap, const char *key, const char **_value)
{
    const void *value = NULL;
    const SDL_bool retval = hash_find(smap, key, &value);
    *_value = (const char *) value;
    return retval;
}


/* The string cache...   !!! FIXME: use StringMap internally for this. */

typedef struct StringBucket
{
    char *string;
    struct StringBucket *next;
} StringBucket;

struct StringCache
{
    StringBucket **hashtable;
    Uint32 table_size;
    SDL_SHADER_Malloc m;
    SDL_SHADER_Free f;
    void *d;
};


const char *stringcache(StringCache *cache, const char *str)
{
    return stringcache_len(cache, str, strlen(str));
}

static const char *stringcache_len_internal(StringCache *cache, const char *str, const size_t len, const SDL_bool addmissing)
{
    const Uint8 hash = hash_string(str, len) & (cache->table_size-1);
    StringBucket *bucket = cache->hashtable[hash];
    StringBucket *prev = NULL;
    while (bucket) {
        const char *bstr = bucket->string;
        if ((SDL_strncmp(bstr, str, len) == 0) && (bstr[len] == 0)) {
            /* Matched! Move this to the front of the list. */
            if (prev != NULL) {
                SDL_assert(prev->next == bucket);
                prev->next = bucket->next;
                bucket->next = cache->hashtable[hash];
                cache->hashtable[hash] = bucket;
            }
            return bstr; /* already cached */
        }
        prev = bucket;
        bucket = bucket->next;
    }

    /* no match! */
    if (!addmissing) {
        return NULL;
    }

    /* add to the table. */
    bucket = (StringBucket *) cache->m(sizeof (StringBucket) + len + 1, cache->d);
    if (bucket == NULL) {
        return NULL;
    }

    bucket->string = (char *)(bucket + 1);
    SDL_memcpy(bucket->string, str, len);
    bucket->string[len] = '\0';
    bucket->next = cache->hashtable[hash];
    cache->hashtable[hash] = bucket;
    return bucket->string;
}

const char *stringcache_len(StringCache *cache, const char *str, const size_t len)
{
    return stringcache_len_internal(cache, str, len, SDL_TRUE);
}

SDL_bool stringcache_iscached(StringCache *cache, const char *str)
{
    return (stringcache_len_internal(cache, str, SDL_strlen(str), SDL_FALSE) != NULL) ? SDL_TRUE : SDL_FALSE;
}

const char *stringcache_fmt(StringCache *cache, const char *fmt, ...)
{
    const char *retval;
    char buf[128];  /* use the stack if reasonable. */
    char *ptr = NULL;
    size_t len = 0;  /* number of chars, NOT counting null-terminator! */
    va_list ap;

    va_start(ap, fmt);
    len = SDL_vsnprintf(buf, sizeof (buf), fmt, ap);
    va_end(ap);

    if (len > sizeof (buf)) {
        ptr = (char *) cache->m(len, cache->d);
        if (ptr == NULL) {
            return NULL;
        }

        va_start(ap, fmt);
        SDL_vsnprintf(ptr, len, fmt, ap);
        va_end(ap);
    }

    retval = stringcache_len(cache, ptr ? ptr : buf, len);
    cache->f(ptr, cache->d);

    return retval;
}

StringCache *stringcache_create(SDL_SHADER_Malloc m, SDL_SHADER_Free f, void *d)
{
    const Uint32 initial_table_size = 256;
    const size_t tablelen = sizeof (StringBucket *) * initial_table_size;
    StringCache *cache = (StringCache *) m(sizeof (StringCache), d);
    if (!cache) {
        return NULL;
    }
    SDL_zerop(cache);

    cache->hashtable = (StringBucket **) m(tablelen, d);
    if (!cache->hashtable) {
        f(cache, d);
        return NULL;
    }
    SDL_memset(cache->hashtable, '\0', tablelen);

    cache->table_size = initial_table_size;
    cache->m = m;
    cache->f = f;
    cache->d = d;
    return cache;
}

void stringcache_destroy(StringCache *cache)
{
    if (cache != NULL) {
        SDL_SHADER_Free f = cache->f;
        void *d = cache->d;
        size_t i;

        for (i = 0; i < cache->table_size; i++) {
            StringBucket *bucket = cache->hashtable[i];
            cache->hashtable[i] = NULL;
            while (bucket)  {
                StringBucket *next = bucket->next;
                f(bucket, d);
                bucket = next;
            }
        }

        f(cache->hashtable, d);
        f(cache, d);
    }
}


/* We chain errors as a linked list with a head/tail for easy appending.
   These get flattened before passing to the application. */
typedef struct ErrorItem
{
    SDL_SHADER_Error error;
    struct ErrorItem *next;
} ErrorItem;

struct ErrorList
{
    ErrorItem head;
    ErrorItem *tail;
    size_t count;
    SDL_SHADER_Malloc m;
    SDL_SHADER_Free f;
    void *d;
};

ErrorList *errorlist_create(SDL_SHADER_Malloc m, SDL_SHADER_Free f, void *d)
{
    ErrorList *retval = (ErrorList *) m(sizeof (ErrorList), d);
    if (retval != NULL) {
        SDL_zerop(retval);
        retval->tail = &retval->head;
        retval->m = m;
        retval->f = f;
        retval->d = d;
    }

    return retval;
}

SDL_bool errorlist_add(ErrorList *list, const SDL_bool is_error, const char *fname, const int errpos, const char *str)
{
    return errorlist_add_fmt(list, is_error, fname, errpos, "%s", str);
}


SDL_bool errorlist_add_fmt(ErrorList *list, const SDL_bool is_error, const char *fname, const int errpos, const char *fmt, ...)
{
    size_t retval;
    va_list ap;
    va_start(ap, fmt);
    retval = errorlist_add_va(list, is_error, fname, errpos, fmt, ap);
    va_end(ap);
    return retval;
}


SDL_bool errorlist_add_va(ErrorList *list, const SDL_bool is_error, const char *_fname, const int errpos, const char *fmt, va_list va)
{
    ErrorItem *error = (ErrorItem *) list->m(sizeof (ErrorItem), list->d);
    char *fname = NULL;
    char *failstr;
    char scratch[128];
    size_t len;
    va_list ap;

    if (error == NULL) {
        return SDL_FALSE;
    }

    if (_fname != NULL) {
        fname = (char *) list->m(SDL_strlen(_fname) + 1, list->d);
        if (fname == NULL) {
            list->f(error, list->d);
            return SDL_FALSE;
        }
        strcpy(fname, _fname);
    }

    va_copy(ap, va);
    len = SDL_vsnprintf(scratch, sizeof (scratch), fmt, ap);
    va_end(ap);

    failstr = (char *) list->m(len + 1, list->d);
    if (failstr == NULL) {
        list->f(error, list->d);
        list->f(fname, list->d);
        return SDL_FALSE;
    }

    /* If we overflowed our scratch buffer, that's okay. We were going to
       allocate anyhow...the scratch buffer just lets us avoid a second
       run of vsnprintf(). */
    if (len < sizeof (scratch)) {
        SDL_strlcpy(failstr, scratch, len + 1);  /* copy it over. */
    } else {
        va_copy(ap, va);
        SDL_vsnprintf(failstr, len + 1, fmt, ap);  /* rebuild it. */
        va_end(ap);
    }

    error->error.is_error = is_error;
    error->error.message = failstr;
    error->error.filename = fname;
    error->error.error_position = errpos;
    error->next = NULL;

    list->tail->next = error;
    list->tail = error;

    list->count++;

    return SDL_TRUE;
}

size_t errorlist_count(ErrorList *list)
{
    return list ? list->count : 0;
}

SDL_SHADER_Error *errorlist_flatten(ErrorList *list)
{
    SDL_SHADER_Error *retval = NULL;
    if (list->count > 0) {
        ErrorItem *item = list->head.next;
        size_t total = 0;
        retval = (SDL_SHADER_Error *) list->m(sizeof (SDL_SHADER_Error) * list->count, list->d);
        if (retval == NULL) {
            return NULL;
        }

        while (item != NULL) {
            ErrorItem *next = item->next;
            /* reuse the string allocations */
            SDL_memcpy(&retval[total], &item->error, sizeof (SDL_SHADER_Error));
            list->f(item, list->d);
            item = next;
            total++;
        }

        SDL_assert(total == list->count);
        list->count = 0;
        list->head.next = NULL;
        list->tail = &list->head;
    }

    return retval;
}

void errorlist_destroy(ErrorList *list)
{
    if (list != NULL) {
        SDL_SHADER_Free f = list->f;
        void *d = list->d;
        ErrorItem *item = list->head.next;
        while (item != NULL) {
            ErrorItem *next = item->next;
            f((void *) item->error.message, d);
            f((void *) item->error.filename, d);
            f(item, d);
            item = next;
        }
        f(list, d);
    }
}


typedef struct BufferBlock
{
    Uint8 *data;
    size_t bytes;
    struct BufferBlock *next;
} BufferBlock;

struct Buffer
{
    size_t total_bytes;
    BufferBlock *head;
    BufferBlock *tail;
    size_t block_size;
    SDL_SHADER_Malloc m;
    SDL_SHADER_Free f;
    void *d;
};

Buffer *buffer_create(size_t blksz, SDL_SHADER_Malloc m,
                      SDL_SHADER_Free f, void *d)
{
    Buffer *buffer = (Buffer *) m(sizeof (Buffer), d);
    if (buffer != NULL) {
        SDL_zerop(buffer);
        buffer->block_size = blksz;
        buffer->m = m;
        buffer->f = f;
        buffer->d = d;
    }
    return buffer;
}

char *buffer_reserve(Buffer *buffer, const size_t len)
{
    /* note that we make the blocks bigger than blocksize when we have enough
       data to overfill a fresh block, to reduce allocations. */
    const size_t blocksize = buffer->block_size;
    size_t bytecount, malloc_len;

    if (len == 0) {
        return NULL;
    }

    if (buffer->tail != NULL) {
        const size_t tailbytes = buffer->tail->bytes;
        const size_t avail = (tailbytes >= blocksize) ? 0 : blocksize - tailbytes;
        if (len <= avail)
        {
            buffer->tail->bytes += len;
            buffer->total_bytes += len;
            SDL_assert(buffer->tail->bytes <= blocksize);
            return (char *) buffer->tail->data + tailbytes;
        }
    }

    /* need to allocate a new block (even if a previous block wasn't filled, so this buffer is contiguous). */
    bytecount = len > blocksize ? len : blocksize;
    malloc_len = sizeof (BufferBlock) + bytecount;
    BufferBlock *item = (BufferBlock *) buffer->m(malloc_len, buffer->d);
    if (item == NULL) {
        return NULL;
    }

    item->data = ((Uint8 *) item) + sizeof (BufferBlock);
    item->bytes = len;
    item->next = NULL;
    if (buffer->tail != NULL) {
        buffer->tail->next = item;
    } else {
        buffer->head = item;
    }
    buffer->tail = item;

    buffer->total_bytes += len;

    return (char *) item->data;
}

SDL_bool buffer_append(Buffer *buffer, const void *_data, size_t len)
{
    const Uint8 *data = (const Uint8 *) _data;

    /* note that we make the blocks bigger than blocksize when we have enough
       data to overfill a fresh block, to reduce allocations. */
    const size_t blocksize = buffer->block_size;

    if (len == 0) {
        return SDL_TRUE;
    }

    if (buffer->tail != NULL) {
        const size_t tailbytes = buffer->tail->bytes;
        const size_t avail = (tailbytes >= blocksize) ? 0 : blocksize - tailbytes;
        const size_t cpy = (avail > len) ? len : avail;
        if (cpy > 0) {
            SDL_memcpy(buffer->tail->data + tailbytes, data, cpy);
            len -= cpy;
            data += cpy;
            buffer->tail->bytes += cpy;
            buffer->total_bytes += cpy;
            SDL_assert(buffer->tail->bytes <= blocksize);
        }
    }

    if (len > 0) {
        const size_t bytecount = len > blocksize ? len : blocksize;
        const size_t malloc_len = sizeof (BufferBlock) + bytecount;
        BufferBlock *item = (BufferBlock *) buffer->m(malloc_len, buffer->d);
        SDL_assert((!buffer->tail) || (buffer->tail->bytes >= blocksize));
        if (item == NULL) {
            return SDL_FALSE;
        }
        item->data = ((Uint8 *) item) + sizeof (BufferBlock);
        item->bytes = len;
        item->next = NULL;
        if (buffer->tail != NULL) {
            buffer->tail->next = item;
        } else {
            buffer->head = item;
        }
        buffer->tail = item;

        SDL_memcpy(item->data, data, len);
        buffer->total_bytes += len;
    }

    return SDL_TRUE;
}

SDL_bool buffer_append_fmt(Buffer *buffer, const char *fmt, ...)
{
    SDL_bool retval;
    va_list ap;
    va_start(ap, fmt);
    retval = buffer_append_va(buffer, fmt, ap);
    va_end(ap);
    return retval;
}

SDL_bool buffer_append_va(Buffer *buffer, const char *fmt, va_list va)
{
    char scratch[256];
    char *buf;
    size_t len;
    va_list ap;
    SDL_bool retval;

    va_copy(ap, va);
    len = SDL_vsnprintf(scratch, sizeof (scratch), fmt, ap);
    va_end(ap);

    /* If we overflowed our scratch buffer, heap allocate and try again. */

    if (len == 0) {
        return SDL_TRUE;  /* nothing to do. */
    } else if (len < sizeof (scratch)) {
        return buffer_append(buffer, scratch, len);
    }

    buf = (char *) buffer->m(len + 1, buffer->d);
    if (buf == NULL) {
        return SDL_FALSE;
    }

    va_copy(ap, va);
    SDL_vsnprintf(buf, len + 1, fmt, ap);  /* rebuild it. */
    va_end(ap);

    retval = buffer_append(buffer, buf, len);
    buffer->f(buf, buffer->d);
    return retval;
}

size_t buffer_size(Buffer *buffer)
{
    return buffer->total_bytes;
}

void buffer_empty(Buffer *buffer)
{
    BufferBlock *item = buffer->head;
    while (item != NULL) {
        BufferBlock *next = item->next;
        buffer->f(item, buffer->d);
        item = next;
    }
    buffer->head = buffer->tail = NULL;
    buffer->total_bytes = 0;
}

char *buffer_flatten(Buffer *buffer)
{
    char *retval = (char *) buffer->m(buffer->total_bytes + 1, buffer->d);
    if (retval != NULL) {
        BufferBlock *item = buffer->head;
        char *ptr = retval;
        while (item != NULL) {
            BufferBlock *next = item->next;
            SDL_memcpy(ptr, item->data, item->bytes);
            ptr += item->bytes;
            buffer->f(item, buffer->d);
            item = next;
        }
        *ptr = '\0';

        SDL_assert(ptr == (retval + buffer->total_bytes));

        buffer->head = buffer->tail = NULL;
        buffer->total_bytes = 0;
    }

    return retval;
}

char *buffer_merge(Buffer **buffers, const size_t n, size_t *_len)
{
    Buffer *first = NULL;
    char *retval;
    char *ptr;
    size_t len = 0;
    size_t i;

    for (i = 0; i < n; i++) {
        Buffer *buffer = buffers[i];
        if (buffer != NULL) {
            if (first == NULL) {
                first = buffer;
            }
            len += buffer->total_bytes;
        }
    }

    retval = (char *) (first ? first->m(len + 1, first->d) : NULL);
    if (retval == NULL) {
        *_len = 0;
        return NULL;
    }

    *_len = len;
    ptr = retval;
    for (i = 0; i < n; i++) {
        Buffer *buffer = buffers[i];
        if (buffer != NULL) {
            BufferBlock *item = buffer->head;
            while (item != NULL) {
                BufferBlock *next = item->next;
                SDL_memcpy(ptr, item->data, item->bytes);
                ptr += item->bytes;
                buffer->f(item, buffer->d);
                item = next;
            }

            buffer->head = buffer->tail = NULL;
            buffer->total_bytes = 0;
        }
    }

    SDL_assert(ptr == (retval + len));

    *ptr = '\0';

    return retval;
}

void buffer_destroy(Buffer *buffer)
{
    if (buffer != NULL) {
        SDL_SHADER_Free f = buffer->f;
        void *d = buffer->d;
        buffer_empty(buffer);
        f(buffer, d);
    }
}

static SDL_bool blockscmp(BufferBlock *item, const Uint8 *data, size_t len)
{
    if (len == 0) {
        return SDL_TRUE;  /* "match" */
    }

    while (item != NULL) {
        const size_t itemremain = item->bytes;
        const size_t avail = len < itemremain ? len : itemremain;
        if (SDL_memcmp(item->data, data, avail) != 0) {
            return SDL_FALSE;  /* not a match. */
        }
        if (len == avail) {
            return SDL_TRUE;   /* complete match! */
        }

        len -= avail;
        data += avail;
        item = item->next;
    }

    return SDL_FALSE;  /* not a complete match. */
}

const void *MemChr(const void *buf, const Uint8 b, size_t buflen)
{
    const Uint8 *ptr = (const Uint8 *) buf;
    while (buflen--) {
        if (*ptr == b) {
            return ptr;
        }
        ptr++;
    }
    return NULL;
}

ssize_t buffer_find(Buffer *buffer, const size_t start, const void *_data, const size_t len)
{
    if (len == 0) {
        return 0;  /* I guess that's right. */
    } else if (start >= buffer->total_bytes) {
        return -1;  /* definitely can't match. */
    } else if (len > (buffer->total_bytes - start)) {
        return -1;  /* definitely can't match. */
    } else {
        /* Find the start point somewhere in the center of a buffer. */
        BufferBlock *item = buffer->head;
        const Uint8 *ptr = item->data;
        size_t pos = 0;
        const Uint8 *data;
        Uint8 first;

        if (start > 0) {
            while (SDL_TRUE) {
                SDL_assert(item != NULL);
                if ((pos + item->bytes) > start) {  /* start is in this block. */
                    ptr = item->data + (start - pos);
                    break;
                }
                pos += item->bytes;

                item = item->next;
            }
        }

        /* okay, we're at the origin of the search. */
        SDL_assert(item != NULL);
        SDL_assert(ptr != NULL);

        data = (const Uint8 *) _data;
        first = *data;
        while (item != NULL) {
            const size_t itemremain = item->bytes - ((size_t)(ptr-item->data));
            ptr = (Uint8 *) MemChr(ptr, first, itemremain);
            while (ptr != NULL) {
                size_t itemremain, avail;
                const size_t retval = pos + ((size_t) (ptr - item->data));
                if (len == 1) {
                    return retval;  /* we're done, here it is! */
                }

                itemremain = item->bytes - ((size_t)(ptr-item->data));
                avail = len < itemremain ? len : itemremain;
                if ((avail == 0) || (SDL_memcmp(ptr, data, avail) == 0)) {
                    /* okay, we've got a (sub)string match! Move to the next block. Check all blocks until we get a complete match or a failure. */
                    if (blockscmp(item->next, data+avail, len-avail)) {
                        return (ssize_t) retval;
                    }
                }

                /* try again, further in this block. */
                ptr = (Uint8 *) MemChr(ptr + 1, first, itemremain - 1);
            }

            pos += item->bytes;
            item = item->next;
            if (item != NULL) {
                ptr = item->data;
            }
        }
    }

    return -1;  /* no match found. */
}

void *Malloc(Context *ctx, const size_t len)
{
    void *retval = ctx->malloc((int) len, ctx->malloc_data);
    if (retval == NULL) {
        ctx->isfail = SDL_TRUE;
        ctx->out_of_memory = SDL_TRUE;
    }
    return retval;
}

void Free(Context *ctx, void *ptr)
{
    ctx->free(ptr, ctx->malloc_data);
}

void *MallocContextBridge(size_t bytes, void *data)
{
    return Malloc((Context *) data, bytes);
}

void FreeContextBridge(void *ptr, void *data)
{
    Free((Context *) data, ptr);
}

char *StrDup(Context *ctx, const char *str)
{
    const size_t slen = SDL_strlen(str) + 1;
    char *retval = (char *) Malloc(ctx, slen);
    if (retval != NULL) {
        SDL_strlcpy(retval, str, slen);
    }
    return retval;
}

void fail(Context *ctx, const char *reason)
{
    ctx->isfail = SDL_TRUE;
    errorlist_add(ctx->errors, SDL_TRUE, ctx->filename, ctx->position, reason);
}

void fail_ast(Context *ctx, const SDL_SHADER_AstNodeInfo *ast, const char *reason)
{
    ctx->isfail = SDL_TRUE;
    errorlist_add(ctx->errors, SDL_TRUE, ast->filename, ast->line, reason);
}

void failf(Context *ctx, const char *fmt, ...)
{
    va_list ap;
    ctx->isfail = SDL_TRUE;
    va_start(ap, fmt);
    errorlist_add_va(ctx->errors, SDL_TRUE, ctx->filename, ctx->position, fmt, ap);
    va_end(ap);
}

void failf_ast(Context *ctx, const SDL_SHADER_AstNodeInfo *ast, const char *fmt, ...)
{
    va_list ap;
    ctx->isfail = SDL_TRUE;
    va_start(ap, fmt);
    errorlist_add_va(ctx->errors, SDL_TRUE, ast->filename, ast->line, fmt, ap);
    va_end(ap);
}

void warn(Context *ctx, const char *reason)
{
    errorlist_add(ctx->errors, SDL_FALSE, ctx->filename, ctx->position, reason);
}

void warn_ast(Context *ctx, const SDL_SHADER_AstNodeInfo *ast, const char *reason)
{
    errorlist_add(ctx->errors, SDL_FALSE, ast->filename, ast->line, reason);
}

void warnf(Context *ctx, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    errorlist_add_va(ctx->errors, SDL_FALSE, ctx->filename, ctx->position, fmt, ap);
    va_end(ap);
}

void warnf_ast(Context *ctx, const SDL_SHADER_AstNodeInfo *ast, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    errorlist_add_va(ctx->errors, SDL_FALSE, ast->filename, ast->line, fmt, ap);
    va_end(ap);
}


Context *context_create(SDL_SHADER_Malloc m, SDL_SHADER_Free f, void *d)
{
    Context *ctx;

    if ((m == NULL) != (f == NULL)) {
        return NULL;
    }

    if (!m) { m = SDL_SHADER_internal_malloc; }
    if (!f) { f = SDL_SHADER_internal_free; }

    ctx = (Context *) m(sizeof (Context), d);
    if (ctx == NULL) {
        return NULL;
    }

    SDL_zerop(ctx);
    ctx->malloc = m;
    ctx->free = f;
    ctx->malloc_data = d;

    ctx->errors = errorlist_create(MallocContextBridge, FreeContextBridge, ctx);
    if (!ctx->errors) { goto context_create_failed; }

    return ctx;

context_create_failed:
    context_destroy(ctx);
    return NULL;
}

void context_destroy(Context *ctx)
{
    if (ctx) {
        SDL_SHADER_Free f = ctx->free;
        void *d = ctx->malloc_data;

        errorlist_destroy(ctx->errors);
        preprocessor_end(ctx);
        ast_end(ctx);
        compiler_end(ctx);

        f(ctx, d);
    }
}

/* end of SDL_shader_common.c ... */

