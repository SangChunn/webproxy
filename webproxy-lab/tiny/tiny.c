/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"
FILE *logfile;

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  logfile = fopen("tiny.log", "a");
    if (logfile == NULL) {
        perror("fopen");
        exit(1);
    }

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit
    Close(connfd); // line:netp:tiny:close
  }
}

void doit(int fd){
  int is_static;   
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);

  fprintf(logfile, "[LOG] Request line: %s", buf);
  fflush(logfile);

  sscanf(buf, "%s %s %s", method, uri, version);
  fprintf(logfile, "[LOG] Method = %s, URI = %s, Version = %s\n", method, uri, version);  // Log

  if (strcasecmp(method, "GET")){
    fprintf(logfile, "[ERROR] Unsupported method: %s\n", method);
    fflush(logfile);
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");

    return;
  }
  read_requesthdrs(&rio);

  is_static = parse_uri(uri, filename, cgiargs);

  if (is_static)
        fprintf(logfile, "[LOG] Static content. filename = %s\n", filename);
    else
        fprintf(logfile, "[LOG] Dynamic content. filename = %s, cgiargs = %s\n", filename, cgiargs);
    fflush(logfile);

  if(stat(filename, &sbuf) < 0){
    fprintf(logfile, "[ERROR] File not found: %s\n", filename);
    fflush(logfile);
    clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
    return;
  }

  if (is_static){
    if(!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)){
      fprintf(logfile, "[ERROR] Permission denied (static): %s\n", filename);
      fflush(logfile);
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn’t read the file");
      return;
    }
    fprintf(logfile, "[LOG] Serving static file: %s (%ld bytes)\n", filename, sbuf.st_size);
    fflush(logfile);
    serve_static(fd, filename, sbuf.st_size);
  }
  else{
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){
      fprintf(logfile, "[ERROR] Permission denied (CGI): %s\n", filename);
      fflush(logfile);
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn’t run the CGI program");
      return;
    }
    fprintf(logfile, "[LOG] Executing CGI: %s (args: %s)\n", filename, cgiargs);
    fflush(logfile);
    serve_dynamic(fd, filename, cgiargs);
  }
}