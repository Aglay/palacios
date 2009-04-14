/*
  Copyright (c) 2002, 2004, Christopher Clark
  All rights reserved.
  
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
  
  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
  
  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
  
  * Neither the name of the original author; nor the names of any contributors
    may be used to endorse or promote products derived from this software
    without specific prior written permission.
  
  
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* Modifications made by Jack Lange <jarusl@cs.northwestern.edu> */



#include <palacios/vmm.h>
#include <palacios/vmm_hashtable.h>
#include <palacios/vmm_string.h>





struct hash_entry {
    addr_t key;
    addr_t value;
    uint_t hash;
    struct hash_entry * next;
};

struct hashtable {
    uint_t table_length;
    struct hash_entry ** table;
    uint_t entry_count;
    uint_t load_limit;
    uint_t prime_index;
    uint_t (*hash_fn) (addr_t key);
    int (*eq_fn) (addr_t key1, addr_t key2);
};



/* HASH FUNCTIONS */



uint_t do_hash(struct hashtable * htable, addr_t key) {
    /* Aim to protect against poor hash functions by adding logic here
     * - logic taken from java 1.4 hashtable source */
    uint_t i = htable->hash_fn(key);
    i += ~(i << 9);
    i ^=  ((i >> 14) | (i << 18)); /* >>> */
    i +=  (i << 4);
    i ^=  ((i >> 10) | (i << 22)); /* >>> */

    return i;
}


/* HASH AN UNSIGNED LONG */
/* LINUX UNSIGHED LONG HASH FUNCTION */
#ifdef __V3_32BIT__
/* 2^31 + 2^29 - 2^25 + 2^22 - 2^19 - 2^16 + 1 */
#define GOLDEN_RATIO_PRIME 0x9e370001UL
#define BITS_PER_LONG 32
#elif  defined(__V3_64BIT__)
/*  2^63 + 2^61 - 2^57 + 2^54 - 2^51 - 2^18 + 1 */
#define GOLDEN_RATIO_PRIME 0x9e37fffffffc0001UL
#define BITS_PER_LONG 64
#else
#error Define GOLDEN_RATIO_PRIME for your wordsize.
#endif

ulong_t hash_long(ulong_t val, uint_t bits) {
    ulong_t hash = val;

#ifdef __V3_64BIT__
    /*  Sigh, gcc can't optimise this alone like it does for 32 bits. */
    ulong_t n = hash;
    n <<= 18;
    hash -= n;
    n <<= 33;
    hash -= n;
    n <<= 3;
    hash += n;
    n <<= 3;
    hash -= n;
    n <<= 4;
    hash += n;
    n <<= 2;
    hash += n;
#else
    /* On some cpus multiply is faster, on others gcc will do shifts */
    hash *= GOLDEN_RATIO_PRIME;
#endif

    /* High bits are more random, so use them. */
    return hash >> (BITS_PER_LONG - bits);
}

/* HASH GENERIC MEMORY BUFFER */
/* ELF HEADER HASH FUNCTION */
ulong_t hash_buffer(uchar_t * msg, uint_t length) {
    ulong_t hash = 0;
    ulong_t temp = 0;
    uint_t i;

    for (i = 0; i < length; i++) {
	hash = (hash << 4) + *(msg + i) + i;
	if ((temp = (hash & 0xF0000000))) {
	    hash ^= (temp >> 24);
	}
	hash &= ~temp;
    }
    return hash;
}



/*****************************************************************************/
/* indexFor */
static inline uint_t indexFor(uint_t table_length, uint_t hash_value) {
    return (hash_value % table_length);
};

/* Only works if table_length == 2^N */
/*
  static inline uint_t indexFor(uint_t table_length, uint_t hashvalue)
  {
  return (hashvalue & (table_length - 1u));
  }
*/

/*****************************************************************************/
#define freekey(X) V3_Free(X)
/*define freekey(X) ; */


static void * tmp_realloc(void * old_ptr, uint_t old_size, uint_t new_size) {
    void * new_buf = V3_Malloc(new_size);

    if (new_buf == NULL) {
	return NULL;
    }

    memcpy(new_buf, old_ptr, old_size);
    V3_Free(old_ptr);

    return new_buf;
}


/*
  Credit for primes table: Aaron Krowne
  http://br.endernet.org/~akrowne/
  http://planetmath.org/encyclopedia/GoodHashTablePrimes.html
*/
static const uint_t primes[] = { 
    53, 97, 193, 389,
    769, 1543, 3079, 6151,
    12289, 24593, 49157, 98317,
    196613, 393241, 786433, 1572869,
    3145739, 6291469, 12582917, 25165843,
    50331653, 100663319, 201326611, 402653189,
    805306457, 1610612741 };


