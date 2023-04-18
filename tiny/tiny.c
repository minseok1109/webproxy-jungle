/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, int is_get);
void serve_dynamic(int fd, char *filename, char *cgiargs, int is_get);
void get_filetype(char *filename, char *filetype);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr; // 어떠한 소켓 주소든 담을수 있을만큼 크다 : protocol-independent가능

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]); // 포트번호 인자로 넘김, listenfd 식별자 생성

  /* infinite server loop */
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // line:netp:tiny:accept

    /* 소켓 구조체를 대응되는 호스트와 서비스이름 스트링으로 대체: hostname, port*/
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit
    Close(connfd); // line:netp:tiny:close
  }
}

/* 한개의 HTTP 트랜잭션을 처리 */
void doit(int fd)
{
  int is_static;
  int is_get; // GET메서드와 HEAD메서드 비교
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /* 요청 라인을 읽고 분석 */
  Rio_readinitb(&rio, fd);           // rio 초기화
  Rio_readlineb(&rio, buf, MAXLINE); // buf에 rio의 값을 씀
  printf("Request headers :\n");
  printf("버프 %s", buf); // 요청헤더 출력

  // method, uri, version 저장
  sscanf(buf, "%s %s %s", method, uri, version); // scanf인데 입력대상이 표준입력이 아닌, buf

  /* GET과 HEAD가 아닌 메소드에 대한 에러메시지 */
  if (strcasecmp(method, "GET") != 0 && strcasecmp(method, "HEAD") != 0)
  { // 대소문자 구분X 스트링 비교
    clienterror(fd, method, "501", "Not Implemented", "Tiny does not implement this method");
    return;
  }
  /* 읽어들이고, 다른 요청헤더들을 무시 */
  read_requesthdrs(&rio);

  /* GET인지 HEAD인지 확인 */
  if (!strcmp(method, "GET"))
  {
    is_get = 1;
  }
  else
  {
    is_get = 0;
  }

  /* URI에 대한 CGI 인자 분석 - Parse URI */
  /* 정적 컨텐츠에 대한 요청? 동적 컨텐츠에 대한 요청? 플래그 설정 */
  is_static = parse_uri(uri, filename, cgiargs);
  if (stat(filename, &sbuf) < 0)
  {                                                                                // 파일의 정보 얻는 함수, filename을 현재 디렉토리에서 찾아서 ,filename의 상태 얻어와서 stat구조체인 sbuf에 채워넣음, 성공시 0 / 실패시 -1 반환
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file"); // 못찾음
    return;
  }

  /* 정적 컨텐츠 요청 - 정규 파일(ISREG) & 읽기 권한 검증(IRUSR- 사용자가 파일을 읽을 수 있음) */
  if (is_static)
  {
    /* 정규 파일 & 읽기 권한 있으면 클라이언트에게 서비스 */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
    { // st_mode : protection
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }

    serve_static(fd, filename, sbuf.st_size, is_get);
  }
  /* 동적 컨텐츠 요청  */
  else
  {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }

    /* 실행 가능시 동적 컨텐츠 서비스 */
    serve_dynamic(fd, filename, cgiargs, is_get);
  }
}

/* 에러를 확인하고 클라이언트에게 보고
  - 적절한 응답 코드와 상태메시지, 에러를 설명하는 HTML을 포함한 HTTP 응답
 */
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

/* 요청 헤더의 다른 정보들 무시(TINY는 이들을 사용하지 않는다) */
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  /* 읽고 무시 */
  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n"))
  {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }

  return;
}

