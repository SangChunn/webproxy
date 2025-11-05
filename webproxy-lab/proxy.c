#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void doit(int connfd);
void parse_uri(char *uri, char *hostname, char *path, char *port);
void build_http_header(char *http_header, char *hostname, char *path, rio_t *client_rio);
void *thread(void *vargp);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  if (argc != 2){
    fprintf(stderr, "use: %s \n", argv[0]);
    exit(1);
  }

  Signal(SIGPIPE, SIG_IGN);
  listenfd = Open_listenfd(argv[1]);
   
  while(1){
    int *connfdp = malloc(sizeof(int));
    clientlen = sizeof(clientaddr);
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    pthread_t tid;
    pthread_create(&tid,NULL, thread, connfdp);
  }
  return 0;
}

void *thread(void *vargp){
  int connfd = *(int *)vargp;
  Pthread_detach(pthread_self());
  free(vargp);
  doit(connfd);
  Close(connfd);
  return NULL;
}

void doit(int connfd){
  char buf[MAXLINE], method[MAXLINE], version[MAXLINE], uri[MAXLINE];
  char hostname[MAXLINE], path[MAXLINE], port[MAXLINE];
  rio_t client_rio, server_rio;

  Rio_readinitb(&client_rio, connfd);
  if (!Rio_readlineb(&client_rio, buf, MAXLINE))
    return;

  sscanf(buf, "%s %s %s", method, uri, version);
  if (strcasecmp(method, "GET")) {
    return;
  }    

  parse_uri(uri, hostname, path, port);

  char http_header[MAXLINE];
  build_http_header(http_header, hostname, path, &client_rio);

  int serverfd = Open_clientfd(hostname, port);
  if (serverfd < 0){
    printf("Error - connect (%s %s)\n", hostname, port);
    return;
  }

  Rio_writen(serverfd, http_header, strlen(http_header));

  Rio_readinitb(&server_rio, serverfd);
  size_t n;
  while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0){
    Rio_writen(connfd, buf, n);
  }

  Close(serverfd);

}

void parse_uri(char *uri, char *hostname, char *path, char *port) {
    *port = '\0';
    if (!strstr(uri, "http://")) {
        strcpy(hostname, uri);
        strcpy(path, "/");
        strcpy(port, "80");
        return;
    }
    char *host_begin = uri + 7;
    char *path_begin = strchr(host_begin, '/');
    if (path_begin) {
        strcpy(path, path_begin);
        *path_begin = '\0';
    } else {
        strcpy(path, "/");
    }

    char *port_pos = strchr(host_begin, ':');
    if (port_pos) {
        *port_pos = '\0';
        strcpy(port, port_pos + 1);
    } else {
        strcpy(port, "80");
    }
    strcpy(hostname, host_begin);
}

void build_http_header(char *http_header, char *hostname, char *path, rio_t *client_rio) {
    char buf[MAXLINE], other_hdr[MAXLINE] = "";
    char host_hdr[MAXLINE] = "";
    char request_line[MAXLINE];

    sprintf(request_line, "GET %s HTTP/1.0\r\n", path);

    int host_exists = 0;
    while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {
        if (!strcmp(buf, "\r\n")) break;
        if (!strncasecmp(buf, "Host:", 5)) {
            strcpy(host_hdr, buf);
            host_exists = 1;
        } else if (strncasecmp(buf, "User-Agent", 10) &&
                   strncasecmp(buf, "Connection", 10) &&
                   strncasecmp(buf, "Proxy-Connection", 17)) {
            strcat(other_hdr, buf);
        }
    }
    if (!host_exists) {
        sprintf(host_hdr, "Host: %s\r\n", hostname);
    }

    sprintf(http_header, "%s" "%s" "%s" "Connection: close\r\n" "Proxy-Connection: close\r\n" "%s" "\r\n",
       request_line, host_hdr, user_agent_hdr ,other_hdr);
}
