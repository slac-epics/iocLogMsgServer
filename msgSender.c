/*
**     msgSender
**
**     msgSender socket interface for sending messages to msg_handler.
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>

#include "osiSock.h"
#include "epicsThread.h"
#include "alarm.h"              /* ALARM_NSEV, ALARM_NSTATUS, NO_ALARM    */
#include "alarmString.h"        /* alarmSeverityString, alarmStatusString */
#include "errlog.h"             /* errlogFatal, errlogSevEnumString       */
#include "msgSender.h"

#define MSGSENDER_WAIT_DELAY 5.0  /* seconds */

#define MSGSENDER_TIMESTAMP_SIZE   23
#define MSGSENDER_STATSEVR_SIZE    21
#define MSGSENDER_SYSTEM_SIZE      21
#define MSGSENDER_HOST_SIZE        21
#define MSGSENDER_TEXT_SIZE        256
#define MSGSENDER_MSG_SIZE         350
#define MSGSENDER_BUFFER_SIZE      2000

#define MSG_HANDLER_PORT         4569
#define MSG_HANDLER_NODE         "spear1"

static SOCKET ioSock = -1;
static struct sockaddr_in sourceAddr;
static int connected = 0;
static int bufferLen = 0;
static char buffer[MSGSENDER_BUFFER_SIZE];

