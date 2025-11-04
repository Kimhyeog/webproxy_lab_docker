/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated from CS:APP3e (Fig. 11.29~11.33)
 */
#include "csapp.h"

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

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE,
                port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);
    Close(connfd);
  }
}

/* ------------------------
   Begin: functions
   ------------------------ */

/* clienterror - sends an HTTP response describing the error */
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=\"ffffff\">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

/* doit - handle one HTTP request/response transaction */
/**
 * @brief 하나의 HTTP 요청/응답 트랜잭션 처리
 *
 * 이 함수는 하나의 HTTP 요청을 처리하는 핵심 기능을 수행합니다:
 * 1. 요청 라인과 헤더를 읽음
 * 2. HTTP 메서드, URI, 버전을 파싱
 * 3. 지원되지 않는 메서드 여부 확인 (GET만 지원)
 * 4. 요청이 정적(static)인지 동적(dynamic, CGI)인지 판별
 * 5. 파일 존재 여부 및 접근 권한 확인
 * 6. 정적 콘텐츠를 제공하거나 CGI 프로그램 실행
 *
 * 사용 예시:
 *  - 정적 요청:
 *      GET /home.html HTTP/1.1
 *      → serve_static(fd, "./home.html", filesize)
 *  - 동적 요청:
 *      GET /cgi-bin/adder?arg1=10&arg2=20 HTTP/1.1
 *      → serve_dynamic(fd, "./cgi-bin/adder", "arg1=10&arg2=20")
 *
 * @param fd 클라이언트와 연결된 소켓 파일 디스크립터
 * @return void (응답을 클라이언트로 바로 전송)
 */
void doit(int fd)
{

  // 1. connfd에 rio 버퍼 등록
  rio_t rio;
  Rio_readinitb(&rio, fd);

  // 2. 명령어 입력 및 오류 체크
  char buf[MAXLINE];
  if (Rio_readlineb(&rio, buf, MAXLINE) <= 0) // 성공 : 0 , 실패 -1
    return;

  // 3. 요청 라인 출력 및 method, uri , version 추출 -> sscanf()
  printf("Request headers : \n");
  printf("%s", buf);

  char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  sscanf(buf, "%s %s %s", method, uri, version);

  // 4. tiny서버는 GET함수만 처리 -> strcasecmp()
  if (strcasecmp(method, "GET"))
  {
    clienterror(fd, method, "501", "Not Implemented", "Tiny 서버는 이 메서드를 지원하지 않습니다");
    return;
  }

  // 5. 요청 라인을 제외한 요청 헤더들 읽기
  read_requesthdrs(&rio);

  // 6. uri 가지고, 정적 요청 || 동적 요청 구분 -> parse_uri
  char filename[MAXLINE], cgiargs[MAXLINE];
  int is_static = parse_uri(uri, filename, cgiargs);

  // 7. 파일이 유효한지 , struct stat sbuf 등록 -> stat()
  struct stat sbuf;
  if (stat(filename, &sbuf) < 0)
  {
    clienterror(fd, filename, "404", "Not Found", "Tiny 서버가 해당 파일을 찾을 수 없습니다");
    return;
  }
  // 8. 정적 파일 일 시,
  if (is_static)
  {
    // 읽을 수 없는 권한이라면,-> 에러
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden",
                  "Tiny 서버가 파일을 읽을 수 없습니다");
      return;
    }

    serve_static(fd, filename, sbuf.st_size);
  }
  // 9. 동적 파일일 경우
  else
  {
    // 읽을 수 없는 권한이라면,-> 에러
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden",
                  "Tiny 서버가 파일을 읽을 수 없습니다");
      return;
    }

    serve_dynamic(fd, filename, cgiargs);
  }
}

/* read_requesthdrs - read and ignore request headers */
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n"))
    Rio_readlineb(rp, buf, MAXLINE);
  return;
}

/* parse_uri - parse URI into filename and CGI args
 * return 1 if static content, 0 if dynamic
 */
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  if (!strstr(uri, "cgi-bin"))
  { /* Static content */
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if (uri[strlen(uri) - 1] == '/')
      strcat(filename, "home.html");
    return 1;
  }
  else
  { /* Dynamic content */
    ptr = index(uri, '?');
    if (ptr)
    {
      strcpy(cgiargs, ptr + 1);
      *ptr = '\0';
    }
    else
      strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

/* serve_static - send static content to the client */
void serve_static(int fd, char *filename, int filesize)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* Send response headers to client */
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf + strlen(buf), "Server: Tiny Web Server\r\n");
  sprintf(buf + strlen(buf), "Connection: close\r\n");
  sprintf(buf + strlen(buf), "Content-length: %d\r\n", filesize);
  sprintf(buf + strlen(buf), "Content-type: %s\r\n\r\n", filetype);
  Rio_writen(fd, buf, strlen(buf));

  /* Send response body to client */
  srcfd = Open(filename, O_RDONLY, 0);
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  Munmap(srcp, filesize);
}

/* get_filetype - derive file type from filename */
void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else
    strcpy(filetype, "text/plain");
}

/* serve_dynamic - run a CGI program on behalf of the client */
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
  char buf[MAXLINE], *emptylist[] = {NULL};

  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (Fork() == 0)
  { /* Child process */
    setenv("QUERY_STRING", cgiargs, 1);

    Dup2(fd, STDOUT_FILENO);
    /* Redirect stdout to client */
    Execve(filename, emptylist, environ); /* Run CGI program */
  }
  Wait(NULL); /* Parent waits for child */
}

/* $end tinymain */
