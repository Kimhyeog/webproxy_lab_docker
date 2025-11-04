/* proxy.c - simple sequential/ threaded proxy (safe parse_uri version) */

#include "csapp.h"
#include <stdio.h>

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void *thread(void *vargp);
void doit(int fd);
int parse_uri(char *uri, char *hostname, char *pathname, int *port);
void build_http_header(char *http_header, char *hostname, char *pathname, rio_t *client_rio);
void forward_request(int serverfd, rio_t *server_rio, int connfd);

int main(int argc, char **argv)
{
  int listenfd, *connfdp;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  Signal(SIGPIPE, SIG_IGN);
  listenfd = Open_listenfd(argv[1]);

  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfdp = Malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Pthread_create(&tid, NULL, thread, connfdp);
  }

  return 0;
}

/* thread wrapper */
void *thread(void *vargp)
{
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  doit(connfd);
  Close(connfd);
  return NULL;
}

/* doit: handle one HTTP request/response transaction for proxy */
void doit(int connfd)
{
  int serverfd;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], pathname[MAXLINE];
  int port;

  rio_t client_rio, server_rio;

  /* Read request line from client */
  Rio_readinitb(&client_rio, connfd);
  if (Rio_readlineb(&client_rio, buf, MAXLINE) <= 0)
    return;

  printf("Request: %s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  if (strcasecmp(method, "GET"))
  {
    printf("Proxy does not implement method %s\n", method);
    return;
  }

  /* parse URI */
  if (parse_uri(uri, hostname, pathname, &port) < 0)
  {
    printf("parse_uri failed for uri=%s\n", uri);
    return;
  }
  printf("Parsed: host=%s path=%s port=%d\n", hostname, pathname, port);

  /* connect to origin server */
  char port_str[8];
  snprintf(port_str, sizeof(port_str), "%d", port);
  serverfd = Open_clientfd(hostname, port_str);
  if (serverfd < 0)
  {
    printf("Open_clientfd failed to %s:%s\n", hostname, port_str);
    return;
  }

  /* build header and send to server */
  char http_header[MAXLINE];
  build_http_header(http_header, hostname, pathname, &client_rio);

  Rio_readinitb(&server_rio, serverfd);
  Rio_writen(serverfd, http_header, strlen(http_header));

  /* forward server response back to client */
  forward_request(serverfd, &server_rio, connfd);

  Close(serverfd);
}

/* parse_uri - parse uri into hostname, pathname and port (returns 0 on ok) */
int parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
  char *hostbegin;
  char *hostend;
  char *pathbegin;
  int default_port = 80;

  if (strncasecmp(uri, "http://", 7) != 0)
  {
    return -1;
  }

  hostbegin = uri + 7; /* skip "http://" */

  /* find path begin */
  pathbegin = strchr(hostbegin, '/');
  if (pathbegin)
  {
    strcpy(pathname, pathbegin);
    *pathbegin = '\0'; /* terminate hostname:port portion */
  }
  else
  {
    strcpy(pathname, "/");
  }

  /* check for port */
  hostend = strchr(hostbegin, ':');
  if (hostend)
  {
    *hostend = '\0';
    strcpy(hostname, hostbegin);
    *port = atoi(hostend + 1);
  }
  else
  {
    strcpy(hostname, hostbegin);
    *port = default_port;
  }

  return 0;
}

/* build_http_header - create request to send to origin server */
void build_http_header(char *http_header, char *hostname, char *pathname, rio_t *client_rio)
{
  char buf[MAXLINE], request_hdr[MAXLINE], host_hdr[MAXLINE], other_hdr[MAXLINE];

  /* Request line */
  sprintf(request_hdr, "GET %s HTTP/1.0\r\n", pathname);

  /* Host header */
  sprintf(host_hdr, "Host: %s\r\n", hostname);

  /* Other headers: keep it simple */
  sprintf(other_hdr,
          "Connection: close\r\n"
          "Proxy-Connection: close\r\n"
          "%s"
          "\r\n",
          user_agent_hdr);

  /* Combine */
  sprintf(http_header, "%s%s%s", request_hdr, host_hdr, other_hdr);
}

/* forward_request - read from server_rio and write to client connfd */
void forward_request(int serverfd, rio_t *server_rio, int connfd)
{
  char buf[MAXLINE];
  ssize_t n;

  /* Read and forward until EOF */
  while ((n = Rio_readlineb(server_rio, buf, MAXLINE)) > 0)
  {
    Rio_writen(connfd, buf, n);
  }
}
