#include "csapp.h"
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define HASH_SIZE 97   

typedef struct cache_node {
    char uri[MAXLINE];
    char object[MAX_OBJECT_SIZE];
    int size;

    struct cache_node *prev;
    struct cache_node *next;
} cache_node;

typedef struct {
    cache_node *head;   
    cache_node *tail;  
    int total_size;

    cache_node *hash_table[HASH_SIZE]; 
    pthread_rwlock_t lock;
} lru_cache;

lru_cache cache;
void doit(int connfd);
void *thread(void *vargp);
void parse_uri(char *uri, char *hostname, char *path, char *port, char *cache_key);
void build_http_header(char *http_header, char *hostname, char *path, rio_t *client_rio);

void cache_init();
unsigned int hash_func(char *uri);
cache_node *cache_find(char *uri);
void cache_insert(char *uri, char *buf, int size);
void remove_node(cache_node *node);
void add_to_head(cache_node *node);
void move_to_head(cache_node *node);

int main(int argc, char **argv) {
    int listenfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    cache_init();              
    Signal(SIGPIPE, SIG_IGN);  

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        int *connfdp = malloc(sizeof(int));
        clientlen = sizeof(clientaddr);
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        pthread_t tid;
        pthread_create(&tid, NULL, thread, connfdp);
    }
}

void *thread(void *vargp) {
    int connfd = *(int *)vargp;
    Pthread_detach(pthread_self());
    free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

void doit(int connfd) {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE], port[MAXLINE];
    char cache_key[MAXLINE];
    rio_t client_rio, server_rio;

    char object_buf[MAX_OBJECT_SIZE];
    int obj_size = 0;

    Rio_readinitb(&client_rio, connfd);
    if (!Rio_readlineb(&client_rio, buf, MAXLINE))
        return;

    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) return; 

    parse_uri(uri, hostname, path, port, cache_key);

    pthread_rwlock_rdlock(&cache.lock);
    cache_node *hit = cache_find(cache_key);
    if (hit) {
        Rio_writen(connfd, hit->object, hit->size);
        pthread_rwlock_unlock(&cache.lock);
        return;
    }
    pthread_rwlock_unlock(&cache.lock);

    char http_header[MAXLINE];
    build_http_header(http_header, hostname, path, &client_rio);

    int serverfd = Open_clientfd(hostname, port);
    if (serverfd < 0) return;

    Rio_writen(serverfd, http_header, strlen(http_header));
    Rio_readinitb(&server_rio, serverfd);

    size_t n;
    while ((n = Rio_readnb(&server_rio, buf, MAXLINE)) > 0) {
        Rio_writen(connfd, buf, n);

        if (obj_size + n < MAX_OBJECT_SIZE) {
            memcpy(object_buf + obj_size, buf, n);
            obj_size += n;
        }
    }
    Close(serverfd);

    if (obj_size < MAX_OBJECT_SIZE) {
        pthread_rwlock_wrlock(&cache.lock);
        cache_insert(cache_key, object_buf, obj_size);
        pthread_rwlock_unlock(&cache.lock);
    }
}
void parse_uri(char *uri, char *hostname, char *path, char *port, char *cache_key) {
    strcpy(port, "80");
    strcpy(path, "/");

    if (strncasecmp(uri, "http://", 7) == 0) {
        uri += 7;
    }

    char *path_start = strchr(uri, '/');
    if (path_start) {
        strcpy(path, path_start);
        *path_start = '\0';
    }

    char *port_start = strchr(uri, ':');
    if (port_start) {
        *port_start = '\0';
        strcpy(port, port_start + 1);
    }

    strcpy(hostname, uri);

    sprintf(cache_key, "%s:%s%s", hostname, port, path);
}
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) "
    "Gecko/20120305 Firefox/10.0.3\r\n";

void build_http_header(char *http_header, char *hostname, char *path, rio_t *client_rio) {
    char buf[MAXLINE], request_hdr[MAXLINE];
    char host_hdr[MAXLINE] = "";
    char other_hdr[MAXLINE] = "";
    int host_present = 0;

    sprintf(request_hdr, "GET %s HTTP/1.0\r\n", path);

    while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {
        if (!strcmp(buf, "\r\n")) break;
        if (!strncasecmp(buf, "Host:", 5)) {
            strcpy(host_hdr, buf);
            host_present = 1;
        }
        else if (strncasecmp(buf, "User-Agent", 10) &&
                 strncasecmp(buf, "Connection", 10) &&
                 strncasecmp(buf, "Proxy-Connection", 17)) {
            strcat(other_hdr, buf);
        }
    }
    if (!host_present) sprintf(host_hdr, "Host: %s\r\n", hostname);

    sprintf(http_header,
        "%s"
        "%s"
        "%s"
        "Connection: close\r\n"
        "Proxy-Connection: close\r\n"
        "%s"
        "\r\n",
        request_hdr, host_hdr, user_agent_hdr, other_hdr);
}
unsigned int hash_func(char *uri) {
    unsigned int hash = 0;
    while (*uri) {
        hash = (hash * 131) + *uri++; 
    }
    return hash % HASH_SIZE;
}
void cache_init() {
    cache.head = NULL;
    cache.tail = NULL;
    cache.total_size = 0;
    pthread_rwlock_init(&cache.lock, NULL);

    for (int i = 0; i < HASH_SIZE; i++)
        cache.hash_table[i] = NULL;
}

void remove_node(cache_node *node) {
    if (!node) return;

    if (node->prev) node->prev->next = node->next;
    else cache.head = node->next;

    if (node->next) node->next->prev = node->prev;
    else cache.tail = node->prev;
}

void add_to_head(cache_node *node) {
    node->prev = NULL;
    node->next = cache.head;
    if (cache.head) cache.head->prev = node;
    cache.head = node;
    if (!cache.tail) cache.tail = node;
}

void move_to_head(cache_node *node) {
    if (cache.head == node) return; 
    remove_node(node);
    add_to_head(node);
}
cache_node *cache_find(char *uri) {
    unsigned int idx = hash_func(uri);
    cache_node *node = cache.hash_table[idx];

    while (node) {
        if (strcmp(node->uri, uri) == 0) {
            move_to_head(node);
            return node;
        }
        node = node->next; 
    }
    return NULL; 
}
void evict_tail() {
    cache_node *victim = cache.tail;
    if (!victim) return;

    cache.total_size -= victim->size;
    remove_node(victim);

    unsigned int idx = hash_func(victim->uri);
    if (cache.hash_table[idx] == victim) {
        cache.hash_table[idx] = NULL;
    }

    free(victim);
}
void cache_insert(char *uri, char *buf, int size) {
    if (size >= MAX_OBJECT_SIZE) return;

    unsigned int idx = hash_func(uri);

    cache_node *old = cache_find(uri);
    if (old) {
        memcpy(old->object, buf, size);
        old->size = size;
        move_to_head(old);
        return;
    }

    while (cache.total_size + size > MAX_CACHE_SIZE) {
        evict_tail();
    }

    cache_node *node = malloc(sizeof(cache_node));
    strcpy(node->uri, uri);
    memcpy(node->object, buf, size);
    node->size = size;
    node->prev = node->next = NULL;

    add_to_head(node);
    cache.total_size += size;
    cache.hash_table[idx] = node;
}