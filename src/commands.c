#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/socket.h>
#include "commands.h"
#include "support.h"



void handle_command(int fd, char **args) {
    for (int i = 0; i < num_commands; i++){
        if (strcasecmp(args[0], commands[i].name) == 0) {
            commands[i].func(fd, args);
            return;
        }
    }
    send_error(fd, "unknown command");
}

void delete_key(int index) {
    free(store[index].key);

    if (store[index].type == TYPE_LIST) {
        list_t *list = store[index].value;
        for (int i = 0; i < list->size; i++) {
            int idx = (list->head + i) % list->capacity;
            free(list->items[idx]);
        }
        free(list->items);
        free(list);
    } else {
        free(store[index].value);
    }

    store[index] = store[store_size - 1];
    store_size--;
}

void ping_command(int fd, char **args) {
    const char *response = "+PONG\r\n";
    send(fd, response, strlen(response), 0);
}

void echo_command(int fd, char **args) {
    if (!args[1]) {
        send_error(fd, "wrong number of arguments");
        return;
    }

    char *response = bulk_string(args[1]);
    send(fd, response, strlen(response), 0);
    free(response);
}

void set_command(int fd, char **args){
     if (!(args[1] && args[2])) {
        send_error(fd, "wrong number of arguments");
        return;
    }

    char *ok = "+OK\r\n";
    char *key = args[1];
    char *value = args[2];
    int timelife = -1;
    long long cur_time = current_time_ms();

    if (args[3] && args[4]) {
        if (strcasecmp(args[3], "px") == 0) {
            timelife = atoi(args[4]);
        } else if (strcasecmp(args[3], "ex") == 0) {
            timelife = atoi(args[4]) * 1000;
        }
    }

    kv_pair *entry = find_key(args[1]);
    if (entry != NULL) {
            free(entry->value);
            entry->value = strdup(value);
            entry->type = TYPE_STRING;
            entry->expire_at = (timelife != -1) ? cur_time + timelife : -1;
            send(fd, ok, strlen(ok), 0);
            return;
        }

    //It's a new key
    store[store_size].key = strdup(key);
    store[store_size].value = strdup(value);
    store[store_size].type = TYPE_STRING;
    store[store_size].expire_at = (timelife != -1) ? cur_time + timelife : -1;
    store_size++;
    send(fd, ok, strlen(ok), 0);
}

void get_command(int fd, char **args){
    if (!args[1]) {
        send_error(fd, "wrong number of arguments");
        return;
    }

    kv_pair *entry = find_key(args[1]);
    if (entry == NULL) {
        send_null(fd);
    } else {
        bool expired = (entry->expire_at != -1) && (entry->expire_at <= current_time_ms());
        if (expired) {
            send_null(fd);
            int idx = find_key_index(args[1]);
            delete_key(idx);
            return;
        }
    }

    //The key exists and is active
    if (entry->type != TYPE_STRING) {
        send_error(fd, "WRONGTYPE");
        return;
    }
    char *str_value = (char *) entry->value;
    char *response = bulk_string(str_value);
    send(fd, response, strlen(response), 0);
    free(response);
}

void rpush_command(int fd, char **args){
    if (!(args[1] && args[2])) {
        send_error(fd, "wrong number of arguments");
        return;
    }
    
    kv_pair *entry = find_or_create_list(args[1]);

    if (entry->type != TYPE_LIST) {
        send_error(fd, "WRONGTYPE");
        return;
    }

    list_t *list = (list_t *) entry->value;
    for (int j = 2; args[j] != NULL; j++) {
        rpush_element(list, args[j]);
    }
    char *response = resp_integer(list->size);
    send(fd, response, strlen(response), 0);
    free(response);

    notify_waiting_clients(list, args[1]);
}

void lpush_command(int fd, char **args) {
    if (!(args[1] && args[2])) {
        send_error(fd, "wrong number of arguments");
        return;
    }
    
    kv_pair *entry = find_or_create_list(args[1]);

    if (entry->type != TYPE_LIST) {
        send_error(fd, "WRONGTYPE");
        return;
    }

    list_t *list = (list_t *) entry->value;
    for (int j = 2; args[j] != NULL; j++) {
        lpush_element(list, args[j]);
    }
    char *response = resp_integer(list->size);
    send(fd, response, strlen(response), 0);
    free(response);

    notify_waiting_clients(list, args[1]);
}

