#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_conn_hdr = "Proxy-Connection: close\r\n";

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

void do_proxy(int fd);
void parse_uri(char *uri, char *hostname, char *path, int *port);
void make_header(char *http_header, char *hostname, char *path, rio_t *client_rio);
int connect_server(char *hostname, int port);

/*
  Pt1. Sequential
  - GET처리
  1. 커맨드라인으로 받은 포트로부터의 요청을 listen
  2. Connection이 수립되면, 요청을 읽고 parse
  3. 해당 요청을 가지고, 적절한 웹서버와 connection설립하고 클라이언트가 특정한 object를 요청(이때 지켜야하는 헤더 규약 있다)
  4. 서버의 응답을 읽고 클라이언트에게 forward
*/
int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  // size_t tid_p = 0;

  if (argc != 2)
  {
    fprintf(stderr, "Usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);

  /* Pt2. Dealing with concurrent request
    - 여러 요청을 동시에 처리할 수 있어야 함
    - 가능한 방안들
    a. 새 연결 요청 처리위해서 새 쓰레드 생성
    b. prethreaded server (12.5.5)

    - 메모리 leak을 막기 위해 쓰레드는 detached mode에서 동작해야 함: 쓰레드의 실행이 부모 프로세스와 분리
      - 스레드가 실행을 마치면, 부모가 자동으로 스레드를 종료하는 것이 아니라, 스레드가 스스로를 종료함
  */

  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // 클라이언트와의 통신 수립, connection descriptor반환

    /* 여기서의 hostname과 port는 클라이언트 그 자체! 의 hostname과 port
       클라이언트 소켓 주소 구조체를 가지고 hostname과 port를 채움
     */
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);

    printf("Accepted connection from (%s, %s)\n", hostname, port);
    do_proxy(connfd);
    Close(connfd);
  }
}

void do_proxy(int connfd)
{               // fd는 클라이언트와 수립된 descriptor
  int serverFd; // 엔드서버로의 descriptor
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], path[MAXLINE];
  int port;
  char server_header[MAXLINE]; // 서버에 전송할 헤더

  rio_t client_rio, server_rio;

  /* 2. Client로부터 request받기 */
  Rio_readinitb(&client_rio, connfd);       // rio 초기화
  Rio_readlineb(&client_rio, buf, MAXLINE); // buf에 rio의 값을 씀(클라이언트의 request)

  /* 요청 헤더에서 method, uri, version을 가져옴 */
  sscanf(buf, "%s %s %s", method, uri, version);

  /* GET 아닌 메소드에 대한 에러메시지 */
  if (strcasecmp(method, "GET"))
  { // 대소문자 구분X 스트링 비교
    clienterror(connfd, method, "501", "Not Implemented", "Proxy only supports GET method");
    return;
  }

  /* 프록시에서 서버로 보낼 정보 파싱 - uri에서 hostname, path, port를 꺼내서 채운다 */
  parse_uri(uri, hostname, path, &port);
  printf("호스트 : %s\n", hostname);
  printf("패스 : %s\n", path);
  printf("포트 : %d\n", port);

  /* 서버로 보낼 요청 헤더 생성 - hostname, path, port를 가지고 만든다 */
  make_header(server_header, hostname, path, &client_rio);

  /* 3. 웹서버와 Connection 설립 */
  serverFd = connect_server(hostname, port);

  // 서버에 요청 헤더 전송
  Rio_readinitb(&server_rio, serverFd);
  printf("server헤더 : %s\n", server_header);

  Rio_writen(serverFd, server_header, strlen(server_header));

  // 서버로부터 응답을 받아 클라이언트에 전송
  size_t n;
  while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0)
  {
    printf("proxy received %d bytes, then send them to client\n", n);
    // 서버의 응답을 클라이언트에게 forward
    Rio_writen(connfd, buf, n); // client와의 연결 소켓인 connfd에 쓴다
  }

  Close(serverFd);
}

/* uri에서 hostname, path, port 뽑아내기 */
void parse_uri(char *uri, char *hostname, char *path, int *port)
{
  *port = 80;  // 별 언급 없을시 default
  *path = '/'; // 기본 path
  char *hPos;
  char *pPos;

  /* host 부분부터 parse */
  /* http:// 가 달린 요청인경우 */
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

/*
  request     : GET /path HTTP/1.0\r\n
  host        : Host: localhost:80
  Con         : Connection:
  Prox-con    : Proxy-Connection:
  user-agent  : User-Agent:
*/
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
          conn_hdr,
          prox_conn_hdr,
          other,
          "\r\n");
}

/* 유저가 요청한 hostname, port에 적합한 서버에 접속한다 */
int connect_server(char *hostname, int port)
{
  int serverFd;
  char portc[MAXLINE];
  sprintf(portc, "%d", port); // int 인 port를 문자열로변경

  serverFd = open_clientfd(hostname, portc);

  return serverFd;
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* Http response body */
  sprintf(body, "<html><title>Tiny Error</title>"); // 출력값을 변수(문자열)에 저장(string printf)
  sprintf(body, "%s<body bgcolor="
                "ffffff"
                ">\r\n",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* print Http response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}