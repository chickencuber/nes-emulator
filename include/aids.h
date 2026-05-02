#ifndef AIDS
#define AIDS

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>


#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#define IMPORT __declspec(dllimport)
#else
#define EXPORT 
#define IMPORT
#endif

#define STRING "%.*s"
#define FMTSTRING(s) (int)(s)->len, (s)->data

#define BOOL "%s"
#define FMTBOOL(s) (s? "true": "false")

#define auto __auto_type

//must be first in a struct
#define IMPL_ALLOCATOR\
    fun_alloc alloc;\
    fun_calloc calloc;\
    fun_realloc realloc;\
    fun_free free;\

typedef struct Allocator Allocator;
typedef void*(*fun_alloc)(Allocator*, size_t);
typedef void*(*fun_realloc)(Allocator*, void*, size_t);
typedef void*(*fun_calloc)(Allocator*, size_t, size_t);
typedef void(*fun_free)(Allocator*, void*);

struct Allocator {
    IMPL_ALLOCATOR;
};

IMPORT extern Allocator DEFAULT_ALLOCATOR;

#define ALLOC(allocator, size) (allocator)->alloc((Allocator*)(allocator), size)
#define CALLOC(alloc, nmemb, size) (alloc)->calloc((Allocator*)(alloc), nmemb, size)
#define REALLOC(alloc, ptr, size) (alloc)->realloc((Allocator*)(alloc), ptr, size)
#define FREE(alloc, ptr) (alloc)->free((Allocator*)(alloc), ptr)

#ifndef DA_INIT_CAP
#define DA_INIT_CAP 256
#endif

#define panic(...)\
    do {\
        fprintf(stderr, __VA_ARGS__);\
        exit(1);\
    }while(0)

#define assert(c, ...)\
    do {\
        if(!(c)) {\
            panic(__VA_ARGS__);\
        }\
    } while(0)

//doesn't matter where in the struct it is
#define IMPL_DA(type)\
    Allocator* alloc;\
size_t len;\
size_t cap;\
type* data;

#define DA(type)\
    struct {\
        IMPL_DA(type);\
    }


#define da_init(da, allocator)\
    do {\
        (da)->alloc = (Allocator*)allocator;\
        (da)->cap = 0;\
        (da)->len = 0;\
        (da)->data = NULL;\
    } while(0)

#define da_init_if_not(da, allocator)\
    do {\
        if(!(da)->alloc) {\
            da_init(da, allocator);\
        }\
    } while(0)

#define da_reserve(da, size)\
    do {\
        assert((da)->alloc, "DA not initialized\n");\
        if((size)>(da)->cap) {\
            (da)->cap = size;\
            (da)->data = REALLOC((da)->alloc, (da)->data, sizeof(*(da)->data) * (da)->cap);\
            assert((da)->data != NULL, "But more RAM LOL\n");\
        }\
    } while(0)

#define da_append(da, el)\
    do {\
        while((da)->len >= (da)->cap) {\
            da_reserve((da), (da)->cap? (da)->cap*2: DA_INIT_CAP);\
        }\
        (da)->data[(da)->len++] = el;\
    } while(0)

#define da_free(da)\
    do{\
        if((da)->alloc) {\
            FREE((da)->alloc, (da)->data);\
            (da)->data = NULL;\
            (da)->len = 0;\
            (da)->cap = 0;\
        }\
    } while(0)

#define da_free_call(da, c)\
    do{\
        if((da)->alloc) {\
            da_foreach(b, da) c\
            FREE((da)->alloc, (da)->data);\
            (da)->data = NULL;\
            (da)->len = 0;\
            (da)->cap = 0;\
        }\
    } while(0)

#define da_foreach(it, da) for (typeof(*(da)->data)* it = (da)->data; it < (da)->data + (da)->len; ++it)
#define da_foreach_enum(idx, it, da) for(size_t idx = 0; idx < (da)->len; idx++)\
                                                      for(typeof(*(da)->data)* it = &(da)->data[idx]; it != NULL; it = NULL)

#define fa_len(fa) sizeof(fa)/sizeof(*fa)
#define fa_foreach(it, fa) for (typeof(*(fa))* it = (fa); it < (fa) + fa_len(fa); ++it)
#define fa_foreach_len(it, fa, size) for (typeof(*(fa))* it = (fa); it < (fa) + size; ++it)
#define fa_foreach_enum(idx, it, fa) for(size_t idx = 0; idx < fa_len(fa); idx++)\
                                                      for(typeof(*(fa))* it = &(fa)[idx]; it != NULL; it = NULL)
