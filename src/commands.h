#ifndef COMMANDS_H
#define COMMANDS_H

typedef void (*command_fn)(int fd, char **args);

typedef struct {
    char *name;
    command_fn func;
} command_entry;

extern command_entry commands[];
extern int num_commands;

typedef enum {
    TYPE_STRING,
    TYPE_LIST,
    TYPE_SET,
    TYPE_HASH
} value_type;

typedef struct {
    char **items;
    int size;
    int capacity;
    int head;
    int tail;
} list_t;

typedef struct {
    char *key;
    value_type type;
    void *value;
    long long expire_at;
} kv_pair;

extern kv_pair store[1024];
extern int store_size;

void delete_key(int index);
void handle_command(int fd, char **args);
void ping_command(int fd, char **args);
void echo_command(int fd, char **args);
void set_command(int fd, char **args);
void get_command(int fd, char **args);
void rpush_command(int fd, char **args);
void lpush_command(int fd, char **args);
void lrange_command(int fd, char **args);
void llen_command(int fd, char **args);
void lpop_command(int fd, char **args);
void blpop_command(int fd, char **args);
void type_command(int fd, char **args);

#endif