void lrange_command(int fd, char **args) {
    if (!(args[1] && args[2] && args[3])) {
        send_error(fd, "wrong number of arguments");
        return;
    }

    kv_pair *entry = find_key(args[1]);
    if (!entry) {
        send_empty_array(fd);
        return;
    }
    
    if (entry->type != TYPE_LIST) {
        send_error(fd, "WRONGTYPE");
        return;
    }

    list_t *list = (list_t *) entry->value;
    int sz = list->size;

    //Out of bound clamp
    int start_idx = atoi(args[2]);
    int end_idx = atoi(args[3]);

    if (start_idx < 0) start_idx = sz + start_idx;
    if (start_idx < 0) start_idx = 0;
    if (start_idx >= sz) {
        send_empty_array(fd);
        return;
    }
    if (end_idx < 0) end_idx = sz + end_idx;
    if (end_idx >= sz) end_idx = sz - 1;
    if (start_idx > end_idx) {
        send_empty_array(fd);
        return;
    }
    int count = end_idx - start_idx + 1;
    char header[64];
    snprintf(header, sizeof(header), "*%d\r\n", count);
    send(fd, header, strlen(header), 0);
    for (int i = start_idx; i <= end_idx; i++) {
        int idx = (list->head + i) % list->capacity;

        char *response = bulk_string(list->items[idx]);
        send(fd, response, strlen(response), 0);
        free(response);
    }
}

void llen_command(int fd, char **args) {
    if (!args[1]) {
        send_error(fd, "wrong number of arguments");
        return;
    }

    kv_pair *entry = find_key(args[1]);
    if (!entry) {
        const char *zero_int = ":0\r\n";
        send(fd, zero_int, strlen(zero_int), 0);
        return;
    }

    if (entry->type != TYPE_LIST) {
        send_error(fd, "WRONGTYPE");
        return;
    }

    list_t *list = (list_t *) entry->value;
    char *response = resp_integer(list->size);
    send(fd, response, strlen(response), 0);
    free(response);
}

void lpop_command(int fd, char **args){
    if (!args[1]) {
        send_error(fd, "wrong number of arguments");
        return;
    }

    kv_pair *entry = find_key(args[1]);
    if (!entry) {
        send_null(fd);
        return;
    }

    if (entry->type != TYPE_LIST) {
        send_error(fd, "WRONGTYPE");
        return;
    }

    list_t *list = (list_t *) entry->value;
    if (list->size == 0) {
        send_null(fd);
        return;
    }

    int num_of_pop = 1;
    if (args[2]) {
        num_of_pop = atoi(args[2]);
        if (num_of_pop <= 0) {
            send_empty_array(fd);
            return;
        }
        if (num_of_pop > list->size) num_of_pop = list->size;

        char header[64];
        snprintf(header, sizeof(header), "*%d\r\n", num_of_pop);
        send(fd, header, strlen(header), 0);
    } 

    for (int i = 0; i < num_of_pop; i++) {
        char *val = lpop(list);
        char *el = bulk_string(val);

        send(fd, el, strlen(el), 0);

        free(val);
        free(el);
    }
}

void blpop_command(int fd, char **args){
    if (!(args[1] && args[2])) {
        send_error(fd, "wrong number of arguments");
        return;
    }

    kv_pair *entry = find_or_create_list(args[1]); 

    if (entry->type != TYPE_LIST) {
        send_error(fd, "WRONGTYPE");
        return;
    }

    char *header = "*2\r\n";
    char *key = bulk_string(args[1]);

    list_t *list = (list_t *) entry->value;
    if (list->size != 0) {
        char *val = lpop(list);
        char *response = bulk_string(val);

        send(fd, header, strlen(header), 0);
        send(fd, key, strlen(key), 0);
        send(fd, response, strlen(response), 0);

        free(key);
        free(val);
        free(response);

        return;
    }

    int timeout = atof(args[2]) * 1000; //ms
    if (timeout == 0) timeout = -1;
    add_waiting_client(fd, args[1], timeout);

}

void type_command(int fd, char **args) {
     if (!args[1]) {
        send_error(fd, "wrong number of arguments");
        return;
    }

    kv_pair *entry = find_key(args[1]);
    char *response;

    if (!entry) {
        response = simple_string("none");
        send(fd, response, strlen(response), 0);
        free(response);
        return;
    }

    int t = entry->type;
    
    if (t == TYPE_STRING) {
        response = simple_string("string");
    } else if (t == TYPE_LIST) {
        response = simple_string("list");
    } else if (t == TYPE_HASH) {
        response = simple_string("hash");
    } else if (t == TYPE_SET) {
        response = simple_string("set");
    } else {
        response = simple_string("error");
    }

    send(fd, response, strlen(response), 0);
    free(response);
}


command_entry commands[] = {
	{"ping", ping_command},
	{"echo", echo_command},
    {"set", set_command},
    {"get", get_command},
    {"rpush", rpush_command},
    {"lpush", lpush_command},
    {"lrange", lrange_command},
    {"llen", llen_command},
    {"lpop", lpop_command},
    {"blpop", blpop_command},
    {"type", type_command},
};
int num_commands = sizeof(commands) / sizeof(commands[0]);

kv_pair store[1024];
int store_size = 0;