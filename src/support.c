#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <sys/socket.h>

#include "support.h"

unsigned int hash(char *str) {
    //djb2
    unsigned long hash = 5381;
    int c;

    while (c = *str++) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % HASHMAP_SIZE;
}

void add_waiting_client(int fd, char *key, int timeout) {
    unsigned int idx = hash(key);
    waiting_list *b = hashmap[idx];

    //find the rigth key (collision management)
    while (b != NULL) {
        if (strcmp(b->key, key) == 0) {
            //found the right key
            break;
        }
        b = b->next;
    }
    
    if (!b) { 
        //the key does not exist yet
        b = malloc(sizeof(waiting_list));
        b->key = strdup(key);
        b->clients = NULL;
        //head insertion
        b->next = hashmap[idx];
        hashmap[idx] = b;
    }

    //here b is no longer NULL, and no longer pointing to an already existing key
    //we can insert the client in the waiting list (FIFO)
    waiting_client *client = malloc(sizeof(waiting_client));
    client->fd = fd;
    client->next = NULL;
    client->next_timeout = NULL;
    client->owner = b;
    client->timeout_at = -1;
    if (timeout != -1){
        client->timeout_at = timeout + current_time_ms();
        add_waiting_client_with_timeout(client);
    } 

    if (!b->clients) {
        b->clients = client;
    } else {
        waiting_client *curr_client = b->clients;
        while (curr_client->next != NULL) curr_client = curr_client->next;
        curr_client->next = client;
        client->next = NULL;
    }
}

waiting_client *pop_waiting_client(char *key) {
    unsigned int idx = hash(key);
    waiting_list *b = hashmap[idx];

    while (b != NULL) {
        if (strcmp(b->key, key) == 0) {
            break;
        }
        b = b->next;
    }

    if (!b || !b->clients) return NULL; //No client waiting for the key

    waiting_client *client = b->clients;
    b->clients = client->next;
    client->next = NULL;
    return client;
}

void notify_waiting_clients(list_t *list, char *key) {
    waiting_client *client;
    while ((client = pop_waiting_client(key)) != NULL) {
        if (client->timeout_at != -1) {
            remove_from_timeout_list(client);
        }

        char *val = lpop(list);

        char *resp = bulk_string(val);
        char *header = "*2\r\n";
        char *key_val = bulk_string(key);

        send(client->fd, header, strlen(header), 0);
        send(client->fd, key_val, strlen(key_val), 0);
        send(client->fd, resp, strlen(resp), 0);

        free(val);
        free(resp);
        free(key_val);
        free(client);
    }
}

void add_waiting_client_with_timeout(waiting_client *client){
    if (!timeout_list || client->timeout_at < timeout_list->timeout_at) {
        client->next_timeout = timeout_list;
        timeout_list = client;
        return;
    }

    waiting_client *curr_client = timeout_list;
    while (curr_client->next_timeout != NULL) {
        if (client->timeout_at < (curr_client->next_timeout)->timeout_at) {
            client->next_timeout = curr_client->next_timeout;
            curr_client->next_timeout = client;
            return;
        }
        curr_client = curr_client->next_timeout;
    }
    curr_client->next_timeout = client;
    client->next_timeout = NULL;
}

void process_timeouts(long long now) {
    while (timeout_list && now >= timeout_list->timeout_at) {
        waiting_client *client = timeout_list;
        timeout_list = client->next_timeout;

        remove_from_waiting_list(client);

        send_null_array(client->fd);

        free(client);
    }
}

void remove_from_waiting_list(waiting_client *client) {
    waiting_list *b = client->owner;
    
    if (!b) return;

    waiting_client **curr = &b->clients;

    while (*curr && *curr != client) {
        curr = &(*curr)->next;
    }

    if (*curr) *curr = client->next;
}

void remove_from_timeout_list(waiting_client *client) {
    waiting_client **curr = &timeout_list;

    while (*curr && *curr !=client) {
        curr = &(*curr)->next_timeout;
    }

    if (*curr) *curr = client->next_timeout;
}

void remove_client_everywhere(waiting_client *client) {
    remove_from_waiting_list(client);
    remove_from_timeout_list(client);
    free(client);
}

void to_lowercase(char *s) {
    for (int i = 0; i < strlen(s); i++){
        s[i] = tolower((unsigned char)s[i]);
    }
}