#define fa_foreach_enum_len(idx, it, fa, size) for(size_t idx = 0; idx < size; idx++)\
                                                                for(typeof(*(fa))* it = &(fa)[idx]; it != NULL; it = NULL)

#define da_append_da(da, from)\
    do{\
        da_foreach(v, from) {\
            da_append(da, *v);\
        }\
    } while(0)

#define da_append_fa(da, from)\
    do{\
        fa_foreach(v, from) {\
            da_append(da, *v);\
        }\
    } while(0)

#define da_clone(da, allocator)\
    ({\
     typeof(*da) n = {0};\
     da_init(&n, allocator);\
     da_append_da(&n, da);\
     n;\
     })

#define da_pop(da) ((da)->data[--(da)->len])

#define da_get_last(da) ((da)->data[(da)->len-1])

#define da_set_last(da, v) ((da)->data[(da)->len-1]) = v

#define da_get(da, i) (da)->data[i]
#define da_set(da, i, v) (da)->data[i] = v


#define da_remove_unordered(da, idx)\
    ({\
     size_t _i = (idx);\
     assert(_i < (da)->len, "Index out of bounds\n");\
     typeof(*(da)->data) a = da_get_last(da);\
     typeof(*(da)->data) b = da_get(da, _i);\
     da_set_last(da, b);\
     da_set(da, _i, a);\
     da_pop(da);\
     })

//TASK(20260217-105205-686-n6-024): add more things to the da

typedef struct {
    IMPL_ALLOCATOR;
    Allocator* allocator;
    DA(void*) pointers;
} TrackingAllocator;

IMPORT void tracking_destroy(TrackingAllocator*);
IMPORT TrackingAllocator tracking_create(Allocator*);

#define UNUSED(a) (void)(a)

typedef struct {
    IMPL_ALLOCATOR;
    Allocator* allocator;
    uint8_t* data;
    size_t offset, cap;
} ArenaAllocator;

IMPORT void arena_destroy(ArenaAllocator*);
IMPORT ArenaAllocator arena_create(Allocator*, size_t);
IMPORT void arena_free_all(ArenaAllocator*);

#ifndef REGION_SIZE
#define REGION_SIZE 1024 * 64 //64 KiB
#endif


typedef struct Region Region;
struct Region {
    uint8_t* data;
    size_t offset, cap;
    Region* next;
};

typedef struct {
    IMPL_ALLOCATOR;
    Allocator* allocator;
    Region* first, *last;
} ScratchAllocator;

IMPORT void scratch_destroy(ScratchAllocator*);
IMPORT ScratchAllocator scratch_create(Allocator*);

//TASK(20260217-112915-392-n6-805): add more allocators

typedef DA(char) String;

IMPORT bool string_compare(String, String);
IMPORT bool str_cmp(const char*, const char*);
IMPORT String string_from(const char*, Allocator*);
IMPORT String string_clone(String*, Allocator*);
IMPORT String string_create(Allocator*);
IMPORT char* from_string(String*, Allocator*);
IMPORT void append_char(String*, char);
IMPORT void append_cstr(String*, char*);
IMPORT void append_string(String*, String*);

#ifndef HM_SIZE
#define HM_SIZE 103 
#endif

#define KV(ktype, vtype)\
    struct {\
        ktype key;\
        vtype value;\
    }

#define IMPL_HM(ktype, vtype)\
    Allocator* alloc;\
size_t(*hash)(ktype);\
DA(KV(ktype, vtype)) buckets[HM_SIZE];

#define HM(ktype, vtype)\
    struct {\
        IMPL_HM(ktype, vtype);\
    }

#ifndef HASHES
#define HASHES
#endif

#define _hm_hash(x) _Generic((x), _HASH_CASES HASHES)
#define _HASH_CASES \
    const char*: hm_hash_cstr,\
char*: (size_t(*)(char*))hm_hash_cstr,\
String: hm_hash_string

#define _DUMMY_KEY(hm) ((typeof((hm)->buckets[0].data->key)){0})
IMPORT size_t hm_hash_cstr(const char*);
IMPORT size_t hm_hash_int(int);
IMPORT size_t hm_hash_string(String);

#define hm_init(hm, allocator)\
    do {\
        (hm)->alloc = (Allocator*)allocator;\
        (hm)->hash = _hm_hash(_DUMMY_KEY(hm));\
    } while(0)

#ifndef COMPARE
#define COMPARE
#endif

#define _EQUALS(a, b) _Generic((a),\
        const char*: str_cmp,\
        String:string_compare\
        COMPARE\
        )(a, b)

