#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>

#include "autoconfig.h"

#define BUFSIZE 8192

#define SERVER_ERROR_ADDRESS -1
#define SERVER_ERROR_SOCKET -2
#define SERVER_ERROR_SOCKOPT -3
#define SERVER_ERROR_BIND -4
#define SERVER_ERROR_LISTEN -5
#define SERVER_ERROR_ACCEPT -6
#define SERVER_ERROR_GETPEERNAME -7
#define SERVER_DONE 0
#define SERVER_RETRY 1

void usage(char *progname)
{
  fprintf(stderr, "Usage: %s -p <listen port/service> [-a <listen address>]\n", progname);
}

int server(char *listen_address, char *listen_port, unsigned long timeout)
{
  struct addrinfo hints={0, PF_UNSPEC, SOCK_STREAM, IPPROTO_TCP, 0, NULL, NULL, NULL};
  struct addrinfo *my_address;
  static int s=-1;
  int cs, l;
  int sockopt;
  struct sockaddr_storage local;
  struct sockaddr_storage peer;
  int sockaddr_len;
  struct timeval select_timeout;
  fd_set myfdset;
  int r;

  static char local_ip[BUFSIZE], local_port[BUFSIZE], peer_ip[BUFSIZE], peer_port[BUFSIZE], data[BUFSIZE];

  if (s==-1)
    {
      if ((r=getaddrinfo(listen_address, listen_port, &hints, &my_address))!=0)
	{
	  fprintf(stderr, "getaddrinfo(): %s / %s\n", gai_strerror(r), strerror(errno));
	  return SERVER_ERROR_ADDRESS;
	}
      if ((s=socket(my_address->ai_family, my_address->ai_socktype, my_address->ai_protocol))==-1)
	{
	  perror("socket()");
	  freeaddrinfo(my_address);
	  return SERVER_ERROR_SOCKET;
	}
      sockopt=1;
      if (setsockopt(s, SOL_SOCKET,SO_REUSEADDR, &sockopt,sizeof(sockopt))!=0)
	{
	  perror("setsockopt()");
	  close(s);
	  s=-1;
	  freeaddrinfo(my_address);
	  return SERVER_ERROR_SOCKOPT;
	}
      if ((bind(s, (struct sockaddr *)(my_address->ai_addr), my_address->ai_addrlen))==-1)
	{
	  perror("bind()");
	  close(s);
	  s=-1;
	  freeaddrinfo(my_address);
	  return SERVER_ERROR_BIND;
	}
      freeaddrinfo(my_address);
      if ((listen(s, 5))==-1)
	{
	  perror("listen()");
	  close(s);
	  s=-1;
	  return SERVER_ERROR_LISTEN;
	}

    }
  select_timeout.tv_sec=timeout/1000;
  select_timeout.tv_usec=1000*(timeout % 1000);

  FD_ZERO(&myfdset);
  FD_SET(s, &myfdset);
  if (select(s+1, &myfdset, NULL, NULL, &select_timeout) == -1)
    {
      return SERVER_RETRY;
    }
  sockaddr_len=sizeof(peer);
  if ((cs=accept(s,
		 (struct sockaddr *)&peer,
		 (socklen_t *)(&sockaddr_len)))==-1)
    {
      perror("accept()");
      close(s);
      s=-1;
      return SERVER_ERROR_ACCEPT;
    }
  sockaddr_len=sizeof(local);
  if ((getsockname(cs,
		   (struct sockaddr *)&local,
		   (socklen_t *)(&sockaddr_len)))!=0)
    {
      close(s);
      s=-1;
      perror("getsockname()");
      return SERVER_ERROR_GETPEERNAME;
    }
  sockaddr_len=sizeof(local);
  getnameinfo((struct sockaddr *)&local, sockaddr_len, local_ip, sizeof(local_ip), local_port, sizeof(local_port), NI_NUMERICHOST | NI_NUMERICSERV);
  sockaddr_len=sizeof(peer);
  getnameinfo((struct sockaddr *)&peer, sockaddr_len, peer_ip, sizeof(peer_ip), peer_port, sizeof(peer_port), NI_NUMERICHOST | NI_NUMERICSERV);
  select_timeout.tv_sec=timeout/1000;
  select_timeout.tv_usec=1000*(timeout % 1000);

  FD_ZERO(&myfdset);
  FD_SET(cs, &myfdset);
  if (select(cs+1, &myfdset, NULL, NULL, &select_timeout) > 0)
    {
      l=read(cs, data, sizeof(data)-1);
      data[l]='\0';
    }
  else
    data[0]='\0';
  printf("Connection from %s port %s to %s port %s%s%s%s.\n",
	 peer_ip,
	 peer_port,
	 local_ip,
	 local_port,
	 strlen(data)?", received \"":"",
	 data,
	 strlen(data)?"\"":"");
  if (strlen(data))
    send(cs, data, strlen(data)+1, MSG_NOSIGNAL);
  shutdown(cs, 2);
  close(cs);
  return 0;
}

int main(int argc, char *argv[])
{
  int o, r;
  char *listen_address="0.0.0.0";
  char *listen_port=NULL;
  while ((o=getopt(argc, argv, "a:p:"))!=EOF)
    {
      switch(o)
	{
	case 'a':
	  listen_address=optarg;
	  break;
	case 'p':
	  listen_port=optarg;
	  break;
	default:
	  usage(argv[0]);
	  return -1;
	  break;
	}
    }
  if (!listen_port || (optind != argc))
    {
      usage(argv[0]);
      return -1;
    }
  while ((r=server(listen_address, listen_port, 10)) >= 0);
  return r;
}