/*
** msgSenderConnect
** Attempts to connect to the msg_handler.
*/
static int msgSenderConnect() 
{
#ifdef MSGSENDER_NOBLOCK
  int                block;
#endif
  int                status;

  if (ioSock < 0) return SOCK_ENOTSOCK;
  if (connected)  return 0;
#ifdef MSGSENDER_NOBLOCK
  block = 1;
  socket_ioctl(ioSock, FIONBIO, &block);
#endif
  do {
    status = connect(ioSock, (struct sockaddr *)&sourceAddr,
                     sizeof(sourceAddr));
    if (status == 0) {
      connected = 1;
    } else {
      status = SOCKERRNO;
#ifdef MSGSENDER_NOBLOCK
      /*
      ** If blocked, wait for connection to finish
      */
      if ((status == SOCK_EWOULDBLOCK) || (status == SOCK_EINPROGRESS)) {
        struct timeval    timeout;
        fd_set            fds;
        timeout.tv_sec  = MSGSENDER_WAIT_DELAY;
        timeout.tv_usec = 0;
        FD_ZERO(&fds);
        FD_SET(ioSock, &fds);
        if (select (ioSock+1, 0, &fds, 0, &timeout) > 0) status = 0;
      }
#endif
    }
/*
**  Retry if interrupted.
*/
    if ((status != 0) && (status != SOCK_EINTR)) {
      if (status != SOCK_ECONNREFUSED) {
        fprintf(stderr, "msgSenderConnect: Error connecting - %s\n",
                strerror(status));
        close(ioSock);
        ioSock = -1;
      }
      return status;
    }
  } while (status != 0);
#ifdef MSGSENDER_NOBLOCK
  block = 0;
  socket_ioctl(ioSock, FIONBIO, &block);
#endif

  return 0;
}
/*
** msgSenderInit
** Allocates a socket and connects to the msg_handler.
*/
int msgSenderInit() 
{
  int                port;
  char              *source;
  char              *envVar;

  if (ioSock >= 0) return 0;
/*
** Get the msg_handler node and port from environment variables.
*/
  envVar = getenv("MSG_HANDLER_NODE");
  if (envVar) source = envVar;
  else        source = MSG_HANDLER_NODE;
  
  envVar = getenv("MSG_HANDLER_PORT");
  if (envVar) sscanf(envVar, "%d", &port);
  else        port = MSG_HANDLER_PORT;
/*
 * Initialize buffer the first time through.
 */
  if (!bufferLen) buffer[0] = 0;
/*
** Translate host name or IP address to a socket address.
*/
  if (aToIPAddr(source, port, &sourceAddr) < 0) {
    fprintf(stderr, "msgSenderInit: Invalid host %s\n", source);
    return -1;
  }
/*
** Ignore broken pipe when other side goes down.
*/
  sigignore(SIGPIPE);
/*
** Create a socket.
*/
  if ((ioSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    fprintf(stderr, "msgSenderInit: Socket creation error because %s\n",
            strerror(SOCKERRNO));
    return -1;
  }
/*
** Attempt to connect.
*/
  msgSenderConnect();
  if (connected)
    fprintf(stderr, "msgSenderInit: Connected to %s:%d\n", source, port);
  else
    fprintf(stderr, "msgSenderInit: Not connected to %s:%d\n", source, port);

  return 0;
}

/*
** msgSenderExit
** Disconnect and deallocate the socket.
**/
int msgSenderExit() 
{
  if (ioSock >= 0) {
    shutdown(ioSock, SHUT_RDWR);
    close(ioSock);
    ioSock = -1;
    connected = 0;
    bufferLen = 0;
    buffer[0] = 0;
    fprintf(stderr, "msgSenderExit: Disconnected from msg_handler\n");
  }
  return 0;
}

/*
** msgSenderFlush
** Send message buffer.
**/
int msgSenderFlush() 
{
  int                status  = 0;
  int                sendtry = 0;
#ifdef MSGSENDER_NOBLOCK
  int                block;
  struct timeval     timeout;
  fd_set             fds;
#endif

/*
 * Connect to msg_handler.  Return if there's nothing to send.
 */
  msgSenderConnect();
  if (bufferLen <= 0) return 0;
  while (1) {
    if ((ioSock >= 0) && connected) {
#ifdef MSGSENDER_NOBLOCK
      block = 1;
      socket_ioctl(ioSock, FIONBIO, &block);
      timeout.tv_sec  = MSGSENDER_WAIT_DELAY;
      timeout.tv_usec = 0;
      FD_ZERO(&fds);
      FD_SET(ioSock, &fds);
      status = select(ioSock+1, 0, &fds, 0, &timeout);
      if (status > 0) 
#endif
        status = send(ioSock, buffer, bufferLen, 0);
#ifdef MSGSENDER_NOBLOCK
      block = 0;
      socket_ioctl(ioSock, FIONBIO, &block);
#endif
      if (status > 0) {
        bufferLen = 0;
        buffer[0] = 0;
        return status;
      }
      if (sendtry) return 0;
      sendtry = 1;
      msgSenderExit();  /* ioSock set to -1 */
      epicsThreadSleep(MSGSENDER_WAIT_DELAY);
    }
    if (ioSock < 0) status = msgSenderInit();
    if (ioSock < 0) return -1;
    if (!connected) return 0;
  }
  return 0;
}
/*
** msgSenderWrite
** Update the buffer with a new message.
**/
int msgSenderWrite(const char   * const timeIn,
                   const char   * const msgStatus,
                   const char   * const msgSeverity,
                   const char   * const system,
                   const char   * const host,
                   const char   * const textIn,
                         int            textInLen)
{
  int                idx;
  char               text[MSGSENDER_TEXT_SIZE];
  char              *charPtr;

/*
**  Valid timestamp must be provided.
*/
  if ((!timeIn) || (strlen(timeIn) > MSGSENDER_TIMESTAMP_SIZE)) return 0;
/*
** Flush the existing buffer if there isn't enough room for the new message.
*/
  if ((bufferLen + MSGSENDER_MSG_SIZE) >= MSGSENDER_BUFFER_SIZE) {
    msgSenderFlush();
    bufferLen = 0;
    buffer[0] = 0;
  }
/*
**  Add timestamp.
*/
  bufferLen += sprintf(buffer+bufferLen, "#%s ", timeIn);
/*
**  Add status.  Convert from string to corresponding integer.
*/
  if (msgStatus) {
    for (idx = 0; (idx < ALARM_NSTATUS) &&
                  (strcmp(msgStatus, alarmStatusString[idx])); idx++);
    if (idx == ALARM_NSTATUS) idx = NO_ALARM;
  } else {
    idx = NO_ALARM;
  }
  bufferLen += sprintf(buffer+bufferLen, "%d ", idx);
/*
**  Add severity.  Convert from string to corresponding integer.
**  First check for an alarm severity, then an errlog severity.
*/
  if (msgSeverity) {
    for (idx = 0; (idx < ALARM_NSEV) &&
                  (strcmp(msgSeverity, alarmSeverityString[idx])); idx++);
    if (idx == ALARM_NSEV) {
      for (idx = 0; (idx <= errlogFatal) &&
                    (strcmp(msgSeverity, errlogSevEnumString[idx])); idx++);
      if (idx == (errlogFatal+1)) idx = NO_ALARM;
    }
  } else {
    idx = NO_ALARM;
  }
  bufferLen += sprintf(buffer+bufferLen, "%d ", idx);
/*
**  Add the system (ie, ALH, IOC, Channel, etc).  Replace the first
**  blank character, if any, with a null-terminator.
*/
  if (system) {
    strncpy(text, system, MSGSENDER_SYSTEM_SIZE);
    text[MSGSENDER_SYSTEM_SIZE-1] = 0;
    charPtr = strchr(text, ' ');
    if (charPtr) *charPtr = 0;
  } else {
    text[0] = 0;
  }
  if (strlen(text) == 0) strcpy(text, "Unknown");
  bufferLen += sprintf(buffer+bufferLen, "%s ", text);
/*
**  Add the host.  Replace the first blank character, if any, 
**  with a null-terminator.
*/
  if (host) {
    strncpy(text, host, MSGSENDER_HOST_SIZE);
    text[MSGSENDER_HOST_SIZE-1] = 0;
    charPtr = strchr(text, ' ');
    if (charPtr) *charPtr = 0;
  } else {
    text[0] = 0;
  }
  if (strlen(text) == 0) strcpy(text, "unknown");
  bufferLen += sprintf(buffer+bufferLen, "%s ", text);
/*
**  Now do the message text.  Replace any '#' in text with '*'.
*/
  if (textInLen > 0) {
    textInLen++;
    if (textInLen > MSGSENDER_TEXT_SIZE) textInLen = MSGSENDER_TEXT_SIZE;
    strncpy(text, textIn, textInLen);
    text[textInLen-1] = 0;
    for (charPtr = text; (charPtr = strchr(charPtr, '#')); charPtr++)
      *charPtr = '*';
    bufferLen += sprintf(buffer+bufferLen, "%s", text);
  }
/*
**  Signal end-of-message.
*/
  bufferLen += sprintf(buffer+bufferLen, "\n");
/*
 *  The message will be sent later when program calls msgSenderFlush.
 */
  return 0;
}