#define hm_set(hm, _nkey, _nvalue)\
    do {\
        typeof(_nkey) nkey = _nkey;\
        typeof(_nvalue) nvalue = _nvalue;\
        bool done = false;\
        size_t index = (hm)->hash(nkey)%HM_SIZE;\
        da_init_if_not(&(hm)->buckets[index], (hm)->alloc);\
        da_foreach(i, &((hm)->buckets[index])) {\
            if(_EQUALS(i->key, nkey)){\
                i->value = nvalue;\
                done = true;\
                break;\
            }\
        }\
        if(!done) {\
            da_append(&(hm)->buckets[index], ((typeof(*(hm)->buckets[index].data)) {\
                        .key=nkey,\
                        .value=nvalue,\
                        }));\
        }\
    } while(0)

#define hm_get(hm, _nkey)\
    ({\
     typeof(_nkey) nkey = _nkey;\
     typeof(((hm)->buckets[0].data->value))* _res = NULL;\
     size_t index = (hm)->hash(nkey)%HM_SIZE;\
     da_init_if_not(&(hm)->buckets[index], (hm)->alloc);\
     da_foreach(i, &((hm)->buckets[index])) {\
     if(_EQUALS(i->key, nkey)){\
     _res = &i->value;\
     break;\
     }\
     }\
     _res;\
     })

#define hm_has(hm, key) (hm_get(hm, key) != NULL)

#define hm_free(hm)\
    do {\
        for(size_t i = 0; i < HM_SIZE; i++) {\
            da_free(&(hm)->buckets[i]);\
        }\
    } while(0)

#define hm_free_call(hm, c)\
    do {\
        for(size_t i = 0; i < HM_SIZE; i++) {\
            da_free_call(&(hm)->buckets[i], c);\
        }\
    } while(0)

#define hm_remove(hm, nkey)\
    ({\
     typeof(nkey) _nkey = nkey;\
     size_t index = (hm)->hash(_nkey)%HM_SIZE;\
     typeof(*((hm)->buckets[0].data)) _res = {0};\
     for(size_t i = 0; i < (hm)->buckets[index].len; i++){\
     if(_EQUALS((hm)->buckets[index].data[i].key, _nkey)){\
     _res=da_remove_unordered(&(hm)->buckets[index], i);\
     break;\
     }\
     }\
     _res;\
     })

#define hm_keys(hm, allocator)\
    ({\
     DA(typeof(((hm)->buckets[0].data->key))) _res = {0};\
     da_init(&_res, allocator);\
     for(size_t i = 0; i < HM_SIZE; i++){\
     da_init_if_not(&(hm)->buckets[i], (hm)->alloc);\
     da_foreach(v, &(hm)->buckets[i]) {\
     da_append(&_res, v->key);\
     }\
     }\
     _res;\
     })

#define hm_values(hm, allocator)\
    ({\
     DA(typeof(((hm)->buckets[0].data->value))) _res = {0};\
     da_init(&_res, allocator);\
     for(size_t i = 0; i < HM_SIZE; i++){\
     da_init_if_not(&(hm)->buckets[i], (hm)->alloc);\
     da_foreach(v, &(hm)->buckets[i]) {\
     da_append(&_res, v->value);\
     }\
     }\
     _res;\
     })

#define hm_entries(hm, allocator)\
    ({\
     DA(typeof(*((hm)->buckets[0].data))) _res = {0};\
     da_init(&_res, allocator);\
     for(size_t i = 0; i < HM_SIZE; i++){\
     da_init_if_not(&(hm)->buckets[i], (hm)->alloc);\
     da_foreach(v, &(hm)->buckets[i]) {\
     da_append(&_res, *v);\
     }\
     }\
     _res;\
     })
//TASK(20260218-083256-729-n6-649): add more functions for hashmaps


//WARNING requires `-fms-extensions` to function
#ifdef AIDS_CLASSES
#define StartClass(name)\
    typedef struct name name;\
struct name##_

//should error if the vaargs is more than one
#define EndClass(name,...)\
    typedef struct name name;\
struct name {\
    __VA_ARGS__;\
    struct name##_;\
};

#define constructor(name, ...)\
    void name##_new (name* self, ##__VA_ARGS__)

#define new_stack(T, ...) ({\
        T var={0};\
        T##_new(&var, ##__VA_ARGS__);\
        var;\
        })
#define new_heap(alloc, T, ...) ({\
        T* var = ALLOC(alloc, sizeof(T));\
        *var = (T){0};\
        T##_new(var, ##__VA_ARGS__);\
        var;\
        })

#define SUPER(T, ...) \
    T##_new((T*) self, ##__VA_ARGS__);

#define METHOD(R, name, ...) \
    R(*name)(__VA_ARGS__)
