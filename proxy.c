#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
// static variables
static const char *connection = "Connection: close\r\n";
static const char *proxy_connection = "Proxy-Connection: close\r\n";

// functions
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);
void proxy(int connfd);
void parse_uri(char *uri, char *hostname, char *path, int *port);
void make_header(char *final_header, char *hostname, char *path, rio_t *client_rio);

int main(int argc, char **argv)
{
  /*
  1. connfd의 내용을 읽어서
  2. 알맞은 요청인지 확인 후 (Client_error)
  3. 내용을 읽은 다음에
  4. 새로운 fd를 만들어서 연결 시킨 후에 내용을 써서
  5. server에서 요청을 읽고 Response 전달
  */

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

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    proxy(connfd);
    Close(connfd);
  }
  return 0;
}

void proxy(int connfd)
{
  int server_connected_fd;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], path[MAXLINE], to_server_header[MAXLINE];
  int port;
  rio_t client_rio, server_rio;

  Rio_readinitb(&client_rio, connfd);
  Rio_readlineb(&client_rio, buf, MAXLINE);

  printf("proxy headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  // GET말고 다른 요청 보내면 return
  if (strcasecmp(method, "GET"))
  {
    clienterror(connfd, method, "501", "Not implemented", "Proxy does not implemet this method");
    return;
  }

  parse_uri(uri, hostname, path, &port);
  printf("proxy server hostname: %s\r\n", hostname);
  printf("proxy server port: %d\r\n", port);
  printf("proxy server path: %s\r\n", path);

  // Tiny 서버와 연결
  make_header(to_server_header, hostname, path, &client_rio);
  server_connected_fd = connect_server(hostname, port);

  // 넘겨줄 헤더 작성

  // printf("%s\n", to_server_header);

  // send to tiny server
  Rio_readinitb(&server_rio, server_connected_fd);
  Rio_writen(server_connected_fd, to_server_header, strlen(to_server_header));
  printf("서버 헤더: %s\n", to_server_header);
  // 서버로부터 응답을 받아 클라이언트에 전송
  size_t n;
  while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0)
  {
    printf("proxy received %d bytes, then send them to client\n", n);
    // 서버의 응답을 클라이언트에게 forward
    Rio_writen(connfd, buf, n); // client와의 연결 소켓인 connfd에 쓴다
  }
  Close(server_connected_fd);
}

// other도 추가
void make_header(char *final_header, char *hostname, char *path, rio_t *client_rio)
{
  char request_header[MAXLINE], buf[MAXLINE], host_header[MAXLINE], other[MAXLINE];

  sprintf(request_header, "GET %s HTTP/1.0\r\n", path);
  sprintf(host_header, "Host: %s\r\n", hostname); // 호스트헤더는 기 요청받은 호스트네임으로

  /* 클라이언트의 나머지 요청 받아서 저장 */
  while (Rio_readlineb(client_rio, buf, MAXLINE))
  {
    if (!(strcmp("\r\n", buf)))
    {
      break; // '\r\n' 이면 끝
    }

    /* 얘네는 정해진 형식대로 채워줄거임 */
    if (strncasecmp(buf, "User-Agent", strlen("User-Agent")) && strncasecmp(buf, "Connection", strlen("Connection")) && strncasecmp(buf, "Proxy-Connection", strlen("Proxy-Connection")))
    {
      strcat(other, buf); // 따라서 정해진 형식없는 애들만 other에 넣어줌(이어붙이기)
    }
  }

  /* 최종 요청 헤더의 모습 */
  sprintf(final_header, "%s%s%s%s%s%s%s",
          request_header,
          host_header,
          user_agent_hdr,
          connection,
          proxy_connection,
          other,
          "\r\n");
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg)
{
  char buf[MAXLINE], body[MAXLINE];

  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor="
                "ffffff"
                ">\r\n",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

void parse_uri(char *uri, char *hostname, char *path, int *port)
{
  *port = 80;  // 별 언급 없을시 default
  *path = '/'; // 기본 path
  char *hPos;
  char *pPos;

  /* host 부분부터 parse */
  /* http:// 가 달린 요청인경우 */
  // http://localhost:3000
  hPos = strstr(uri, "//");
  if (hPos != NULL)
  {
    hPos = hPos + 2; /*  // 이거 두개 건너뛰어야함 */
  }
  else
  {
    hPos = uri;
  }

  /* port와 path를 parse */
  pPos = strstr(hPos, ":");
  // 여기서 strstr의 첫번째 인자를 uri로 두면, http://~~ 와 같은 요청이 들어왔을때
  // pPos를 http바로 다음 : 위치로 잡게됨
  if (pPos != NULL)
  { // 따로 지정된 포트가 있음
    *pPos = '\0';
    sscanf(hPos, "%s", hostname);
    sscanf(pPos + 1, "%d%s", port, path); // 여기서 segfault
  }
  else // 따로 지정된 포트가 없음
  {
    pPos = strstr(hPos, "/");
    if (!pPos)
    { // 뒤의 path가 아예없음
      sscanf(hPos, "%s", hostname);
    }
    else
    { // 뒤의 path가 있음
      *pPos = '\0';
      sscanf(hPos, "%s", hostname);
      *pPos = '/'; // path부분 긁어야 하므로 다시 / 로 바꿔줌
      sscanf(pPos, "%s", path);
    }
  }
  return;
}

int connect_server(char *hostname, int port)

{
  int clientfd;
  char port_s[MAXLINE];
  sprintf(port_s, "%d", port);
  clientfd = open_clientfd(hostname, port_s);

  return clientfd;
}
