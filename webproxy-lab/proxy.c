/* proxy.c - proxy with thread-safe LRU cache for CS:APP proxylab */

#include "csapp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 512000

static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

/* ---------- cache data structures ---------- */
typedef struct cache_obj
{
  char *uri;  /* key (malloc'd) */
  char *data; /* response bytes (malloc'd) */
  int size;   /* total bytes in data */
  struct cache_obj *prev;
  struct cache_obj *next;
} cache_obj_t;

static cache_obj_t *cache_head = NULL; /* most-recently-used */
static cache_obj_t *cache_tail = NULL; /* least-recently-used */
static int cache_total_size = 0;
static pthread_mutex_t cache_lock;

/* ---------- function prototypes ---------- */
void *thread(void *vargp);
void doit(int connfd);
int parse_uri(char *uri, char *hostname, char *pathname, int *port);
void build_http_header(char *http_header, char *hostname, char *pathname, rio_t *client_rio);
void forward_request_and_maybe_cache(int serverfd, rio_t *server_rio, int connfd, char *uri);
void cache_init(void);
int cache_get(const char *uri, char **buf_ptr, int *size_ptr); /* returns 1 if hit */
void cache_put(const char *uri, const char *buf, int size);
void cache_evict_if_needed(int needed);
void cache_move_to_head(cache_obj_t *obj);
void cache_remove(cache_obj_t *obj);
void cache_free_obj(cache_obj_t *obj);

/* ---------- main ---------- */
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
  cache_init();
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

/* ---------- thread wrapper ---------- */
void *thread(void *vargp)
{
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  doit(connfd);
  Close(connfd);
  return NULL;
}

/* ---------- doit: handle one HTTP request/response transaction ---------- */
void doit(int connfd)
{
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], pathname[MAXLINE];
  int port;

  rio_t client_rio;

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

  /* Parse URI first */
  if (parse_uri(uri, hostname, pathname, &port) < 0)
  {
    printf("parse_uri failed for uri=%s\n", uri);
    return;
  }
  printf("Parsed: host=%s path=%s port=%d\n", hostname, pathname, port);

  /* Cache key: host + path */
  char cache_key[MAXLINE];
  sprintf(cache_key, "%s%s", hostname, pathname);

  /* Try cache */
  char *cached_buf = NULL;
  int cached_size = 0;
  if (cache_get(cache_key, &cached_buf, &cached_size))
  {
    Rio_writen(connfd, cached_buf, cached_size);
    Free(cached_buf);
    return;
  }

  /* Connect to origin server */
  char port_str[8];
  snprintf(port_str, sizeof(port_str), "%d", port);
  int serverfd = Open_clientfd(hostname, port_str);
  if (serverfd < 0)
  {
    printf("Open_clientfd failed to %s:%s\n", hostname, port_str);
    return;
  }

  /* Build header and send to server */
  char http_header[MAXLINE];
  build_http_header(http_header, hostname, pathname, &client_rio);

  rio_t server_rio;
  Rio_readinitb(&server_rio, serverfd);
  Rio_writen(serverfd, http_header, strlen(http_header));

  /* Forward response and maybe cache */
  forward_request_and_maybe_cache(serverfd, &server_rio, connfd, cache_key);

  Close(serverfd);
}

/* ---------- URI parsing (http://host[:port]/path) ---------- */
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

/* ---------- build request header to origin ---------- */
void build_http_header(char *http_header, char *hostname, char *pathname, rio_t *client_rio)
{
  char buf[MAXLINE], request_hdr[MAXLINE], host_hdr[MAXLINE], other_hdr[MAXLINE];
  char line[MAXLINE];

  /* Request line */
  sprintf(request_hdr, "GET %s HTTP/1.0\r\n", pathname);

  /* Host header */
  sprintf(host_hdr, "Host: %s\r\n", hostname);

  /* Read client headers and keep those we want (but not Connection/Proxy-Connection/User-Agent) */
  other_hdr[0] = '\0';
  Rio_readlineb(client_rio, line, MAXLINE); /* we've already read request line; now read headers */
  while (strcmp(line, "\r\n"))
  {
    if (!strncasecmp(line, "Host:", 5))
    {
      /* ignore, we'll add our own Host header */
    }
    else if (!strncasecmp(line, "Connection:", 11))
    {
      /* ignore */
    }
    else if (!strncasecmp(line, "Proxy-Connection:", 17))
    {
      /* ignore */
    }
    else if (!strncasecmp(line, "User-Agent:", 11))
    {
      /* ignore: use our own */
    }
    else
    {
      strcat(other_hdr, line);
    }
    Rio_readlineb(client_rio, line, MAXLINE);
  }

  /* Other headers: connection and user-agent fixed */
  sprintf(other_hdr + strlen(other_hdr),
          "Connection: close\r\n"
          "Proxy-Connection: close\r\n"
          "%s"
          "\r\n",
          user_agent_hdr);

  /* Combine */
  sprintf(http_header, "%s%s%s", request_hdr, host_hdr, other_hdr);
}