#define CALL(self, name, ...) (self)->name((self), ##__VA_ARGS__)
#endif

#define FORMAT(alloc, text, ...)\
    ({\
     int __size = snprintf(NULL, 0, text, ##__VA_ARGS__);\
     char* __txt = ALLOC(alloc, __size+1);\
     sprintf(__txt, text, ##__VA_ARGS__);\
     __txt;\
     })

#endif

#ifdef AIDS_IMPLEMENTATION
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void* dmalloc(Allocator* _, size_t size) {
    return malloc(size);
}
void* drealloc(Allocator* _, void* ptr, size_t size) {
    return realloc(ptr, size);
}
void* dcalloc(Allocator* _, size_t nmemb, size_t size) {
    return calloc(nmemb, size);
}
void dfree(Allocator* _, void* ptr) {
    free(ptr);
}

EXPORT Allocator DEFAULT_ALLOCATOR = {
    .alloc = dmalloc,
    .realloc = drealloc,
    .calloc = dcalloc,
    .free = dfree,
};

void* tracking_alloc_i(TrackingAllocator* self, size_t size) {
    void* ptr = ALLOC(self->allocator, size);
    if(ptr == NULL) return NULL;
    da_foreach(pointer, &self->pointers) {
        //pointer is void**;
        if(*pointer == NULL) {
            *pointer = ptr;
            return ptr;
        }
    }
    da_append(&self->pointers, ptr);
    return ptr;
}

void* tracking_calloc_i(TrackingAllocator* self, size_t nmemb, size_t size) {
    void* ptr = CALLOC(self->allocator, nmemb, size);
    if(ptr == NULL) return NULL;
    da_foreach(pointer, &self->pointers) {
        //pointer is void**;
        if(*pointer == NULL) {
            *pointer = ptr;
            return ptr;
        }
    }
    da_append(&self->pointers, ptr);
    return ptr;
}

void* tracking_realloc_i(TrackingAllocator* self, void* ptr, size_t size) {
    if(ptr == NULL) return ALLOC(self, size);
    da_foreach(pointer, &self->pointers) {
        //pointer is a void**
        if((*pointer) == ptr) {
            *pointer = REALLOC(self->allocator, ptr, size);
            return *pointer;
        }
    }
    panic("the pointer isn't in the Tracking Allocator\n");
}

void tracking_free_i(TrackingAllocator* self, void* ptr) {
    if(ptr == NULL) return;
    da_foreach(pointer, &self->pointers) {
        //pointer is a void**
        if((*pointer) == ptr) {
            FREE(self->allocator, ptr);
            *pointer = NULL;
            return;
        }
    }
    panic("the pointer isn't in the Tracking Allocator\n");
}

EXPORT TrackingAllocator tracking_create(Allocator* backing) {
    TrackingAllocator a = {0};
    a.allocator = backing;
    da_init(&a.pointers, a.allocator);
    a.alloc = (fun_alloc) tracking_alloc_i;
    a.calloc = (fun_calloc) tracking_calloc_i;
    a.free = (fun_free) tracking_free_i;
    a.realloc = (fun_realloc) tracking_realloc_i;
    return a;
}

EXPORT void tracking_destroy(TrackingAllocator* self) {
    da_foreach(ptr, &self->pointers) {
        FREE(self->allocator, *ptr); 
    }
    da_free(&self->pointers);
}

EXPORT String string_from(const char* str, Allocator* alloc) {
    size_t l = strlen(str);
    if(l == 0) {
        return (String) {0};
    }
    String string = {0};
    string.alloc = alloc;
    string.data = ALLOC(alloc,l+1);
    strcpy(string.data, str);
    string.cap = l+1;
    string.len = l;
    return string;
}
EXPORT String string_clone(String * str, Allocator* alloc) {
    return da_clone(str, alloc);
}
EXPORT char* from_string(String* str, Allocator* alloc) {
    char* s = ALLOC(alloc, str->len + 1);
    memcpy(s, str->data, str->len);
    s[str->len] = '\0';
    return s;
}
EXPORT void append_char(String* str, char ch) {
    da_append(str, ch);
}
EXPORT void append_cstr(String* str, char* s) {
    size_t len = strlen(s); 

    for(size_t i = 0; i < len; i++) {
        da_append(str, s[i]);
    }
}
EXPORT void append_string(String* str, String* val) {
    da_foreach(c, val) {
        da_append(str, *c);
    }
}

EXPORT bool string_compare(String a, String b) {
    if(a.len != b.len) return false;
    for(size_t i = 0; i < a.len; i++) {
        if(a.data[i] != b.data[i]) return false;
    }
    return true;
}

EXPORT bool str_cmp(const char* a, const char* b) {
    return strcmp(a, b) == 0;
}

EXPORT String string_create(Allocator* alloc) {
    String s = {0};
    da_init(&s, alloc);
    return s;
}

EXPORT size_t hm_hash_cstr(const char* s) {
    size_t hash = 1469598103934665603ULL;
    while (*s) {
        hash ^= (unsigned char)*s++;
        hash *= 1099511628211ULL;
    }
    return hash;
}

EXPORT size_t hm_hash_int(int s) {
    return s;
}

EXPORT size_t hm_hash_string(String s) {
    size_t hash = 1469598103934665603ULL;
    da_foreach(ch, &s) {
        hash ^= (unsigned char)*ch;
        hash *= 1099511628211ULL;
    }
    return hash;
}

void* arena_alloc_i(ArenaAllocator* self, size_t size) {
    if(self->offset+size <= self->cap) {
        void* ptr = self->data+self->offset;
        self->offset += size;
        return ptr;
    } else {
        return NULL;
    }
}

void* arena_calloc_i(ArenaAllocator* self, size_t nmemb, size_t size) {
    void* ptr = ALLOC(self, nmemb*size);
    if(ptr == NULL) return NULL;
    memset(ptr, 0, nmemb*size);
    return ptr;
}

void* arena_realloc_i(ArenaAllocator* self, void* _, size_t size) {
    return ALLOC(self, size);
}

void arena_free_i(ArenaAllocator* _, void* ptr) {
    UNUSED(ptr);
    printf("Arena Allocator doesn't support free");
}


EXPORT ArenaAllocator arena_create(Allocator* base, size_t size) {
    ArenaAllocator alloc = {0};
    alloc.allocator = base;
    alloc.cap = size;
    alloc.offset = 0;
    alloc.data = (uint8_t*)ALLOC(base, size);


    alloc.alloc = (fun_alloc) arena_alloc_i;
    alloc.calloc = (fun_calloc) arena_calloc_i;
    alloc.free = (fun_free) arena_free_i;
    alloc.realloc = (fun_realloc) arena_realloc_i;
    return alloc;
}

EXPORT void arena_free_all(ArenaAllocator* self) {
    self->offset = 0;
}

EXPORT void arena_destroy(ArenaAllocator* self) {
    FREE(self->allocator, self->data);
    self->cap = 0;
    self->offset = 0;
    self->data = NULL;
}

void* scratch_alloc_i(ScratchAllocator* self, size_t size) {
    if(self->last == NULL) {
        self->first = ALLOC(self->allocator, sizeof(Region));
        self->last = self->first;
        self->first->offset=0;
        self->first->cap = size>REGION_SIZE?size:REGION_SIZE;
        self->first->next = NULL;
        self->first->data = ALLOC(self->allocator, self->first->cap);
    }
    if(self->last->offset+size <= self->last->cap) {
        void* ptr = self->last->data+self->last->offset;
        self->last->offset += size;
        return ptr;
    } else {
        self->last->next = ALLOC(self->allocator, sizeof(Region));
        self->last->next->offset=0;
        self->last->next->cap = size>REGION_SIZE?size:REGION_SIZE;
        self->last->next->next = NULL;
        self->last->next->data = ALLOC(self->allocator, self->last->next->cap);
        self->last = self->last->next;
        return ALLOC(self, size);
    }
}

void* scratch_calloc_i(ScratchAllocator* self, size_t nmemb, size_t size) {
    void* ptr = ALLOC(self, nmemb*size);
    if(ptr == NULL) return NULL;
    memset(ptr, 0, nmemb*size);
    return ptr;
}

void* scratch_realloc_i(ScratchAllocator* self, void* _, size_t size) {
    return ALLOC(self, size);
}

void scratch_free_i(ScratchAllocator* _, void* ptr) {
    UNUSED(ptr);
    printf("Scratch Allocator doesn't support free");
}


EXPORT ScratchAllocator scratch_create(Allocator* base) {
    ScratchAllocator alloc = {0};
    alloc.allocator = base;

    alloc.alloc = (fun_alloc) scratch_alloc_i;
    alloc.calloc = (fun_calloc) scratch_calloc_i;
    alloc.free = (fun_free) scratch_free_i;
    alloc.realloc = (fun_realloc) scratch_realloc_i;
    return alloc;
}

EXPORT void scratch_destroy(ScratchAllocator* self) {
    self->last = NULL;
    Region* s = self->first;
    while(s!=NULL) {
        Region* next = s->next;
        FREE(self->allocator, s->data);
        FREE(self->allocator, s);
        s = next;
    }
    self->first = NULL;
}
#endif