// this assumes that the max load factor is .65
static const uint_t load_factors[] = {
    35, 64, 126, 253,
    500, 1003, 2002, 3999,
    7988, 15986, 31953, 63907,
    127799, 255607, 511182, 1022365,
    2044731, 4089455, 8178897, 16357798,
    32715575, 65431158, 130862298, 261724573,
    523449198, 1046898282 };

const uint_t prime_table_length = sizeof(primes) / sizeof(primes[0]);



/*****************************************************************************/
struct hashtable * create_hashtable(uint_t min_size,
				    uint_t (*hash_fn) (addr_t),
				    int (*eq_fn) (addr_t, addr_t)) {
    struct hashtable * htable;
    uint_t prime_index;
    uint_t size = primes[0];

    /* Check requested hashtable isn't too large */
    if (min_size > (1u << 30)) {
	return NULL;
    }

    /* Enforce size as prime */
    for (prime_index = 0; prime_index < prime_table_length; prime_index++) {
        if (primes[prime_index] > min_size) { 
	    size = primes[prime_index]; 
	    break; 
	}
    }

    htable = (struct hashtable *)V3_Malloc(sizeof(struct hashtable));

    if (htable == NULL) {
	return NULL; /*oom*/
    }

    htable->table = (struct hash_entry **)V3_Malloc(sizeof(struct hash_entry*) * size);

    if (htable->table == NULL) { 
	V3_Free(htable); 
	return NULL;  /*oom*/
    }


    memset(htable->table, 0, size * sizeof(struct hash_entry *));

    htable->table_length  = size;
    htable->prime_index   = prime_index;
    htable->entry_count   = 0;
    htable->hash_fn       = hash_fn;
    htable->eq_fn         = eq_fn;
    htable->load_limit    = load_factors[prime_index];

    return htable;
}



/*****************************************************************************/
static int hashtable_expand(struct hashtable * htable) {
    /* Double the size of the table to accomodate more entries */
    struct hash_entry ** new_table;
    struct hash_entry * tmp_entry;
    struct hash_entry ** entry_ptr;
    uint_t new_size;
    uint_t i;
    uint_t index;

    /* Check we're not hitting max capacity */
    if (htable->prime_index == (prime_table_length - 1)) {
	return 0;
    }

    new_size = primes[++(htable->prime_index)];

    new_table = (struct hash_entry **)V3_Malloc(sizeof(struct hash_entry*) * new_size);

    if (new_table != NULL) {
        memset(new_table, 0, new_size * sizeof(struct hash_entry *));
        /* This algorithm is not 'stable'. ie. it reverses the list
         * when it transfers entries between the tables */

        for (i = 0; i < htable->table_length; i++) {

	    while ((tmp_entry = htable->table[i]) != NULL) {
		htable->table[i] = tmp_entry->next;
	   
		index = indexFor(new_size, tmp_entry->hash);
	    
		tmp_entry->next = new_table[index];
	    
		new_table[index] = tmp_entry;
	    }
        }

        V3_Free(htable->table);

        htable->table = new_table;
    } else {
	/* Plan B: realloc instead */

	//new_table = (struct hash_entry **)realloc(htable->table, new_size * sizeof(struct hash_entry *));
	new_table = (struct hash_entry **)tmp_realloc(htable->table, primes[htable->prime_index - 1], 
						      new_size * sizeof(struct hash_entry *));

	if (new_table == NULL) {
	    (htable->prime_index)--;
	    return 0;
	}

	htable->table = new_table;

	memset(new_table[htable->table_length], 0, new_size - htable->table_length);

	for (i = 0; i < htable->table_length; i++) {

	    for (entry_ptr = &(new_table[i]), tmp_entry = *entry_ptr; 
		 tmp_entry != NULL; 
		 tmp_entry = *entry_ptr) {

		index = indexFor(new_size, tmp_entry->hash);

		if (i == index) {
		    entry_ptr = &(tmp_entry->next);
		} else {
		    *entry_ptr = tmp_entry->next;
		    tmp_entry->next = new_table[index];
		    new_table[index] = tmp_entry;
		}
	    }
	}
    }

    htable->table_length = new_size;

    htable->load_limit   = load_factors[htable->prime_index];

    return -1;
}

/*****************************************************************************/
uint_t hashtable_count(struct hashtable * htable) {
    return htable->entry_count;
}

