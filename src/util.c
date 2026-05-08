#include <aids.h>
#include <error.h>
#include <stdio.h>
#include <util.h>

bool is_err(Result r) { return r.kind == Result_Err; }
bool is_ok(Result r) { return r.kind == Result_Ok; }

Result Err(char* message) {
    return (Result){
        .message = message,
        .kind = Result_Err,
    };
}

Result Ok() {
    return (Result){
        .message = NULL,
        .kind = Result_Ok,
    };
}

char* get_result_message(Result r) {
    if (is_ok(r)) {
        return "";
    }
    return r.message;
}

Result load_file(const char* path, ByteSlice* buf, Allocator* alloc) {
    buf->buf = NULL;
    FILE* file = fopen(path, "rb");
    if (!file)
        return Err("failed to open file");
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    if (size <= 0) {
        fclose(file);
        return Err("invalid file size");
    }
    buf->size = (size_t)size;
    rewind(file);
    buf->buf = ALLOC(alloc, buf->size * sizeof(uint8_t));
    if (!buf->buf) {
        fclose(file);
        return Err("out of memory");
    }
    size_t read = fread(buf->buf, 1, buf->size, file);
    if (read != buf->size) {
        FREE(alloc, buf->buf);
        fclose(file);
        return Err("failed to read file");
    }
    fclose(file);
    return Ok();
}

void free_slice(ByteSlice* buf, Allocator* alloc) {
    FREE(alloc, buf->buf);
    buf->buf = NULL;
}