/* ---------- forward response and maybe cache ---------- */
void forward_request_and_maybe_cache(int serverfd, rio_t *server_rio, int connfd, char *uri)
{
  char buf[MAXLINE];
  char hdr[MAXLINE * 4];
  int hdr_len = 0;
  ssize_t n;

  /* 1) Read response headers from server, store into hdr buffer */
  hdr_len = 0;
  /* Read status line */
  if ((n = Rio_readlineb(server_rio, buf, MAXLINE)) <= 0)
    return;
  memcpy(hdr + hdr_len, buf, n);
  hdr_len += n;

  /* Read header lines until CRLF */
  int content_length = -1;
  while ((n = Rio_readlineb(server_rio, buf, MAXLINE)) > 0)
  {
    memcpy(hdr + hdr_len, buf, n);
    hdr_len += n;
    /* parse Content-length */
    if (!strncasecmp(buf, "Content-length:", 15))
    {
      content_length = atoi(buf + 15);
    }
    if (strcmp(buf, "\r\n") == 0)
      break;
  }

  /* If no Content-length, we will read until EOF (but caching only if total <= MAX_OBJECT_SIZE) */
  /* send headers to client first */
  Rio_writen(connfd, hdr, hdr_len);

  /* 2) Read body */
  /* If content_length >= 0, read that many bytes; else read until EOF */
  char *body = NULL;
  int body_len = 0;
  if (content_length >= 0)
  {
    if (content_length > 0)
    {
      body = Malloc(content_length);
      int remain = content_length;
      char *p = body;
      while (remain > 0)
      {
        n = Rio_readnb(server_rio, p, remain);
        if (n <= 0)
          break;
        p += n;
        remain -= n;
      }
      body_len = content_length - remain;
      /* forward body to client */
      if (body_len > 0)
        Rio_writen(connfd, body, body_len);
    }
  }
  else
  {
    /* read until EOF */
    /* accumulate into a dynamic buffer in chunks */
    int cap = 8192;
    body = Malloc(cap);
    body_len = 0;
    while ((n = Rio_readnb(server_rio, buf, MAXLINE)) > 0)
    {
      if (body_len + n > cap)
      {
        while (body_len + n > cap)
          cap *= 2;
        body = Realloc(body, cap);
      }
      memcpy(body + body_len, buf, n);
      body_len += n;
      Rio_writen(connfd, buf, n);
    }
  }

  /* 3) Combine hdr + body into one object and cache if small enough */
  int total_size = hdr_len + body_len;
  if (total_size <= MAX_OBJECT_SIZE)
  {
    char *objbuf = Malloc(total_size);
    memcpy(objbuf, hdr, hdr_len);
    if (body_len > 0)
      memcpy(objbuf + hdr_len, body, body_len);
    printf("[Cache Insert] URI=%s, size=%d\n", uri, total_size);
    cache_put(uri, objbuf, total_size);
    Free(objbuf);
  }

  if (body)
    Free(body);
}

/* ---------- cache implementation ---------- */
void cache_init(void)
{
  cache_head = cache_tail = NULL;
  cache_total_size = 0;
  pthread_mutex_init(&cache_lock, NULL);
}

/* return 1 and set *buf_ptr (malloc'd copy) and *size_ptr if hit; else return 0 */
int cache_get(const char *uri, char **buf_ptr, int *size_ptr)
{
  pthread_mutex_lock(&cache_lock);
  cache_obj_t *p = cache_head;
  while (p)
  {
    if (strcmp(p->uri, uri) == 0)
    {
      /* hit: move to head */
      cache_move_to_head(p);
      /* copy out */
      *size_ptr = p->size;
      *buf_ptr = Malloc(p->size);
      memcpy(*buf_ptr, p->data, p->size);
      pthread_mutex_unlock(&cache_lock);
      return 1;
    }
    p = p->next;
  }
  pthread_mutex_unlock(&cache_lock);
  return 0;
}

/* insert object into cache (evict as needed). copies uri and buf */
void cache_put(const char *uri, const char *buf, int size)
{
  if (size > MAX_OBJECT_SIZE)
    return; /* don't cache oversize objects */

  pthread_mutex_lock(&cache_lock);

  /* If already present, remove it first (we'll re-insert as MRU) */
  cache_obj_t *p = cache_head;
  while (p)
  {
    if (strcmp(p->uri, uri) == 0)
    {
      cache_remove(p);
      cache_free_obj(p);
      break;
    }
    p = p->next;
  }

  /* evict as needed */
  cache_evict_if_needed(size);

  /* create new object */
  cache_obj_t *obj = Malloc(sizeof(cache_obj_t));
  obj->uri = Malloc(strlen(uri) + 1);
  strcpy(obj->uri, uri);
  obj->data = Malloc(size);
  memcpy(obj->data, buf, size);
  obj->size = size;
  obj->prev = obj->next = NULL;

  /* insert at head (MRU) */
  obj->next = cache_head;
  if (cache_head)
    cache_head->prev = obj;
  cache_head = obj;
  if (!cache_tail)
    cache_tail = obj;
  cache_total_size += size;

  pthread_mutex_unlock(&cache_lock);
}

/* evict LRU until we have room for 'needed' bytes */
void cache_evict_if_needed(int needed)
{
  while (cache_total_size + needed > MAX_CACHE_SIZE && cache_tail)
  {
    cache_obj_t *victim = cache_tail;
    cache_remove(victim);
    cache_total_size -= victim->size;
    cache_free_obj(victim);
  }
}

/* move existing node to head (MRU) */
void cache_move_to_head(cache_obj_t *obj)
{
  if (obj == cache_head)
    return;
  /* unlink */
  if (obj->prev)
    obj->prev->next = obj->next;
  if (obj->next)
    obj->next->prev = obj->prev;
  if (obj == cache_tail)
    cache_tail = obj->prev;
  /* insert at head */
  obj->prev = NULL;
  obj->next = cache_head;
  if (cache_head)
    cache_head->prev = obj;
  cache_head = obj;
}

/* remove obj from list (does not free) */
void cache_remove(cache_obj_t *obj)
{
  if (!obj)
    return;
  if (obj->prev)
    obj->prev->next = obj->next;
  else
    cache_head = obj->next;
  if (obj->next)
    obj->next->prev = obj->prev;
  else
    cache_tail = obj->prev;
}

/* free node memory */
void cache_free_obj(cache_obj_t *obj)
{
  if (!obj)
    return;
  Free(obj->uri);
  Free(obj->data);
  Free(obj);
}