/*****************************************************************************/
int hashtable_insert(struct hashtable * htable, addr_t key, addr_t value) {
    /* This method allows duplicate keys - but they shouldn't be used */
    uint_t index;
    struct hash_entry * new_entry;

    if (++(htable->entry_count) > htable->load_limit) {
	/* Ignore the return value. If expand fails, we should
	 * still try cramming just this value into the existing table
	 * -- we may not have memory for a larger table, but one more
	 * element may be ok. Next time we insert, we'll try expanding again.*/
	hashtable_expand(htable);
    }


    new_entry = (struct hash_entry *)V3_Malloc(sizeof(struct hash_entry));

    if (new_entry == NULL) { 
	(htable->entry_count)--; 
	return 0; /*oom*/
    }

    new_entry->hash = do_hash(htable, key);

    index = indexFor(htable->table_length, new_entry->hash);

    new_entry->key = key;
    new_entry->value = value;

    new_entry->next = htable->table[index];

    htable->table[index] = new_entry;

    return -1;
}



int hashtable_change(struct hashtable * htable, addr_t key, addr_t value, int free_value) {
    struct hash_entry * tmp_entry;
    uint_t hash_value;
    uint_t index;

    hash_value = do_hash(htable, key);

    index = indexFor(htable->table_length, hash_value);

    tmp_entry = htable->table[index];

    while (tmp_entry != NULL) {
        /* Check hash value to short circuit heavier comparison */
        if ((hash_value == tmp_entry->hash) && (htable->eq_fn(key, tmp_entry->key))) {

	    if (free_value) {
		V3_Free((void *)(tmp_entry->value));
	    }

	    tmp_entry->value = value;
	    return -1;
        }
        tmp_entry = tmp_entry->next;
    }
    return 0;
}



int hashtable_inc(struct hashtable * htable, addr_t key, addr_t value) {
    struct hash_entry * tmp_entry;
    uint_t hash_value;
    uint_t index;

    hash_value = do_hash(htable, key);

    index = indexFor(htable->table_length, hash_value);

    tmp_entry = htable->table[index];

    while (tmp_entry != NULL) {
        /* Check hash value to short circuit heavier comparison */
        if ((hash_value == tmp_entry->hash) && (htable->eq_fn(key, tmp_entry->key))) {

	    tmp_entry->value += value;
	    return -1;
        }
        tmp_entry = tmp_entry->next;
    }
    return 0;
}


int hashtable_dec(struct hashtable * htable, addr_t key, addr_t value) {
    struct hash_entry * tmp_entry;
    uint_t hash_value;
    uint_t index;

    hash_value = do_hash(htable, key);

    index = indexFor(htable->table_length, hash_value);

    tmp_entry = htable->table[index];

    while (tmp_entry != NULL) {
        /* Check hash value to short circuit heavier comparison */
        if ((hash_value == tmp_entry->hash) && (htable->eq_fn(key, tmp_entry->key))) {

	    tmp_entry->value -= value;
	    return -1;
        }
        tmp_entry = tmp_entry->next;
    }
    return 0;
}




/*****************************************************************************/
/* returns value associated with key */
addr_t hashtable_search(struct hashtable * htable, addr_t key) {
    struct hash_entry * cursor;
    uint_t hash_value;
    uint_t index;
  
    hash_value = do_hash(htable, key);
  
    index = indexFor(htable->table_length, hash_value);
  
    cursor = htable->table[index];
  
    while (cursor != NULL) {
	/* Check hash value to short circuit heavier comparison */
	if ((hash_value == cursor->hash) && 
	    (htable->eq_fn(key, cursor->key))) {
	    return cursor->value;
	}
    
	cursor = cursor->next;
    }
  
    return (addr_t)NULL;
}

/*****************************************************************************/
/* returns value associated with key */
addr_t hashtable_remove(struct hashtable * htable, addr_t key, int free_key) {
    /* TODO: consider compacting the table when the load factor drops enough,
     *       or provide a 'compact' method. */
  
    struct hash_entry * cursor;
    struct hash_entry ** entry_ptr;
    addr_t value;
    uint_t hash_value;
    uint_t index;
  
    hash_value = do_hash(htable, key);

    index = indexFor(htable->table_length, hash_value);

    entry_ptr = &(htable->table[index]);
    cursor = *entry_ptr;

    while (cursor != NULL) {
	/* Check hash value to short circuit heavier comparison */
	if ((hash_value == cursor->hash) && 
	    (htable->eq_fn(key, cursor->key))) {
     
	    *entry_ptr = cursor->next;
	    htable->entry_count--;
	    value = cursor->value;
      
	    if (free_key) {
		freekey((void *)(cursor->key));
	    }
	    V3_Free(cursor);
      
	    return value;
	}

	entry_ptr = &(cursor->next);
	cursor = cursor->next;
    }
    return (addr_t)NULL;
}

