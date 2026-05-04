#ifndef TYPES_H
#define TYPES_H

#include <aids.h>
#include <stdint.h>

typedef enum ResultKind ResultKind;
enum ResultKind {
    Result_Ok,
    Result_Err,
};

typedef struct Result Result;
struct Result {
    char* message;
    ResultKind kind;
};

typedef struct ByteSlice ByteSlice;
struct ByteSlice {
    size_t size;
    uint8_t* buf;
};

Result Err(char* message);
Result Ok();

bool is_err(Result);
bool is_ok(Result);

char* get_result_message(Result);

#define RESULT "%s"
#define FMTRESULT(s) get_result_message(s)

Result load_file(const char* path, ByteSlice* buf, Allocator*);

void free_slice(ByteSlice*, Allocator*);

#ifdef NO_DEBUG
#define debug(...)                                                             \
    do {                                                                       \
    } while (0)
#else
#ifdef DEBUG_BUILD
#define debug(...)                                                             \
    do {                                                                       \
        printf(__VA_ARGS__);                                                   \
    } while (0)
#else
#define debug(...)                                                             \
    do {                                                                       \
    } while (0)
#endif
#endif

#define loop while (1)

#endif
