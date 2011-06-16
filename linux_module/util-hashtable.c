/*
 * Palacios Hash Table 
 * (c) Lei Xia, 2011
 */
 
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/preempt.h>
#include <linux/sched.h>
 
#include "util-hashtable.h"


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

static inline uint_t do_hash(struct hashtable * htable, addr_t key) {
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
#ifdef __32BIT__
/* 2^31 + 2^29 - 2^25 + 2^22 - 2^19 - 2^16 + 1 */
#define GOLDEN_RATIO_PRIME 0x9e370001UL
//#define BITS_PER_LONG 32
#elif  defined(__64BIT__)
/*  2^63 + 2^61 - 2^57 + 2^54 - 2^51 - 2^18 + 1 */
#define GOLDEN_RATIO_PRIME 0x9e37fffffffc0001UL
//#define BITS_PER_LONG 64
#else
#error Define GOLDEN_RATIO_PRIME for your wordsize.
#endif

ulong_t palacios_hash_long(ulong_t val, uint_t bits) {
    ulong_t hash = val;

#ifdef __PALACIOS_64BIT__
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
ulong_t palacios_hash_buffer(uchar_t * msg, uint_t length) {
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

/* indexFor */
static inline uint_t indexFor(uint_t table_length, uint_t hash_value) {
    return (hash_value % table_length);
};

#define freekey(X) kfree(X)


static void * tmp_realloc(void * old_ptr, uint_t old_size, uint_t new_size) {
    void * new_buf = kmalloc(new_size, GFP_KERNEL);

    if (new_buf == NULL) {
	return NULL;
    }

    memcpy(new_buf, old_ptr, old_size);
    kfree(old_ptr);

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

const uint_t prime_table_len = sizeof(primes) / sizeof(primes[0]);

struct hashtable * palacios_create_htable(uint_t min_size,
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
    for (prime_index = 0; prime_index < prime_table_len; prime_index++) {
        if (primes[prime_index] > min_size) { 
	    size = primes[prime_index]; 
	    break; 
	}
    }

    htable = (struct hashtable *)kmalloc(sizeof(struct hashtable), GFP_KERNEL);

    if (htable == NULL) {
	return NULL; /*oom*/
    }

    htable->table = (struct hash_entry **)kmalloc(sizeof(struct hash_entry*) * size, GFP_KERNEL);

    if (htable->table == NULL) { 
	kfree(htable); 
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


static int hashtable_expand(struct hashtable * htable) {
    /* Double the size of the table to accomodate more entries */
    struct hash_entry ** new_table;
    struct hash_entry * tmp_entry;
    struct hash_entry ** entry_ptr;
    uint_t new_size;
    uint_t i;
    uint_t index;

    /* Check we're not hitting max capacity */
    if (htable->prime_index == (prime_table_len - 1)) {
	return 0;
    }

    new_size = primes[++(htable->prime_index)];

    new_table = (struct hash_entry **)kmalloc(sizeof(struct hash_entry*) * new_size, GFP_KERNEL);

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

        kfree(htable->table);

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

uint_t palacios_htable_count(struct hashtable * htable) {
    return htable->entry_count;
}

int palacios_htable_insert(struct hashtable * htable, addr_t key, addr_t value) {
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

    new_entry = (struct hash_entry *)kmalloc(sizeof(struct hash_entry), GFP_KERNEL);

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


int palacios_htable_change(struct hashtable * htable, addr_t key, addr_t value, int free_value) {
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
		kfree((void *)(tmp_entry->value));
	    }

	    tmp_entry->value = value;
	    return -1;
        }
        tmp_entry = tmp_entry->next;
    }
    return 0;
}



int palacios_htable_inc(struct hashtable * htable, addr_t key, addr_t value) {
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


int palacios_htable_dec(struct hashtable * htable, addr_t key, addr_t value) {
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


/* returns value associated with key */
addr_t palacios_htable_search(struct hashtable * htable, addr_t key) {
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


/* returns value associated with key */
addr_t palacios_htable_remove(struct hashtable * htable, addr_t key, int free_key) {
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
	    kfree(cursor);
      
	    return value;
	}

	entry_ptr = &(cursor->next);
	cursor = cursor->next;
    }
    return (addr_t)NULL;
}


/* destroy */
void palacios_free_htable(struct hashtable * htable, int free_values, int free_keys) {
    uint_t i;
    struct hash_entry * cursor;
    struct hash_entry * tmp;
    struct hash_entry **table = htable->table;

    if (free_values) {
	for (i = 0; i < htable->table_length; i++) {
	    cursor = table[i];
      
	    while (cursor != NULL) { 
		tmp = cursor; 
		cursor = cursor->next; 

		if (free_keys) {
		    freekey((void *)(tmp->key)); 
		}
		kfree((void *)(tmp->value)); 
		kfree(tmp); 
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
		kfree(tmp); 
	    }
	}
    }
  
    kfree(htable->table);
    kfree(htable);
}
