/* MessageLogging_TestStandHello.h */
/* Just used for stress testing iocLogMsgServer and Oracle db, not part of log server code */

#ifndef IOCLOGMSGSERVER_H
#define IOCLOGMSGSERVER_H

#include    <stddef.h>
#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#include 	<stdio.h>
#include	<limits.h>
#include	<time.h>
#include    <ctype.h>

#ifdef UNIX
#include 	<unistd.h>
#include	<signal.h>
#endif

#include	"epicsStdio.h"
#include	"epicsAssert.h"
#include 	"envDefs.h"
#include    "cadef.h"
#include	"dbDefs.h"
#include 	"fdmgr.h"
#include 	"osiSock.h"

/* ************************ */
/* new for logging to Oracle */
extern int db_connect(char server[31]);
extern int db_disconnect();
extern int db_commit();

extern int db_insert 
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

/* globals */
static unsigned short ioc_log_port;
static char ioc_log_fileCommand[256];
static char ioc_log_programName[10];
static char ioc_log_testDirectory[500];  /* oracle testing */
static long ioc_log_fileLimit;
static char ioc_log_fileName[256];
static FILE *ioc_log_plogfile;           /* pointer to log file handler */
static char ioc_log_hostname[100];

// verbose log file
static char ioc_log_verboseFileName[256];
static FILE *ioc_log_pverbosefile;

static int ioc_log_throttleSeconds;
static int ioc_log_throttleFields;
static char ioc_log_throttleSecondsPv[100];
static char ioc_log_throttleFieldsPv[100];
/* Channel Access pv type hooks */
static int ioc_log_throttleSecondsPvType;
static int ioc_log_throttleFieldsPvType;
static int ioc_log_commitCount;

/* raw data file used to write data in case Oracle insert fails */
static char ioc_log_rawDataFileName[256];
static FILE *ioc_log_prawdatafile;

#define MAX_VERBOSE_FILESIZE 5000000
//#define ioc_log_debug 1
#define IOCLS_ERROR (-1)
#define IOCLS_OK 0

#define MSG_SIZE 681
#define NAME_SIZE 32
#define USER_NAME_SIZE 41
#define FACILITY_SIZE 41
#define SEVERITY_SIZE 21
#define ASCII_TIME_SIZE 31
#define DESTINATION_SIZE 81
#define HOSTNODE_SIZE 41
#define PROCESS_SIZE 41
#define THROTTLE_MSG_SIZE 2001
#define MSG_CODE_SIZE 257

#define THROTTLE_PROGRAM     1 << 0    /* 1 */
#define THROTTLE_MSG         1 << 1    /* 2 */
#define THROTTLE_FACILITY    1 << 2    /* 4 */
#define THROTTLE_SEVERITY    1 << 3    /* 8 */
#define THROTTLE_CODE        1 << 4    /* 16 */
#define THROTTLE_HOST        1 << 5    /* 32 */
#define THROTTLE_USER        1 << 6    /* 64 */
#define THROTTLE_STATUS      1 << 7    /* 128 */
#define THROTTLE_PROCESS     1 << 8    /* 256 */

struct iocLogClient {
	int insock;
	struct ioc_log_server *pserver;
	size_t nChar;
	char recvbuf[1024]; 
/*	char recvbuf[256]; */
	char name[NAME_SIZE];
/*	char ascii_time[NAME_SIZE]; */
	char ascii_time[ASCII_TIME_SIZE];
};

struct ioc_log_server {
	char outfile[256];
	long filePos;
	FILE *poutfile;
	void *pfdctx;
	SOCKET sock;
	long max_file_size;
};


/* ORACLE TEST FUNCTIONS */
extern void parseMessagesTest(const char *directory);
extern void sendMessagesTest(int nrows);

static void acceptNewClient (void *pParam);
static void readFromClient(void *pParam);
static void getTimestamp (char *timestamp, int len);
static void getDate(char *datestr, int len);
/*static char *getTimestamp(); */
static int getConfig(void);
static int openLogFile();
static int openLogFile2(char *filename, FILE **pfile, int maxsize);
static int checkLogFile();
static int checkLogFile2(char *filename, FILE *pfile, int maxsize);
static int writeToRawDataFile(char *line);
static void envFailureNotify(const ENV_PARAM *pparam);
static void freeLogClient(struct iocLogClient *pclient);
static void parseMessages(struct iocLogClient *pclient);
static int parseTags(int nchar, char *hostIn, char *text, char *timeApp,  char *status, char *severity, char *facility, char *host, char *code, char *process, char *user, int *timeDef);
static int hasNextTag(char *text, char *found);
static void getThrottleTimestamp(char *appTimestamp, char* throttleTimestamp, int throttleSeconds);
static void getThrottleString(char *msg, char *system, char *severity, char *code, char *host, char *user, char *status, char *process, char *throttleString, int throttleStringMask);
static void caEventHandler(evargs args);
static void caConnectionHandler(struct connection_handler_args args);
static int caStartChannelAccess();
static int caStartMonitor(char *pvname, int *pvtype);
static int isNumeric (const char * s);
static void initGlobals();
static void printHelp();

#ifdef UNIX
static int setupSIGHUP(struct ioc_log_server *);
static void sighupHandler(int);
static void serviceSighupRequest(void *pParam);
static int getDirectory(void);
static int sighupPipe[2];
#endif


#endif /* IOCLOGMSGSERVER_H */
