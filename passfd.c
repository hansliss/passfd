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

#define CONTROLLEN (sizeof(struct cmsghdr) + sizeof(int))

#ifndef CMSG_DATA
# define CMSG_DATA(cmsg) ((u_char *)((cmsg) + 1))
#endif

int send_fd(int sockfd, int fd, char *message)
{
  struct iovec iov[1];
  char buf[64];
  struct msghdr msg;

  static struct cmsghdr *cmptr = NULL;

  buf[0] = 1;
  buf[1] = '\0';
  iov[0].iov_base = buf;
  iov[0].iov_len = 2;

  msg.msg_iov = iov;
  msg.msg_iovlen = 1;
  msg.msg_name = (caddr_t)0;
  msg.msg_namelen = 0;

  if (fd < 0)
    {
      buf[0] = 0;
      if (message)
	{
	  fprintf(stderr, "Server: Sending message \"%s\"\n", message);
	  strncpy(&(buf[1]), message, sizeof(buf)-2);
	  buf[sizeof(buf)-1]='\0';
	  iov[0].iov_len=strlen(message)+2;
	}
      else
	buf[1]='\0';
#if HAVE_MSG_ACCRIGHTS == 1
      msg.msg_accrights = NULL;
      msg.msg_accrightslen = 0;
#else
      msg.msg_control = NULL;
      msg.msg_controllen = 0;
#endif
    }
  else
    {
#if HAVE_MSG_ACCRIGHTS == 1
      msg.msg_accrights = (caddr_t)&fd;
      msg.msg_accrightslen = sizeof(int);
#else
      if (cmptr == NULL &&
	  (cmptr = (struct cmsghdr *)malloc(CONTROLLEN)) == NULL)
	return -1;

      cmptr->cmsg_level = SOL_SOCKET;
      cmptr->cmsg_type = SCM_RIGHTS;
      cmptr->cmsg_len = CONTROLLEN;
      *(int *)CMSG_DATA(cmptr) = fd;
      msg.msg_control = (caddr_t)cmptr;
      msg.msg_controllen = CONTROLLEN;
#endif
    }

  if (sendmsg(sockfd, &msg, 0) < 0)
    {
      perror("Server: sendmsg()");
      return -1;
    }
  free(cmptr);
  return 0;
}

int recv_fd(int sockfd, int timeout, char *message, int messagelen)
{
  int fd;
  int l;
  struct timeval select_timeout;
  fd_set myfdset;

  char buf[64];
  struct iovec iov[1];
  struct msghdr msg;
  static struct cmsghdr *cmptr = NULL;

  select_timeout.tv_sec=timeout/1000;
  select_timeout.tv_usec=1000*(timeout % 1000);

  FD_ZERO(&myfdset);
  FD_SET(sockfd, &myfdset);
  if (select(sockfd+1, &myfdset, NULL, NULL, &select_timeout) > 0)
    {
      iov[0].iov_base = buf;
      iov[0].iov_len = sizeof(buf);
      msg.msg_iov = iov;
      msg.msg_iovlen = 1;
      msg.msg_name = (caddr_t)0;
      msg.msg_namelen = 0;

#if HAVE_MSG_ACCRIGHTS == 1
      msg.msg_accrights = (caddr_t)&fd;
      msg.msg_accrightslen = sizeof(int);
#else
      if (cmptr == NULL &&
	  (cmptr = (struct cmsghdr *)malloc(CONTROLLEN)) == NULL)
	return -1;

      msg.msg_control = (caddr_t)cmptr;
      msg.msg_controllen = CONTROLLEN;
#endif

      if ((l=recvmsg(sockfd, &msg, 0) < 0))
	{
	  perror("Client: recvmsg()");
	  free(cmptr);
	  return -1;
	}

#if HAVE_MSG_ACCRIGHTS == 1
      if (msg.msg_accrightslen != sizeof(int))
	{
	  strncpy(message, "recv_fd() protocol error", messagelen);
	  message[messagelen-1]='\0';
	  return -1;
	}
#endif

      if (buf[0]==0)
	{
	  fprintf(stderr, "Client: Error message received\n");
	  strncpy(message, &(buf[1]), messagelen);
	  message[messagelen-1]='\0';
	  return -1;
	}

      if (cmptr->cmsg_type != SCM_RIGHTS)
	return -1;

      fd = *(int *)CMSG_DATA(cmptr);
    }
  else
    {
      fprintf(stderr, "Client: recv_fd(): timeout.\n");
      fd=-1;
    }
  return fd;
}

