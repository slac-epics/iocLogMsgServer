/* MessageLogging_TestStandHello.h 
/* Just used for stress testing iocLogMsgServer and Oracle db, not part of log server code */

#ifndef MESSAGELOGGERTEST__H
#define MESSAGELOGGERTEST__H

#include <string>
#include <ctime>

using namespace std;

int globalCounter;

#ifdef __cplusplus
extern "C" 
#endif

int db_insert 
    (
    int  errlog_id,
	char program_name[21],
    char facility_name[41],
    char severity[21],
    char msg[681],
    char logServer_ascii_time[31],
    char app_ascii_time[31], 
    char throttle_ascii_time[31],
    int  app_timestamp_def,
	char msg_code[257],
    char hostnode[41],
    char user_name[41],
   	char msg_status[21],
    char process_name[41],
    long msg_count,
	char throttle_parameter[2001],
    int commit_flag
    );

void parseLogFile(string filename);
void parseLine(string line);
string doStringReplace(string stringin);

#ifdef __cplusplus
extern "C" 
#endif
void sendMessagesTest(int nrows);  

#ifdef __cplusplus
extern "C" 
#endif
void parseMessagesTest(const char *directory);
#endif
