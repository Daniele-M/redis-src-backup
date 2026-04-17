#ifndef SUPPORT_H
#define SUPPORT_H

#include "commands.h"

#define HASHMAP_SIZE 1024
typedef struct waiting_list waiting_list;

typedef struct waiting_client {
    int fd;
    long long timeout_at;
    struct waiting_client *next;
    struct waiting_client *next_timeout;
    waiting_list *owner;
} waiting_client;

typedef struct waiting_list {
    char *key;
    waiting_client *clients;
    struct waiting_list *next;
} waiting_list;

extern waiting_list *hashmap[HASHMAP_SIZE];
extern waiting_client *timeout_list;

unsigned int hash(char *str);
void add_waiting_client(int fd, char *key, int timeout);
waiting_client *pop_waiting_client(char *key);
void notify_waiting_clients(list_t *list, char *key);
void add_waiting_client_with_timeout(waiting_client *client);
void process_timeouts(long long now);
void remove_from_waiting_list(waiting_client *client);
void remove_from_timeout_list(waiting_client *client);
void to_lowercase(char *s);
char *bulk_string(char* s);
char *resp_integer(int value);
char *simple_string(char *s);
void send_empty_array(int fd);
void send_null_array(int fd);
void send_null(int fd);
void send_error(int fd, char *err);
char **parse_message(char *mess);
long long current_time_ms();
list_t *create_list();
int get_number_of_args(char **args);
void rpush_element(list_t *list, char *value);
void lpush_element(list_t *list, char *value);
char *lpop(list_t *list);
int find_key_index(const char *key);
kv_pair *find_key(const char *key);
kv_pair* find_or_create_list(const char *key);

#endif