/* /경로 - 정적 컨텐츠를 위한 것
   ./cgi-bin - 동적 컨텐츠를 위한 것 */
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;
  /* URI를 파일네임 / optional CGI인자 문자열로 parse */

  /* 정적 컨텐츠일시, CGI 인자 문자열 지우고, URI를 연관된 linux 경로로 편경 */
  if (!strstr(uri, "cgi-bin"))
  { // uri 안에 "cgi-bin"이라는 문자열이 없다면 static content
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri); // filename 뒤에 uri를 이어붙임 .example/path/ 같은느낌
    if (uri[strlen(uri) - 1] == '/')
      // URI가 / 로 끝나면 기본 파일네임 추가
      strcat(filename, "home.html");
    return 1;
  }

  /* 동적 컨텐츠 요청 : CGI 인자 추출 */
  else
  {
    ptr = index(uri, '?'); // 파일이름과 , cgi인자 구분하는 ?
    if (ptr)
    {
      strcpy(cgiargs, ptr + 1);
      *ptr = '\0'; // cgi인자부분은 따로 빼줬으니 잘라줌
    }
    else
    {
      strcpy(cgiargs, "");
    }
    /* URI의 남은 부분을 연관된 리눅스 파일네임으로 변경 */
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

/* 파일 이름에서 타입 얻어냄 */
void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");

  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");

  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");

  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");

  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "audio/mp4");

  else
    strcpy(filetype, "text/plain");
}

/* TINY가 제공하는 정적 컨텐츠 : HTML, 무형식 텍스트, GIF, PNG, JPEG
  - body에 local file의 내용이 담긴 HTTP응답 보냄
 */
void serve_static(int fd, char *filename, int filesize, int is_get)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* suffix를 통해서 파일 타입 결정 */
  get_filetype(filename, filetype);

  /* 클라이언트에게 응답 헤더와 응답 라인 보냄 */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  printf("%s", buf);

  /* HEAD메서드면 여기서 멈춤 */
  if (!is_get)
  {
    return;
  }

  /* 연결 식별자 fd에게 요청받은 파일의 copy본 전송 */
  /* mmap : requested file을 가상 메모리 영역과 map - 9.8절? */
  srcfd = Open(filename, O_RDONLY, 0); // 읽기 전용, 파일 시스템의 기본 접근권한(파일의 디렉토리의 기본 퍼미션) 사용
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  //          적절한 곳     읽기만      변경내용 공유X(사본) 파일의 시작부분부터(파일전체매핑)

  /* 파일을 닫고 실제 파일을 클라이언트에게 전송 */
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);

  // 가상 메모리 영역 free
  Munmap(srcp, filesize);

  /* Mmap대신 malloc 사용 */
  // srcp = (char*)malloc(filesize); // 파일을 복사할 공간할당받고
  // Rio_readn(srcfd, srcp, filesize);   // 공간(srcp)에 파일을 복사하고
  // Close(srcfd);                       // 파일을 닫아줘야 됨!!!!!
  // Rio_writen(fd, srcp, filesize);     // 이를 fd에 쓴다

  // free(srcp); // 공간 free
}

/* TINY의 동적 컨텐츠 제공 방법 : child 프로세스 fork하고 -> child의 context에서 CGI프로그램 실행 */
void serve_dynamic(int fd, char *filename, char *cgiargs, int is_get)
{
  char buf[MAXLINE], *emptylist[] = {NULL};

  /* client에게 success를 알리는 response line 전송(Server 헤더와 함께) */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  /* HEAD메서드면 여기서 멈춤 */
  if (!is_get)
  {
    return;
  }

  /* CGI 프로그램이 나머지 응답 보내야 함 */
  /* child process fork */
  if (Fork() == 0)
  { // fork함수 실행시, 자식 프로세스에서는 0이 반환됨
    /* 요청 URI의 CGI인자를 이용해 QUERY_STRING 환경변수 초기화(덮어쓰기가능) */
    setenv("QUERY_STRING", cgiargs, 1); // CGI프로그램은 런타임에 getenv함수로 이 값을 참조가능

    /* child는 Stdout을 클라이언트와 연계된 file desciptor로 설정하고 CGI프로그램 실행 */
    Dup2(fd, STDOUT_FILENO);

    /* CGI프로그램이 stdout에 쓰는 것은 바로 client에게 전달
    : CGI가 child 컨텍스트에서 실행되기에, execve함수를 호출하기 전의 열린 파일과 환경변수에 대해 똑같이 접근가능
   */
    Execve(filename, emptylist, environ); // 기 설정된 environ전역변수
  }
  /* parent는 자식이 종료될때까지 기다림, 종료 상태정보 확인하고 자식이 사용하던 시스템 리소스 해제 */
  wait(NULL);
}
