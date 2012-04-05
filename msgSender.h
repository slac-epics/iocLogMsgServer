/* msgSender.h */

#ifndef MSGSENDER__H
#define MSGSENDER__H

int msgSenderInit ();
int msgSenderExit ();
int msgSenderFlush();
int msgSenderWrite(const char   * const timeIn,
                   const char   * const alarmStatus,
                   const char   * const alarmSeverity,
                   const char   * const system,
                   const char   * const host,
                   const char   * const textIn,
                         int            textInLen);

#endif
