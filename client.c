#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "autoconfig.h"
#include "passfd.h"

#define BUFSIZE 8192

#define CLIENT_ERROR_ADDRESS -1
#define CLIENT_ERROR_SOCKET -2
#define CLIENT_ERROR_BIND -3
#define CLIENT_ERROR_CONNECT -4
#define CLIENT_OK 0

int clientsocket(char *client_address, char *client_port, char *message, int messagelen)
{
  struct addrinfo hints={AI_PASSIVE, PF_UNSPEC, SOCK_STREAM, IPPROTO_TCP, 0, NULL, NULL, NULL};
  struct addrinfo *client_ai;
  int s, r;
  if ((r=getaddrinfo(client_address, client_port, &hints, &client_ai))!=0)
    {
      if (r==EAI_SYSTEM)
	strncpy(message, strerror(errno), messagelen);
      else
	strncpy(message, gai_strerror(r), messagelen);
      message[messagelen-1]='\0';
      return CLIENT_ERROR_ADDRESS;
    }
  if ((s=socket(client_ai->ai_family,
		client_ai->ai_socktype,
		client_ai->ai_protocol))==-1)
	{
	  snprintf(message, messagelen, "socket(): %s", strerror(errno));
	  message[messagelen-1]='\0';
	  freeaddrinfo(client_ai);
	  return CLIENT_ERROR_SOCKET;
	}
  if ((bind(s,
	    (struct sockaddr *)(client_ai->ai_addr),
	    client_ai->ai_addrlen))==-1)
    {
      snprintf(message, messagelen, "bind(): %s", strerror(errno));
      message[messagelen-1]='\0';
      close(s);
      freeaddrinfo(client_ai);
      return CLIENT_ERROR_BIND;
    }
  freeaddrinfo(client_ai);
  return s;
}

int client(int csock, char *server_address, char *server_port,
	   char *data, int timeout)
{
  struct addrinfo hints={0, PF_UNSPEC, SOCK_STREAM, IPPROTO_TCP, 0, NULL, NULL, NULL};
  struct addrinfo *server_ai;
  int clientsocket, s, r;
  static char rdata[BUFSIZE];
  struct timeval select_timeout;
  fd_set myfdset;
  int l;
  static char message[128];

  if ((clientsocket=recv_fd(csock, 200, message, sizeof(message)))<0)
    {
      fprintf(stderr, "Client: No socket received: %s\n", message);
      return -1;
    }

  fprintf(stderr, "Client (uid=%d): received fd=%d\n", getuid(), clientsocket);
  fflush(stderr);

  if ((r=getaddrinfo(server_address, server_port, &hints, &server_ai))!=0)
    {
      fprintf(stderr,
	      "getaddrinfo(): %s / %s\n",
	      gai_strerror(r),
	      strerror(errno));
      return CLIENT_ERROR_ADDRESS;
    }
  if (connect(clientsocket,
	      (struct sockaddr *)(server_ai->ai_addr),
	      server_ai->ai_addrlen)==-1)
    {
      perror("connect()");
      close(s);
      freeaddrinfo(server_ai);
      return CLIENT_ERROR_CONNECT;
    }
  freeaddrinfo(server_ai);

  if (data)
    send(clientsocket, data, strlen(data)+1, MSG_NOSIGNAL);

  select_timeout.tv_sec=timeout/1000;
  select_timeout.tv_usec=1000*(timeout % 1000);

  FD_ZERO(&myfdset);
  FD_SET(clientsocket, &myfdset);
  if (select(clientsocket+1, &myfdset, NULL, NULL, &select_timeout) > 0)
    {
      l=read(clientsocket, rdata, sizeof(rdata)-1);
      rdata[l]='\0';
    }
  else
    rdata[0]='\0';
  shutdown(clientsocket, 2);
  close(clientsocket);
  printf("%s%s%s%s%s%s%s",
	 data?"Sent \"":"",
	 data?data:"",
	 data?"\". ":"",
	 strlen(rdata)?"Received \"":"",
	 rdata,
	 strlen(rdata)?"\".":"",
	 (data || strlen(rdata))?"\n":"");
  return CLIENT_OK;
}

void usage(char *progname)
{
  fprintf(stderr, "Usage: %s [-a <client address>] -p <client port> -A <server address> -P <server port> [-d <data>] [-u <userid>]\n", progname);
}

void childhandler(int s)
{
  while (wait(NULL)>0);
  signal(SIGCHLD, childhandler);
}

int main(int argc, char *argv[])
{
  int o;
  char *client_address=NULL;
  char *client_port=NULL;
  char *server_address=NULL;
  char *server_port=NULL;
  char *data=NULL;
  int spair[2];
  int uid=0;
  int s;
  int cpid;
  static char message[128];

  while ((o=getopt(argc, argv, "a:p:A:P:d:u:"))!=EOF)
    {
      switch (o)
	{
	case 'a':
	  client_address=optarg;
	  break;
	case 'p':
	  client_port=optarg;
	  break;
	case 'A':
	  server_address=optarg;
	  break;
	case 'P':
	  server_port=optarg;
	  break;
	case 'd':
	  data=optarg;
	  break;
	case 'u':
	  uid=atoi(optarg);
	  break;
	default:
	  usage(argv[0]);
	  return -1;
	  break;
	}
    }
  if (!server_address || !server_port || (optind != argc))
    {
      usage(argv[0]);
      return -1;
    }

  signal(SIGCHLD, childhandler);
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, spair)!=0)
    {
      perror("socketpair()");
      return -2;
    }
  if (!(cpid=fork()))
    {
      close(spair[0]);
      if (uid)
	setuid(uid);

      client(spair[1], server_address,
	     server_port,
	     data, 10);
      return 0;
    }

  close(spair[1]);
  
  s=clientsocket(client_address, client_port, message, sizeof(message));

  fprintf(stderr, "Server: sent fd=%d\n", s);
  fflush(stderr);
  
  if ((send_fd(spair[0], s, message))<0)
    {
      fprintf(stderr, "No socket sent\n");
      return -1;
    }
  while (!kill(cpid, 0))
    sleep(1);
  return 0;
}
