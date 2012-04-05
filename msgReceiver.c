/*
        msgReceiver
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "osiSock.h"
#include "errlog.h"
#include "epicsVersion.h"
#if EPICS_VERSION <= 3 && EPICS_REVISION <= 13
#include <unistd.h>
#define epicsThreadSleep(x) sleep((unsigned)x)
#include "bsdSocketResource.h"
#else
#include "epicsThread.h"
#endif

#define MSGRECEIVER_MAX_CONNS  2
#define MSGRECEIVER_STOP       "MSGRECEIVER_STOP"
#define MSG_HANDLER_PORT       4569
#define MSGSENDER_MSG_SIZE     2000
#define MSGSENDER_BUFFER_SIZE  256

static int msgReceiverRead(SOCKET ioSock, char *buffer, int len);

int main(void)
{
  struct sockaddr_in serverAddr;
  struct sockaddr_in clientAddr;
/*struct linger      tmpLinger;*/
  SOCKET             ioSock;
  SOCKET             inSock;
  int                addrSize = sizeof(struct sockaddr_in);
  int                flag = 1;
  int                status = -1;
  int                stopStatus = 0;
  char               buffer[MSGSENDER_BUFFER_SIZE];
  
  memset (&serverAddr, 0, addrSize);
  serverAddr.sin_family      = AF_INET;
  serverAddr.sin_port        = htons(MSG_HANDLER_PORT);
  serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  /*
   * Create a socket, bind, and listen.
   */
  if ((ioSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    errlogPrintf("msgReceiver: Can't create socket for %d because %s\n",
                 MSG_HANDLER_PORT, strerror(SOCKERRNO));
  }
  else if (setsockopt(ioSock, SOL_SOCKET, SO_REUSEADDR,
                      (char *)&flag, sizeof (flag)) < 0) {
    errlogPrintf("msgReceiver: Can't set socket to reuse address for %d because %s\n",
                MSG_HANDLER_PORT, strerror(SOCKERRNO));
  }    
  else if (bind(ioSock, (struct sockaddr *)&serverAddr, addrSize) < 0) {
    errlogPrintf("msgReceiver: Can't bind to socket for %d because %s\n",
                 MSG_HANDLER_PORT, strerror(SOCKERRNO));
  }
  else if (listen(ioSock, MSGRECEIVER_MAX_CONNS) < 0) {
    errlogPrintf("msgReceiver: Can't listen on socket for %d because %s\n",
                 MSG_HANDLER_PORT, strerror(SOCKERRNO));
  }
  else {
    errlogPrintf("msgReceiver: Initialization complete\n");
    status = 0;
  }
  while (!stopStatus) {
    if ((inSock = accept(ioSock, (struct sockaddr *)&clientAddr,
                         (osiSocklen_t *)(&addrSize))) < 0) {
      errlogPrintf("msgReceiver: Can't accept on socket for %d because %s\n",
                   MSG_HANDLER_PORT, strerror(SOCKERRNO));
      status = -1;
      break;
    }
#if 0
    tmpLinger.l_onoff  = 1;
    tmpLinger.l_linger = 0;
    if (setsockopt(inSock, SOL_SOCKET, SO_LINGER,
                   (char *)&tmpLinger, sizeof (tmpLinger)) < 0) {
      errlogPrintf("msgReceiver: Can't set socket to linger for %d because %s\n",
                   MSG_HANDLER_PORT, strerror(SOCKERRNO));
      status = -1;
      break;
    }
#endif
    ipAddrToA((struct sockaddr_in *)&clientAddr, buffer, sizeof(buffer));
    errlogPrintf("msgReceiver: Accepted connection from %s\n", buffer);
    status = 0;
    while (!status) {
      status = msgReceiverRead(inSock, buffer, sizeof(buffer));
      if (!status) {
        errlogPrintf("%s\n", buffer);
        if (strstr(buffer, MSGRECEIVER_STOP)) {
          errlogPrintf("msgReceiver: Exit on request\n");
          stopStatus = -1;
          status = -1;
        }
      }
    }
    shutdown(inSock, SHUT_RDWR);
    close(inSock);
    errlogPrintf("msgReceiver: Connection closed\n");
  }
  shutdown(ioSock, SHUT_RDWR);
  close(ioSock);
  errlogPrintf("msgReceiver: Exit\n");
  epicsThreadSleep(0.5);
  return status;
}
/*
 * msgReceiverRead
 * Read a line from msgSender.
 */
static int msgReceiverRead(SOCKET ioSock, char *buffer, int len)
{
  int            notEndOfLine = 1;
  int            bufferIndex  = 0;
  static int     msgIndex     = 0;
  static int     msgLen       = 0;
  static char    msg[MSGSENDER_MSG_SIZE];

  /*
   * Read from the server's buffer.
   */
  do
  {
    /*
     * If there is anything left in the buffer from the last time we
     * read, copy it over until nothing left to copy, we hit an end-of-line
     * or null-terminator, or we've filled the buffer.  Ignore
     * any starting blanks.
     */
    while ((bufferIndex<len) && notEndOfLine && (msgIndex<msgLen)) {
      if ((msg[msgIndex] == '\n') || (msg[msgIndex] == 0)) {
        if (bufferIndex!=0) notEndOfLine = 0;
      }
      else if ((msg[msgIndex] != ' ') || (bufferIndex>0)) {
        buffer[bufferIndex] = msg[msgIndex];
        bufferIndex++;
      }
      msgIndex++;
    }
    /*
     * If we've filled up the input buffer without hitting an end-of-line,
     * let's hope the rest of the line consists of blanks and return what
     * we have.
     */    
    if (bufferIndex >= len) {
      notEndOfLine = 0;
      bufferIndex = len-1;
    }
    /*
     * Get more input from the server if necessary.  The socket will block.
     */
    else if (notEndOfLine) {
      
      msgIndex = 0;
      msgLen   = recv(ioSock, msg, sizeof(msg), 0);
      if (msgLen <= 0) {
        errlogPrintf("msgReceiverRead: recv failure - %s\n",
                   msgLen?strerror(SOCKERRNO):"No data");
        msgLen = 0;
        return -1;
      }
    }
  } while (notEndOfLine);
  
  buffer[bufferIndex] = 0;
  return 0;
}



