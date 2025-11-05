#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit(int connfd);

int main(int argc, char **argv)
{
  int listenfd, connfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  // 1. 터미널 명령어 오류 처리
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  // 1. 입력한 Port로 listenfd 소켓 생성
  listenfd = Open_listenfd(argv[1]);
  // 2. while 로 connfd 생성 및 연결 대기
  while (1)
  {
    // 3. connfd 생성 및 client 연결
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    // TODO: implement doit(connfd)
    // doit(connfd);
    Close(connfd);
  }

  return 0;
}