char **parse_message(char *s) {
    char *token = strtok(s, "\r\n");
    int num_of_args = atoi(token + 1);
    
    if (num_of_args <= 0) {
        return NULL;
    }
    
    char **out = malloc(sizeof(char*) * (num_of_args + 1));
    int i = 0;
    while (token != NULL) {
        if (token[0] == '$') {
            int len = atoi(token + 1);
            token = strtok(NULL, "\r\n");
            out[i] = strndup(token, len);
            i++;
        }
        token = strtok(NULL, "\r\n");
    }
    to_lowercase(out[0]);
    out[num_of_args] = NULL;
    return out;
}

char *bulk_string(char* s) {
    size_t len = strlen(s);
    char *buf = malloc(len + 50);
    snprintf(buf, len + 50 , "$%zu\r\n%s\r\n", len, s);
    return buf;
}

char *resp_integer(int value) {
    char *buf = malloc(32);
    snprintf(buf, 32, ":%d\r\n", value);
    return buf;
}

char *simple_string(char *s) {
    int len = strlen(s);
    int total = len + 4;
    
    char *buf = malloc(total);
    snprintf(buf, total, "+%s\r\n", s);
    return buf;
}

void send_empty_array(int fd) {
    const char *empty_array = "*0\r\n";
    send(fd, empty_array, strlen(empty_array), 0);
}

void send_null_array(int fd) {
    const char *null_array = "*-1\r\n";
    send(fd, null_array, strlen(null_array), 0);
}

void send_null(int fd) {
    const char *null_resp = "$-1\r\n";
    send(fd, null_resp, strlen(null_resp), 0);
}

void send_error(int fd, char *err) {
    char buf[512];
    snprintf(buf, sizeof(buf), "Error: %s\r\n", err);
    send(fd, buf, strlen(buf), 0);
}

long long current_time_ms() {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

list_t *create_list() {
    list_t *list = malloc(sizeof(list_t));
    list->size = 0;
    list->capacity = 4;
    list->items = malloc(sizeof(char *) * list->capacity);
    list->head = 0;
    list->tail = 0;
    return list;

}

void resize_list(list_t *list) {
    int new_capacity = list->capacity * 2;
    char **new_items = malloc(sizeof(char *) * new_capacity);

    for (int i = 0; i < list->size; i++) {
        new_items[i] = list->items[(list->head + i) % list->capacity];
    }

    free(list->items);
    list->items = new_items;
    list->capacity = new_capacity;
    list->head = 0;
    list->tail = list->size;

}

int get_number_of_args(char **args) {
    //The array must be NULL terminated
    int count = 0;
    while (args[count] != NULL) {
        count++;
    }
    return count;
}

void rpush_element(list_t *list, char *value) {
    if (list->size >= list->capacity) {
        resize_list(list);
    }    
    
    list->items[list->tail] = strdup(value);
    list->tail = (list->tail + 1) % list->capacity;
    list->size += 1;
}

void lpush_element(list_t *list, char *value){
    if (list->size >= list->capacity) {
        resize_list(list);
    }    

    list->head = (list->head - 1 + list->capacity) % list->capacity; 
    list->items[list->head] = strdup(value);
    list->size += 1;
}

char *lpop(list_t *list){
    
    char *val = list->items[list->head];
    if (!val) return NULL;

    list->items[list->head] = NULL;
    list->head = (list->head + 1) % list->capacity;
    list->size--;

    return strdup(val);

}

int find_key_index(const char *key) {
    //returns index of the key, or -1 if the key does not exist
    for (int i = 0; i < store_size; i++) {
        if (strcmp(store[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

kv_pair *find_key(const char *key) {
    int idx = find_key_index(key);
    return (idx != -1) ? &store[idx] : NULL;
}

kv_pair* find_or_create_list(const char *key) {
    kv_pair *entry = find_key(key);
    if (!entry) {
        store[store_size].key = strdup(key);
        store[store_size].type = TYPE_LIST;
        store[store_size].expire_at = -1;
        store[store_size].value = create_list();
        entry = &store[store_size];
        store_size++;
    }
    return entry;
}

waiting_list *hashmap[HASHMAP_SIZE];
waiting_client *timeout_list = NULL;