/*****************************************************************************/
/* destroy */
void hashtable_destroy(struct hashtable * htable, int free_values, int free_keys) {
    uint_t i;
    struct hash_entry * cursor;;
    struct hash_entry **table = htable->table;

    if (free_values) {
	for (i = 0; i < htable->table_length; i++) {
	    cursor = table[i];
      
	    while (cursor != NULL) { 
		struct hash_entry * tmp;

		tmp = cursor; 
		cursor = cursor->next; 

		if (free_keys) {
		    freekey((void *)(tmp->key)); 
		}
		V3_Free((void *)(tmp->value)); 
		V3_Free(tmp); 
	    }
	}
    } else {
	for (i = 0; i < htable->table_length; i++) {
	    cursor = table[i];

	    while (cursor != NULL) { 
		struct hash_entry * tmp;

		tmp = cursor; 
		cursor = cursor->next; 
	
		if (free_keys) {
		    freekey((void *)(tmp->key)); 
		}
		V3_Free(tmp); 
	    }
	}
    }
  
    V3_Free(htable->table);
    V3_Free(htable);
}




/* HASH TABLE ITERATORS */



struct hashtable_iter * create_hashtable_iterator(struct hashtable * htable) {
    uint_t i;
    uint_t table_length;
    
    struct hashtable_iter * iter = (struct hashtable_iter *)V3_Malloc(sizeof(struct hashtable_iter));

    if (iter == NULL) {
	return NULL;
    }

    iter->htable = htable;
    iter->entry = NULL;
    iter->parent = NULL;
    table_length = htable->table_length;
    iter->index = table_length;

    if (htable->entry_count == 0) {
	return iter;
    }
 
    for (i = 0; i < table_length; i++) {
	if (htable->table[i] != NULL) {
	    iter->entry = htable->table[i];
	    iter->index = i;
	    break;
	}
    }

    return iter;
}


addr_t hashtable_get_iter_key(struct hashtable_iter * iter) {
    return iter->entry->key; 
}

addr_t hashtable_get_iter_value(struct hashtable_iter * iter) { 
    return iter->entry->value; 
}


/* advance - advance the iterator to the next element
 *           returns zero if advanced to end of table */
int hashtable_iterator_advance(struct hashtable_iter * iter) {
    uint_t j;
    uint_t table_length;
    struct hash_entry ** table;
    struct hash_entry * next;
  
    if (iter->entry == NULL) {
	return 0; /* stupidity check */
    }

  
    next = iter->entry->next;

    if (next != NULL) {
	iter->parent = iter->entry;
	iter->entry = next;
	return -1;
    }
   
    table_length = iter->htable->table_length;
    iter->parent = NULL;
    
    if (table_length <= (j = ++(iter->index))) {
	iter->entry = NULL;
	return 0;
    }
   
    table = iter->htable->table;

    while ((next = table[j]) == NULL) {
	if (++j >= table_length) {
	    iter->index = table_length;
	    iter->entry = NULL;
	    return 0;
	}
    }
   
    iter->index = j;
    iter->entry = next;

    return -1;
}


/* remove - remove the entry at the current iterator position
 *          and advance the iterator, if there is a successive
 *          element.
 *          If you want the value, read it before you remove:
 *          beware memory leaks if you don't.
 *          Returns zero if end of iteration. */
int hashtable_iterator_remove(struct hashtable_iter * iter, int free_key) {
    struct hash_entry * remember_entry; 
    struct hash_entry * remember_parent;
    int ret;

    /* Do the removal */
    if ((iter->parent) == NULL) {
	/* element is head of a chain */
	iter->htable->table[iter->index] = iter->entry->next;
    } else {
	/* element is mid-chain */
	iter->parent->next = iter->entry->next;
    }

  
    /* itr->e is now outside the hashtable */
    remember_entry = iter->entry;
    iter->htable->entry_count--;
    if (free_key) {
	freekey((void *)(remember_entry->key));
    }

    /* Advance the iterator, correcting the parent */
    remember_parent = iter->parent;
    ret = hashtable_iterator_advance(iter);

    if (iter->parent == remember_entry) { 
	iter->parent = remember_parent; 
    }
  
    V3_Free(remember_entry);
    return ret;
}


/* returns zero if not found */
int hashtable_iterator_search(struct hashtable_iter * iter,
			      struct hashtable * htable, addr_t key) {
    struct hash_entry * entry;
    struct hash_entry * parent;
    uint_t hash_value;
    uint_t index;
  
    hash_value = do_hash(htable, key);
    index = indexFor(htable->table_length, hash_value);
  
    entry = htable->table[index];
    parent = NULL;
  
    while (entry != NULL) {
	/* Check hash value to short circuit heavier comparison */
	if ((hash_value == entry->hash) && 
	    (htable->eq_fn(key, entry->key))) {
	    iter->index = index;
	    iter->entry = entry;
	    iter->parent = parent;
	    iter->htable = htable;
	    return -1;
	}
	parent = entry;
	entry = entry->next;
    }
    return 0;
}
 
 













/*
 * Copyright (c) 2002, Christopher Clark
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * * Neither the name of the original author; nor the names of any contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 * 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
