/*************************************************************************\
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
*     National Laboratory.
* Copyright (c) 2002 The Regents of the University of California, as
*     Operator of Los Alamos National Laboratory.
* EPICS BASE Versions 3.13.7
* and higher are distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
\*************************************************************************/
/* iocLogServer.c */
/* base/src/util iocLogServer.c,v 1.47.2.6 2005/11/18 23:49:06 jhill Exp */

/*
 *	archive logMsg() from several IOC's to a common rotating file	
 *
 *
 * 	    Author: 	Jeffrey O. Hill 
 *      Date:       080791 
 */

/*
 *	Revised to send messages to the SPEAR RDB msg_handler	
 *
 * 	    Revision: 	Stephanie Allison 
 *      Date:       11/02/07
 */


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

/*
#ifdef __cplusplus
extern "C" {
#endif
int ca_context_create(int);
int ca_create_channel(char *, void *, void *, int, void *);
int ca_array_get( long, long, void *, void *);
int ca_pend_io( double);
int ca_flush_io();
#ifdef __cplusplus
}
#endif
*/

/*
#include	"msgSender.h"
*/

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
static long ioc_log_fileLimit;
static char ioc_log_fileName[256];
static char ioc_log_fileCommand[256];
static char ioc_log_programName[10];
static int ioc_log_throttleSeconds;
static int ioc_log_throttleFields;
static char ioc_log_testDirectory[500];  /* oracle testing */
static FILE *ioc_log_plogfile;           /* pointer to log file handler */


/* Channel Access pv type hooks */
static int ioc_log_caThrottleSecondsPvType;
static int ioc_log_caThrottleFieldsPvType;

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

#define THROTTLE_MSG         1 << 0    /* 1 */
#define THROTTLE_FACILITY    1 << 1    /* 2 */
#define THROTTLE_SEVERITY    1 << 2    /* 4 */
#define THROTTLE_CODE        1 << 3    /* 8 */
#define THROTTLE_HOST        1 << 4    /* 16 */
#define THROTTLE_USER        1 << 5    /* 32 */
#define THROTTLE_STATUS      1 << 6    /* 64 */
#define THROTTLE_PROCESS     1 << 7    /* 128 */

struct iocLogClient {
	int insock;
	struct ioc_log_server *pserver;
	size_t nChar;
	char recvbuf[1024];
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
static void getTimestamp (char *timestamp);
/*static char *getTimestamp(); */
static int getConfig(void);
static int openLogFile(struct ioc_log_server *pserver);
static void envFailureNotify(const ENV_PARAM *pparam);
static void freeLogClient(struct iocLogClient *pclient);
static void parseMessages(struct iocLogClient *pclient);
static int parseTags(int nchar, char *hostIn, char *timeIn, char *text, char *timeApp,  char *status, char *severity, 
                     char *facility, char *host, char *code, char *process, char *user, int *timeDef);
static int hasNextTag(char *text, char *found);
static void getThrottleTimestamp(char *appTimestamp, char* throttleTimestamp, int throttleSeconds);
static void getThrottleString(char *msg, char *system, char *severity, char *code, char *host, char *user, char *status, char *process, char *throttleString, int throttleStringMask);
void caEventHandler(evargs args);
void caConnectionHandler(struct connection_handler_args args);
static int caStartChannelAccess();
static int caStartMonitor(char *pvname, int *pvtype);
int isNumeric (const char * s);
static void initGlobals();
static void printHelp();

#ifdef UNIX
static int setupSIGHUP(struct ioc_log_server *);
static void sighupHandler(int);
static void serviceSighupRequest(void *pParam);
static int getDirectory(void);
static int sighupPipe[2];
#endif

/*
 *
 *	main()
 *
 */
/*int main(void) */
int main(int argc, char* argv[]) { 
	struct sockaddr_in serverAddr;	/* server's address */
	struct timeval timeout;
	int status;
	struct ioc_log_server *pserver;
	int i;
	char buff[256];
	char throttleSecondsPv[100];
	char throttleFieldsPv[100];
	osiSockIoctl_t	optval;
	char timestamp[ASCII_TIME_SIZE];

	int ntestrows=0;  /* delete me */
	ntestrows=0;

	/* initialize variables */
	initGlobals();
	strcpy(throttleSecondsPv, "SIOC:SYS0:AL00:THROTTLE_SECONDS");
	strcpy(throttleFieldsPv, "SIOC:SYS0:AL00:THROTTLE_FIELDS");

	printf("argc=%d\n", argc);

	/* parse incoming args */
	for (i=1; i<argc; i++) {
		printf("argv[%d]=%s\n", i, argv[i]);
		/* get program name */
		strcpy(buff, "program=");
		if (strncmp(argv[i], buff, strlen(buff)) == 0) {
			strcpy(ioc_log_programName, argv[i]+strlen(buff));
		}
		/* get throttle seconds pv */
		strcpy(buff, "throttleSecondsPv=");
		if (strncmp(argv[i], buff, strlen(buff)) == 0) {
			strcpy(throttleSecondsPv, argv[i]+strlen(buff));
		}
		/* get throttle fields pv */
		strcpy(buff, "throttleFieldsPv=");
		if (strncmp(argv[i], buff, strlen(buff)) == 0) {
			strcpy(throttleFieldsPv, argv[i]+strlen(buff));
		}
		/* get test directory */
		strcpy(buff, "testdir=");
		if (strncmp(argv[i], buff, strlen(buff)) == 0) {
			strcpy(ioc_log_testDirectory, "/afs/slac/g/lcls/epics/iocTop/MessageLogging_TestStand/iocBoot/siocMessageLogging_TestStand/");
/*			strcpy(ioc_log_testDirectory, argv[i]+strlen(buff)); */
			strcat(ioc_log_testDirectory, argv[i]+strlen(buff));
		}
		/* get test rows */
		strcpy(buff, "testrows=");
		if (strncmp(argv[i], buff, strlen(buff)) == 0) {
			strcpy(buff, argv[i]+strlen(buff));
			ntestrows = atoi(buff);
		}
		/* help */
		strcpy(buff, "help");
		if (strncmp(argv[i], buff, strlen(buff)) == 0) {
			printHelp();
			return IOCLS_OK;
		}

	}
	printf("ioc_log_program_name=%s\nthrottleSecondsPv=%s\nthrottleFieldsPv=%s\n", ioc_log_programName, throttleSecondsPv, throttleFieldsPv);

	/* RUN_ORACLE_STRESS_TEST */
 	/* JROCK Connect to Oracle */
	if (!db_connect("MCCODEV")) {
        fprintf(stderr, "%s\n", "iocLogMsgServer: Error connecting to Oracle\n");
        return IOCLS_ERROR;
	}
	fprintf(stderr, "%s\n", "connected!");

	/* RUN TEST */
	if (strlen(ioc_log_testDirectory) > 0) {
		sleep(2);
		parseMessagesTest(ioc_log_testDirectory);
		return IOCLS_OK;
	} else if (ntestrows > 1 ) {
		sleep(2);
printf("Sending %d messages\n", ntestrows);
		sendMessagesTest(ntestrows);		
		return IOCLS_OK;
	}
	/* END RUN_ORACLE_STRESS_TEST */

	status = getConfig();
	if(status<0){
		fprintf(stderr, "iocLogServer: EPICS environment underspecified\n");
		fprintf(stderr, "iocLogServer: failed to initialize\n");
		return IOCLS_ERROR;
	}

	pserver = (struct ioc_log_server *) 
	calloc(1, sizeof *pserver);
	if(!pserver){
		fprintf(stderr, "iocLogServer: %s\n", strerror(errno));
		return IOCLS_ERROR;
	}

	pserver->pfdctx = (void *) fdmgr_init();
	if(!pserver->pfdctx){
		fprintf(stderr, "iocLogServer: %s\n", strerror(errno));
		return IOCLS_ERROR;
	}
	/*
	 * Open the socket. Use ARPA Internet address format and stream
	 * sockets. Format described in <sys/socket.h>.
	 */
	pserver->sock = epicsSocketCreate(AF_INET, SOCK_STREAM, 0);
	if (pserver->sock==INVALID_SOCKET) {
        char sockErrBuf[64];
        epicsSocketConvertErrnoToString ( sockErrBuf, sizeof ( sockErrBuf ) );
		fprintf(stderr, "iocLogServer: sock create err: %s\n", sockErrBuf);
		return IOCLS_ERROR;
	}
	
    epicsSocketEnableAddressReuseDuringTimeWaitState ( pserver->sock );

	/* Zero the sock_addr structure */
	memset((void *)&serverAddr, 0, sizeof serverAddr);
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(ioc_log_port);

	/* get server's Internet address */
	status = bind (	pserver->sock, (struct sockaddr *)&serverAddr, sizeof (serverAddr) );
	if (status<0) {
        char sockErrBuf[64];
        epicsSocketConvertErrnoToString ( sockErrBuf, sizeof ( sockErrBuf ) );
		fprintf(stderr, "iocLogServer: bind err: %s\n", sockErrBuf );
		fprintf (stderr, "iocLogServer: a server is already installed on port %u?\n", (unsigned)ioc_log_port);
		return IOCLS_ERROR;
	}

	/* listen and accept new connections */
	status = listen(pserver->sock, 10);
	if (status<0) {
        char sockErrBuf[64];
        epicsSocketConvertErrnoToString ( sockErrBuf, sizeof ( sockErrBuf ) );
		fprintf(stderr, "iocLogServer: listen err %s\n", sockErrBuf);
		return IOCLS_ERROR;
	}

	/*
	 * Set non blocking IO
	 * to prevent dead locks
	 */
	optval = TRUE;
	status = socket_ioctl(
					pserver->sock,
					FIONBIO,
					&optval);
	if(status<0){
        char sockErrBuf[64];
        epicsSocketConvertErrnoToString ( sockErrBuf, sizeof ( sockErrBuf ) );
		fprintf(stderr, "iocLogServer: ioctl FIONBIO err %s\n", sockErrBuf);
		return IOCLS_ERROR;
	}

#	ifdef UNIX
		status = setupSIGHUP(pserver);
		if (status<0) {
			return IOCLS_ERROR;
		}
#	endif

	status = openLogFile(pserver);
	if (status < 0) {
		fprintf(stderr, "File access problems to `%s' because `%s'\n", ioc_log_fileName, strerror(errno));
		return IOCLS_ERROR;
	}
	getTimestamp(timestamp);
	fprintf(pserver->poutfile, "%s: ioc_log_programName=%s, throttleSecondsPv=%s, throttleFieldsPv=%s\n", timestamp, ioc_log_programName, throttleSecondsPv, throttleFieldsPv);

	status = fdmgr_add_callback(
			pserver->pfdctx, 
			pserver->sock, 
			fdi_read,
			acceptNewClient,
			pserver);
	if (status<0) {
		fprintf(stderr,	"iocLogServer: failed to add read callback\n");
		return IOCLS_ERROR;
	}
        
	/*
	 * Initialize connection to msg_handler.
	 */

	/* JROCK Connect to Oracle */
	if (!db_connect("MCCODEV")) {
        fprintf(stderr, "%s\n", "iocLogMsgServer: Error connecting to Oracle\n");
        return IOCLS_ERROR;
	}
	fprintf(stderr, "%s\n", "connected!");

        /* RUN WITHOUT CONNECTION TO SPEAR MSGSENDER
        if (msgSenderInit() < 0) {
		fprintf(stderr, "iocLogServer: msgSender initialization failed\n");
		return IOCLS_ERROR;
	}
        */

	/* setup chchannel access pv monitoring for logserver throttle settings */
	status = caStartChannelAccess();
	if (status == IOCLS_OK) {
		caStartMonitor(throttleSecondsPv, &ioc_log_caThrottleSecondsPvType);
		caStartMonitor(throttleFieldsPv, &ioc_log_caThrottleFieldsPvType);
	}
	printf("PV Monitoring:\nioc_log_programName=%s\nioc_log_throttleSeconds=%d\nioc_log_throttleFields=%d\n", ioc_log_programName, ioc_log_throttleSeconds, ioc_log_throttleFields);
	fprintf(ioc_log_plogfile, "PV Monitoring:	 ioc_log_programName=%s, ioc_log_throttleSeconds=%d, ioc_log_throttleFields=%d\n", ioc_log_programName, ioc_log_throttleSeconds, ioc_log_throttleFields);

	while(TRUE) {
		timeout.tv_sec = 60;            /*  1 min  */
		timeout.tv_usec = 0; 
		fdmgr_pend_event(pserver->pfdctx, &timeout); 
		if (pserver->poutfile) {
		  fflush(pserver->poutfile);
		}
	}

      /* disconnect from  Oracle */
	if (db_disconnect()) {
		fprintf(stderr, "%s\n", "iocLogMsgServer: Disconnected from Oracle\n");
	}

	/* clean up channel access */
    ca_context_destroy();

        /* RUN WITHOUT CONNECTION TO MSGSENDER
        msgSenderExit();
        */

	exit(0); /* should never reach here */
}

static void printHelp()
{
	printf("Message Logging Program\n");
	printf("Execute with command :\n");
	printf("  iocLogMsgServer program=<accelerator> throttleSecondsPv=<throttle seconds pv> throttleFieldsPv=<throttle fields pv>\n");
	printf("  iocLogMsgServer program=LCLS throttleSecondsPv=SIOC:SYS0:AL00:THROTTLE_SECONDS throttleFieldsPv=SIOC:SYS0:AL00:THROTTLE_FIELDS\n");
	printf("  Throttle fields bit mask values :\n");
	printf("    Message=1\n");
	printf("    Facility=2\n");
	printf("    Severity=4\n");
	printf("    Error=8 \n");
	printf("    Host=16\n");
	printf("    User=32\n");
	printf("    Status=64\n");
	printf("    Process=128\n\n");
}

int isNumeric (const char * s)
{
    if (s == NULL || *s == '\0' || isspace(*s))
      return 0;
    char * p;
    strtod (s, &p);
    return *p == '\0';
}

static void initGlobals()
{
	/* initialize globals */
	strcpy(ioc_log_programName, "LCLS");  /* LCLS accelerator */	
	ioc_log_throttleSeconds = 1;          /* 1 second */
	ioc_log_throttleFields = 1;           /* msg field */
	strcpy(ioc_log_testDirectory, "");
}

/* checkLogFile()
// checks file size and copies to <logfile>.log.bak if too big
*/
static int checkLogFile(struct ioc_log_server *pserver)
{
	int fileSize=0;
	char newname[256];
	int rc=IOCLS_OK;
	int rrc = 0;

	if (pserver->poutfile) {
		fflush(pserver->poutfile);
	}

	/* get file size */
	fseek(pserver->poutfile, 0, SEEK_END);
	fileSize = ftell(pserver->poutfile);

	/* check if file is too big */
	if (fileSize > pserver->max_file_size) {
		fprintf(pserver->poutfile, "Logfile too big %d, Rename %s to %s\n", fileSize, pserver->outfile, newname);
		fclose(pserver->poutfile);
		strcpy(newname, pserver->outfile);
		strcat(newname, ".bak");
		printf("Logfile too big %d, Rename %s to %s\n", fileSize, pserver->outfile, newname);
		rrc = rename(ioc_log_fileName, newname);
		if (rrc != 0) printf("ERROR renaming %s to %s, rc=%d\n", pserver->outfile, newname, rrc); /* don't return error, hopefully renaming works later */
		
		/* reset logfile file pointer */
		pserver->poutfile = fopen(ioc_log_fileName, "a+");
		if (!pserver->poutfile) {
			pserver->poutfile = stderr;
			rc = IOCLS_ERROR;
			return rc;
		}
	}
	
	/* set file pos */
    pserver->filePos = ftell(pserver->poutfile); 
printf("pserver's filePos=%ld\n", pserver->filePos);

	return rc;
}

/*
 *	openLogFile()
 *  set ioc_log_plogfile file handler
 */
static int openLogFile(struct ioc_log_server *pserver)
{
	int rc = IOCLS_OK;
	char timestamp[ASCII_TIME_SIZE];

	if (ioc_log_fileLimit==0u) {
		pserver->poutfile = stderr;
printf("ioc_log_fileLimit=0\n");
		return IOCLS_ERROR;
	}

	if (pserver->poutfile && pserver->poutfile != stderr){
		fclose (pserver->poutfile);
		pserver->poutfile = NULL;
printf("SET poutfile is NULL\n");
	}

    if (strlen(ioc_log_fileName) > 0) {
		printf("********************LOG FILENAME is valid %s\n", ioc_log_fileName);
		strcpy(pserver->outfile, ioc_log_fileName);
		pserver->max_file_size = ioc_log_fileLimit;
		
		/* try opening for reading and writing */
		pserver->poutfile = fopen(ioc_log_fileName, "a+"); 
		if (pserver->poutfile) {
			checkLogFile(pserver);  /* check logfile if logfile is too big and needs to be renamed */
		} else { /* file doesn't exit */
			pserver->poutfile = fopen(ioc_log_fileName, "w+");  /* create file */
			fclose (pserver->poutfile);
			printf("opened and closed for write, to create new file\n");
			pserver->poutfile = fopen(ioc_log_fileName, "a+");  /* open for append */
			if (!pserver->poutfile) {
				fprintf(stderr, "ERROR opening logfile %s\n", ioc_log_fileName);
				pserver->poutfile = stderr;
				return IOCLS_ERROR;
			}
		}
/*        return seekLatestLine (pserver); */	
    }
/*    return IOCLS_OK; */
	printf("Successfully opened logfile %s\n", pserver->outfile);
	getTimestamp(timestamp); 
	fprintf(pserver->poutfile, "%s: %s file opened\n", timestamp, ioc_log_fileName);
	ioc_log_plogfile = pserver->poutfile;

	return rc;
}

/*
 *	acceptNewClient()
 *
 */
static void acceptNewClient ( void *pParam )
{
	struct ioc_log_server *pserver = (struct ioc_log_server *) pParam;
	struct iocLogClient	*pclient;
	osiSocklen_t addrSize;
	struct sockaddr_in addr;
	int status;
	osiSockIoctl_t optval;
/* TESTING RECEIVE 
printf ("====>accepting new Client: %s\n", pclient->name);
*/

	pclient = ( struct iocLogClient * ) malloc ( sizeof ( *pclient ) );
	if ( ! pclient ) {
		return;
	}

	addrSize = sizeof ( addr );
	pclient->insock = epicsSocketAccept ( pserver->sock, (struct sockaddr *)&addr, &addrSize );
	if ( pclient->insock==INVALID_SOCKET || addrSize < sizeof (addr) ) {
        static unsigned acceptErrCount;
        static int lastErrno;
        int thisErrno;

		free ( pclient );
		if ( SOCKERRNO == SOCK_EWOULDBLOCK || SOCKERRNO == SOCK_EINTR ) {
            return;
		}

        thisErrno = SOCKERRNO;
        if ( acceptErrCount % 1000 || lastErrno != thisErrno ) {
            fprintf ( stderr, "Accept Error %d\n", SOCKERRNO );
        }
        acceptErrCount++;
        lastErrno = thisErrno;

		return;
	}

	/*
	 * Set non blocking IO
	 * to prevent dead locks
	 */
	optval = TRUE;
	status = socket_ioctl(
					pclient->insock,
					FIONBIO,
					&optval);
	if(status<0){
        char sockErrBuf[64];
        epicsSocketConvertErrnoToString ( sockErrBuf, sizeof ( sockErrBuf ) );
		fprintf(stderr, "%s:%d ioctl FBIO client er %s\n", 
			__FILE__, __LINE__, sockErrBuf);
		epicsSocketDestroy ( pclient->insock );
		free(pclient);
		return;
	}

	pclient->pserver = pserver;
	pclient->nChar = 0u;

	ipAddrToA (&addr, pclient->name, sizeof(pclient->name));

/*	logTime(pclient); */
	getTimestamp(pclient->ascii_time); 
	
#if 0
	status = fprintf(
		pclient->pserver->poutfile,
		"%s %s ----- Client Connect -----\n",
		pclient->name,
		pclient->ascii_time);
	if(status<0){
		handleLogFileError();
	}
#endif

	/*
	 * turn on KEEPALIVE so if the client crashes
	 * this task will find out and exit
	 */
	{
		long true = 1;

		status = setsockopt(
				pclient->insock,
				SOL_SOCKET,
				SO_KEEPALIVE,
				(char *)&true,
				sizeof(true) );
		if(status<0){
			fprintf(stderr, "Keepalive option set failed\n");
		}
	}

	status = shutdown(pclient->insock, SHUT_WR);
	if(status<0){
        char sockErrBuf[64];
        epicsSocketConvertErrnoToString ( sockErrBuf, sizeof ( sockErrBuf ) );
		fprintf (stderr, "%s:%d shutdown err %s\n", __FILE__, __LINE__,
				sockErrBuf);
        epicsSocketDestroy ( pclient->insock );
		free(pclient);

		return;
	}

	status = fdmgr_add_callback(
			pserver->pfdctx, 
			pclient->insock, 
			fdi_read,
			readFromClient,
			pclient);
	if (status<0) {
		epicsSocketDestroy ( pclient->insock );
		free(pclient);
		fprintf(stderr, "%s:%d client fdmgr_add_callback() failed\n", 
			__FILE__, __LINE__);
		return;
	}
}


/*
 * readFromClient()
 * 
 */
#define NITEMS 1

static void readFromClient(void *pParam)
{
	struct iocLogClient	*pclient = (struct iocLogClient *)pParam;
	int             	recvLength;
	int			size;

/* TESTING RECEIVE 
printf ("====>getting from readFromClient: %s\n",pclient->name);
printf ("====>got from readFromClientn");
*/	

	/* now's a good time to check log file size */
	checkLogFile(pclient->pserver);

	/* clear partial buffer in writeMessagesToLog, clearing entire buffer here inadvertently deletes last message */
	/* try clearing pclient->recvbuf */
/*	memset(pclient->recvbuf, '\0', sizeof(pclient->recvbuf)); */

/*	logTime(pclient); 
	getTimestamp(pclient->ascii_time);
*/

	size = (int) (sizeof(pclient->recvbuf) - pclient->nChar);
	recvLength = recv(pclient->insock,
		      &pclient->recvbuf[pclient->nChar],
		      size,
		      0);
	if (recvLength <= 0) {
		if (recvLength<0) {
            int errnoCpy = SOCKERRNO;
			if (errnoCpy==SOCK_EWOULDBLOCK || errnoCpy==SOCK_EINTR) {
				return;
			}
			if (errnoCpy != SOCK_ECONNRESET &&
				errnoCpy != SOCK_ECONNABORTED &&
				errnoCpy != SOCK_EPIPE &&
				errnoCpy != SOCK_ETIMEDOUT
				) {
                char sockErrBuf[64];
                epicsSocketConvertErrnoToString ( sockErrBuf, sizeof ( sockErrBuf ) );
				fprintf(stderr, 
		"%s:%d socket=%d size=%d read error=%s\n",
					__FILE__, __LINE__, pclient->insock, 
					size, sockErrBuf);
			}
		}
		/*
		 * disconnect
		 */
		freeLogClient (pclient);
		return;
	}

	pclient->nChar += (size_t) recvLength;

/* TESTING RECEIVE 
printf ("***RECEIVED:  msglen=%d  msg=%s\n", pclient->nChar, pclient->recvbuf);

printf ("====>writing!\n");
*/
/*	writeMessagesToLog (pclient); */

printf("readFromClient incoming recvbuf  %s\n\n", pclient->recvbuf);

	parseMessages(pclient); 
}


void caEventHandler(evargs args)
{
printf("caEventHandler()\n");
	int *pvtype;
	char timestamp[ASCII_TIME_SIZE];

	if (args.status == ECA_NORMAL) {
		printf("event dbr=%i\n", *((int*)args.dbr));
		pvtype = (int *)ca_puser(args.chid);
		if (pvtype == &ioc_log_caThrottleSecondsPvType) {
			printf("event for throttle seconds pvtype\n");
			ioc_log_throttleSeconds = *((int*)args.dbr);
		} else if (pvtype == &ioc_log_caThrottleFieldsPvType) {
			printf("event for throttle fields pvtype\n");
			ioc_log_throttleFields = *((int*)args.dbr);
		}
	}

	getTimestamp(timestamp);
	printf("new ca PV event: ioc_log_throttleSeconds=%d, ioc_log_throttleFields=%d\n", ioc_log_throttleSeconds, ioc_log_throttleFields);
	fprintf(ioc_log_plogfile, "%s: new ca PV event: ioc_log_throttleSeconds=%d, ioc_log_throttleFields=%d\n", timestamp, ioc_log_throttleSeconds, ioc_log_throttleFields);
}

void caConnectionHandler(struct connection_handler_args args)
{
	int rc;
	int dbtype;
	int *pvtype;

printf("caConnectionHandler()\n");
	pvtype = (int *)ca_puser(args.chid);
printf("pvtype=%p\n", pvtype);
	if (pvtype == &ioc_log_caThrottleSecondsPvType) {
		printf("PV_THROTTLE_SECONDS type\n");
		dbtype = DBR_INT;
	} else if (pvtype == &ioc_log_caThrottleFieldsPvType) {
		printf("PV_THROTTLE_FIELDS type\n");
		dbtype = DBR_INT;
	}

	if (args.op == CA_OP_CONN_UP) {
		printf("monitor STARTED\n");
		/* start monitor */
/*		rc = ca_create_subscription(dbtype, 1, args.chid, DBE_VALUE, (caCh*)caEventHandler, &pvtype, NULL); */
		rc = ca_create_subscription(dbtype, 1, args.chid, DBE_VALUE, caEventHandler, &pvtype, NULL); 
		if (rc != ECA_NORMAL) {
			fprintf(stderr, "CA error %s occured while trying to create channel subscription.\n", ca_message(rc));
		}
	} else { /* connection not up, use default value */
		fprintf(stderr, "CA connection not established.\n");
	}
}

static int caStartChannelAccess()
{
	int rc = IOCLS_OK;

	/* Start up Channel Access */
	printf("caStartChannelAccess()\n");
/*	rc = ca_context_create(ca_disable_preemptive_callback); */
	rc = ca_context_create(ca_enable_preemptive_callback); 
	if (rc != ECA_NORMAL) {
		fprintf(stderr, "CA error %s occurred while trying to start channel access.\n", ca_message(rc));
		return IOCLS_ERROR;
    } else {
		rc = IOCLS_OK;
	}
	fprintf(stderr, "CA rc %s occurred while trying to start channel access.\n", ca_message(rc));
	
	return rc;
}

/* setupPVMonitors 
// Setup channel access pv monitors for throttle settings, one for each pv
*/
static int caStartMonitor(char *pvname, int *pvtype)
{
	int rc;
	int priority=80;
	int timeout=1;
	chid chid;

printf("caStartMonitor()\n");
	/* Create CA connections */
	rc = ca_create_channel(pvname, (caCh*)caConnectionHandler, pvtype, priority, &chid); 
	if (rc != ECA_NORMAL) {
		fprintf(stderr, "CA error %s occurred while trying to create channel '%s'.\n", ca_message(rc), pvname);
		return IOCLS_ERROR;
	}
	fprintf(stderr, "CA %s occurred while trying to create channel '%s'.\n", ca_message(rc), pvname);

	printf("%s, pvtype=%p\n", pvname, pvtype);

	/* Check for channels that didn't connect */
    ca_pend_event(timeout);

	return IOCLS_OK;
}


/* getThrottleString
// Concatenates valid columns to create string for database to use as constraint
// #define THROTTLE_MSG         1 << 0    // 1 
// #define THROTTLE_FACILITY    1 << 1    // 2 
// #define THROTTLE_SEVERITY    1 << 2    // 4 
// #define THROTTLE_CODE        1 << 3    // 8 
// #define THROTTLE_HOST        1 << 4    // 16 
// #define THROTTLE_USER        1 << 5    // 32 
// #define THROTTLE_STATUS      1 << 6    // 64 
// #define THROTTLE_PROCESS     1 << 7    // 128 
*/
static void getThrottleString(char *msg, char *facility, char *severity, char *code, char *host, char *user, char *status, char *process, char *throttleString, int throttleStringMask)
{
/*	printf("throttle mask:\n msg=%d\n facility=%d\n severity=%d\n error=%d\n host=%d\n user=%d\n status=%d\n process=%d\n", 
			THROTTLE_MSG, THROTTLE_FACILITY, THROTTLE_SEVERITY, THROTTLE_ERROR, THROTTLE_HOST, THROTTLE_USER, THROTTLE_STATUS, THROTTLE_PROCESS);
*/

	memset(throttleString, '\0', THROTTLE_MSG_SIZE);

	printf(" throttleMask=%d, ", throttleStringMask);

	if (throttleStringMask & THROTTLE_MSG) { strcpy(throttleString, msg); printf("-msg"); }
	if (throttleStringMask & THROTTLE_FACILITY) { strcat(throttleString, facility); printf("-facility"); }
	if (throttleStringMask & THROTTLE_SEVERITY) { strcat(throttleString, severity); printf("-severity"); }
	if (throttleStringMask & THROTTLE_CODE) { strcat(throttleString, code); printf("-code"); } 
	if (throttleStringMask & THROTTLE_HOST) { strcat(throttleString, host); printf("-host"); }
	if (throttleStringMask & THROTTLE_USER) { strcat(throttleString, user); printf("-user"); }
	if (throttleStringMask & THROTTLE_STATUS) { strcat(throttleString, status); printf("-user"); }
	if (throttleStringMask & THROTTLE_PROCESS) { strcat(throttleString, process); printf("-process"); }
	printf("\n");

	printf(" throttleString='%s'\n", throttleString);
}


/* getThrottleTimestamp
// Truncates second field of app timestamp based on throttleSeconds
// Currently ONLY TRUNCATES up to 1 day, resets at 00:00:00
// AppTimestamp format should be 14-Feb-2012 10:10:38.00
*/
static void getThrottleTimestamp(char *appTimestamp, char* throttleTimestamp, int throttleSeconds)
{
	char *pch;
	int seconds=0;
	int minutes=0;
	int hour=0;
	int tseconds=0;
	int remainder=0;


	printf("getThrottleTimestamp appTimestamp=%s\n", appTimestamp);
	/* assume timestamp in format 14-Feb-2012 10:10:38.00 */
	pch = strchr(appTimestamp, ' ');
	pch = pch+1;
	hour = atoi(pch);
	pch = strchr(appTimestamp, ':');
	pch = pch+1;
	minutes = atoi(pch);
	pch = strchr(pch, ':');
	pch = pch+1; /* pointer to xy.00 */

	/* printf("pch=%s\n", pch); */
	seconds = atoi(pch) + (hour*60 + minutes)*60;

	/* calculate throttled/rounded seconds from start of day */
	remainder = seconds % throttleSeconds;
	tseconds = seconds - remainder;

	/* now build up throttle timestamp */
	struct tm time;
	/* convert string time to time struct */
	strptime(appTimestamp, "%d-%b-%Y %H:%M:%S", &time);
	time.tm_hour = 0;
	time.tm_min = 0;
	time.tm_sec = tseconds;
	mktime(&time);
	strftime(throttleTimestamp, sizeof(throttleTimestamp), "%d-%b-%Y %H:%M:%S", &time);	
	printf(" throttleSeconds=%d, appTimestamp='%s', throttleTimestamp='%s'\n", throttleSeconds, appTimestamp, throttleTimestamp);
}

/*
 * parseMessages()
 * parse iocLogClient recvbuff and process messages
 */
static void parseMessages(struct iocLogClient *pclient)
{
	int rc=0;
	int i=0;
	int isComplete = 0;
	int index;
	int end=0;
	int lastCarriageReturnIndex=0;
	int lastMsgLen=0;
	size_t nchar;
	int ncharStripped;
	char onerow[THROTTLE_MSG_SIZE]; /* single message */
	char *pch;
	char *prevpch;
	int rowlen;
	char buff[sizeof(pclient->recvbuf)];

	/* message parameters */
	char appTime[ASCII_TIME_SIZE];
	char throttleTime[ASCII_TIME_SIZE];
	char status[SEVERITY_SIZE];
	char code[MSG_CODE_SIZE];
	char severity[SEVERITY_SIZE]; 
	char system[FACILITY_SIZE];
	char host[HOSTNODE_SIZE];
	char process[PROCESS_SIZE];
	char user[USER_NAME_SIZE];
	char text[MSG_SIZE];
	int appTimeDef = 0;
	int count=1;
	int commit=1;
	char throttleString[THROTTLE_MSG_SIZE];


/*	printf("lineIndex=%d\n, nChar=%d\n", lineIndex, pclient->nChar);
	printf("\nINCOMING recvbuf : %s\n", pclient->recvbuf);
*/
	end = pclient->nChar;
	prevpch = pclient->recvbuf;
	index=0;

	/* find last carriage return */
	strncpy(buff, pclient->recvbuf, pclient->nChar);
	for (i=0; i<pclient->nChar; i++) {
		if (buff[i] == '\n') { /* found complete message */
			lastCarriageReturnIndex=i;
/*			printf("real carriage return\n"); */
/*			printf("real onerow buff=%s\n", buff); */
		} else if (strncmp(&buff[i-3], "\\n", 3) == 0) { /* weird \\n carriage return from iocsh */
/*			printf("testing buff=%s\n", buff); */
/*			printf("weird \\n carriage return from iocsh\n"); */
			lastCarriageReturnIndex=i-2;			
			buff[i-2]='\n';
/*			printf("onerow buff=%s\n", buff); */
		}
	}
/*	printf("lastCarriageReturnIndex=%d\n", lastCarriageReturnIndex); */
	memset(buff, '\0', sizeof(buff));

	strncpy(buff, pclient->recvbuf, lastCarriageReturnIndex);
/*	printf("consolidated buffer=%s\n", buff); */

	/* tokenize temp buffer to get individual messages */
	pch = strtok(buff, "\n");
	while (pch != NULL) {
		printf("\n-------------------------\n");
		strcpy(onerow, pch);
		isComplete=0;

		rowlen = strlen(onerow);

		/* get logserver timestamp */
		getTimestamp(pclient->ascii_time); 
/*		printf("client ascii_time=%s\n", pclient->ascii_time); */

		/* get message attributes */
		ncharStripped = parseTags(nchar, pclient->name, pclient->ascii_time, onerow, 
		                          appTime, status, severity, system, host, code, process, user, &appTimeDef);

/*		printf("ncharStripped=%d\n", ncharStripped); */

		/* get message text */
		strncpy(text, &onerow[ncharStripped], rowlen-ncharStripped);
		text[rowlen-ncharStripped] = 0;

		printf("text: '%s'\n", text);

		/* format throttling timestamp */
		getThrottleTimestamp(appTime, throttleTime, ioc_log_throttleSeconds);

		/* format throttling string */
		getThrottleString(text, system, severity, code, host, user, status, process, throttleString, ioc_log_throttleFields);
		
/*		printf("ioc_log_program_name=%s\n", ioc_log_program_name); */
		rc = db_insert(0, ioc_log_programName, system, severity, text, pclient->ascii_time, appTime, throttleTime, appTimeDef,
		               code, host, user, status, process, count, throttleString, commit); 
		
		if (rc == 1) {
			/* printf("%s\n", "SUCCESSFUL INSERT INTO ERRLOG TABLE"); */
			printf("%s %s %s\n", system,host,"msg2Oracle");
/*			rc = fprintf(pclient->pserver->poutfile, "%s: SUCCESS inserting %s\n", pclient->ascii_time, onerow);  */
		} else {
			printf("%s for message:  %s %s %s\n", "ERROR INSERTING INTO ERRLOG TABLE", system, host, text);
			/* TODO: save entire message to data file for future processing */
			printf("printing unsuccessful row : '%s'\n", onerow);
			rc = fprintf(pclient->pserver->poutfile, "%s: ERROR inserting %s\n", pclient->ascii_time, onerow);
		}
		
		pch = strtok(NULL, "\n");
		memset(onerow, '\0', THROTTLE_MSG_SIZE);
	}

	printf("  DONE PARSING RECVBUF \n\n");

	/* clean up recvbuf */

/*	printf("lastCarriageReturnIndex=%d\n, pclient->nChar=%d\n",lastCarriageReturnIndex, pclient->nChar); */
	lastMsgLen= pclient->nChar - lastCarriageReturnIndex - 1;
	/* handle truncated message */
	/* make sure this is a truncated message with no "\n" and not just end of clean recvbuf */
/*	if (pch!=NULL && isComplete==0) { */
	if (lastMsgLen > 0) { /* shift partial message to front of recvbuf */
/*
printf("\n\npartial message remaining, lastCarriageReturnIndex=%d, nChar=%d \n", lastCarriageReturnIndex, pclient->nChar);
printf("  PARTIAL message='%s'\n",&pclient->recvbuf[lastCarriageReturnIndex]);
*/
		memmove(pclient->recvbuf, &pclient->recvbuf[lastCarriageReturnIndex], lastMsgLen+1);  /* move incomplete entry to beginning */
		memset(&pclient->recvbuf[lastMsgLen+1], '\0', sizeof(pclient->recvbuf)-(lastMsgLen+1)); /* clear rest of buffer */
		pclient->nChar=lastMsgLen+1;
	} else { /* clear entire recbuf */
		memset(pclient->recvbuf, '\0', sizeof(pclient->recvbuf));
		pclient->nChar=0;
	}
	printf("new recvbuf='%s'\n", pclient->recvbuf);

}


/* checks if there is another tag within this message */
static int hasNextTag(char *text, char *found) 
{
	int rc=0;
	found = NULL;
	char buff[MSG_SIZE*2];
	int len;

	memset(buff, '\0', MSG_SIZE*2);

	/* copy text into buffer so we can manipulate it */
	strcpy(buff, text);
	len = strlen(buff);

	found = strstr(buff,"stat=");
	/* tag is still 5 chars long, don't need to reset */
	if (found == NULL) { found = strstr(buff,"sevr="); } 
	if (found == NULL) { found = strstr(buff,"host="); } 
	if (found == NULL) { found = strstr(buff,"code="); } 
	if (found == NULL) { found = strstr(buff,"proc="); } 
	if (found == NULL) { found = strstr(buff,"user="); } 
	if (found == NULL) { found = strstr(buff,"time="); } 
	if (found == NULL) {	
		/* shorter tag, reset buffer */
		memset(buff, '\0', MSG_SIZE*2);
		strncpy(buff, text-4, len);
	 	found = strstr(buff,"fac="); 
	} 
	if (found == NULL) { 
		/* longer tag, reset buffer */
		memset(buff, '\0', MSG_SIZE*2);
		strncpy(buff, text-9, len);
		found = strstr(buff,"facility="); 
	} 	
	
	/* was a tag found */
	if (found != NULL) rc=1;

/*	printf("rc=%d\n", rc); */

	return rc;
}


/* the first 4 parameters are input from the pclient structure 
// the others are the parsed tag strings 
//
// timeIn : logserver time
// timeApp : application time 
//
// Assumptions : No spaces in between "=" of tag/value, e.g. facility=ioc status=test
//               Single space in between each tag/value
*/
static int parseTags(int nchar, char *hostIn, char *timeIn, char *text, char *timeApp,  char *status, char *severity, 
                     char *facility, char *host, char *code, char *process, char *user, int *timeDef) {
	char *charPtr = text;  
	char *tagPtr;         
	char *lastPtr;
	char *valPtr;
	char *nextTagPtr;
	char *tmpPtr;
	int  ncharStripped = 0;
	int  charsize;
	int rc;
	int maxTagLen;
	char thisTag[1000];
/*  Initialize outputs.
**  default is IOC
**  host defaults to the host in pclient
*/
	memset(thisTag, '\0', 1000);
	memset(timeApp, '\0', sizeof(timeApp));  /* formatted app time */
	memset(status, '\0', sizeof(status));
	memset(severity, '\0', sizeof(severity));
	memset(facility, '\0', sizeof(facility));
	memset(host, '\0', sizeof(host));
	memset(code, '\0', sizeof(code));
	memset(process, '\0', sizeof(process));
	memset(user, '\0', sizeof(user));
	*timeDef=0;

	strcpy(facility, "IOC");
	maxTagLen = NAME_SIZE;    /* default maxTagLen to NAME_SIZE */

	printf("PARSE TAGS\n");
	printf("charPtr=%s\n", charPtr);

	/* start parsing message */
	while (charPtr) {
		tagPtr  = strchr(charPtr, '='); /* look for a valid tag */
		if (tagPtr) {
			nextTagPtr = strchr(tagPtr+1, '='); /* look for next tag */
			if (nextTagPtr) {
				/* make sure this is a tag and not a "=" within the message text */
				rc = hasNextTag(nextTagPtr, tmpPtr);					
				if (rc == 0) { /* no next tag, the "=" is within the message text */
					printf("no next tag for %s\n", nextTagPtr);
					lastPtr= strchr(tagPtr, ' '); /* look for space between this tag value and the message text, assume no space in this tag value */
				} else { /* this is a tag */
					tmpPtr = strchr(tagPtr, ' '); /* look for space, assume there will be at least one more space */
					while (tmpPtr < nextTagPtr) { 
						tmpPtr = strchr(tmpPtr, ' '); /* look for last space before next tag */
						if (tmpPtr < nextTagPtr) lastPtr = tmpPtr;
						if (tmpPtr) tmpPtr += 1;
					}
				}
			} else { /* last tag, assume next space is start of text */
				lastPtr = strchr(tagPtr, ' ');
			}
		}
		else lastPtr = 0;

		/* find a tag */
		if (tagPtr && lastPtr) {
			if ( !strncmp(charPtr,"stat=",5)) { valPtr = status; maxTagLen = NAME_SIZE; strcpy(thisTag,"stat"); }
			else if ( !strncmp(charPtr,"sevr=",5)) { valPtr = severity; maxTagLen = SEVERITY_SIZE; strcpy(thisTag,"sevr"); }
			else if ( !strncmp(charPtr,"fac=" ,4)) { valPtr = facility; maxTagLen = FACILITY_SIZE; strcpy(thisTag,"fac"); }
			else if ( !strncmp(charPtr,"facility=" ,9)) { valPtr = facility; maxTagLen = FACILITY_SIZE; strcpy(thisTag,"facility"); }
			else if ( !strncmp(charPtr,"host=",5)) { valPtr = host; maxTagLen = HOSTNODE_SIZE; strcpy(thisTag,"host"); }
			else if ( !strncmp(charPtr,"code=",5)) { valPtr = code; maxTagLen = NAME_SIZE; strcpy(thisTag,"code"); }
			else if ( !strncmp(charPtr,"proc=",5)) { valPtr = process; maxTagLen = PROCESS_SIZE; strcpy(thisTag,"proc"); }
			else if ( !strncmp(charPtr,"user=",5)) { valPtr = user; maxTagLen = USER_NAME_SIZE; strcpy(thisTag,"user"); }
			else if ((!strncmp(charPtr,"time=",5)) && (charPtr[7]  == '-') && (charPtr[11] == '-') && (charPtr[16] == ' ')) {
				valPtr = timeApp; strcpy(thisTag,"time");
				/* there is a space in the time field (only field that allows a space before msg text), reset lastPtr just in case this is the last tag before msg text */
				lastPtr = strchr(tagPtr, ' ') + 1;
				lastPtr = strchr(lastPtr, ' '); 
			} else valPtr = 0;

			/* get value of the tag */
			if (valPtr) {
				charsize = lastPtr - tagPtr;
				if (charsize > maxTagLen) charsize = maxTagLen;
				tagPtr++;
				strncpy(valPtr, tagPtr, charsize);
				valPtr[charsize-1] = 0;
				printf("Tag: %s, Value: %s\n", thisTag, valPtr);
				lastPtr++;
				ncharStripped = lastPtr - text;
				charPtr = lastPtr;
			} else {  /* didn't find a value */
				charPtr = 0;
			}
		} else { /* no tag, end of pclient recv buffer */
			charPtr = 0;
		}
	}
/*	printf("Msg: %s\n", text);  */

	/* now clean up data and handle defaults */

	/* no host defined, set default */
	if (strlen(host) == 0) { 	
		strncpy(host, hostIn, HOSTNODE_SIZE);
		host[NAME_SIZE-1]=0;
		/* Remove IP domain name */
		charPtr = strchr(host, '.');
		if (charPtr) *charPtr = 0;
	}
	/* prepend "SLC" to anything coming from mcc host 
	if ((strncmp(host, "MCC", 3)==0) || (strncmp(host, "mcc.slac.stanford.edu", 21)==0)) {
		printf("current facility=%s\n", facility);
		strcpy(buff, "SLC-");
		strcat(buff, facility);
		strcpy(facility, buff);
		printf("new facility=%s\n", facility);
	}		
*/
	/* check if timestamp passed in */
	if (strlen(timeApp) == 0) { /* no timestamp defined, set default to logserver current time */
		strcpy(timeApp, timeIn);
		*timeDef = 1;
		printf("no timestamp defined, use log server's, set timeDef=1\n");
	} else {
		*timeDef = 0;
		printf("app timestamp defined, set timeDef=0\n");
	}

	return ncharStripped;
}

 
/*
 * freeLogClient ()
 */
static void freeLogClient(struct iocLogClient     *pclient)
{
	int		status;

#	ifdef	DEBUG
	if(length == 0){
		fprintf(stderr, "iocLogServer: nil message disconnect\n");
	}
#	endif

	/*
	 * flush any left overs
	 */
	if (pclient->nChar) {
		/*
		 * this forces a flush
		 */
		if (pclient->nChar<sizeof(pclient->recvbuf)) {
			pclient->recvbuf[pclient->nChar] = '\n';
		}
/*		writeMessagesToLog (pclient); */
		parseMessages(pclient); 
	}

	status = fdmgr_clear_callback(
		       pclient->pserver->pfdctx,
		       pclient->insock,
		       fdi_read);
	if (status!=IOCLS_OK) {
		fprintf(stderr, "%s:%d fdmgr_clear_callback() failed\n",
			__FILE__, __LINE__);
	}

	epicsSocketDestroy ( pclient->insock );

	free (pclient);

	return;
}

/* get current timestamp in format 14-Feb-2012 14:24:02.09 */
/* assume timestamp is of length NAME_SIZE */
static void getTimestamp(char *timestamp) 
{
	struct timeval tv;
	struct tm* ptm;
	long milliseconds;
	char milli[10];

	memset(timestamp, '\0', sizeof(timestamp));

	/* get time */
	gettimeofday(&tv, NULL);
	ptm = localtime(&tv.tv_sec);

	/* format to seconds */
	strftime(timestamp, ASCII_TIME_SIZE, "%d-%b-%Y %H:%M:%S", ptm);
/*	strftime(throttlingTimestamp, NAME_SIZE, "%d-%b-%Y %H:%M:%S", &time);	*/

	/* format year */
/*	strftime(year, 10, "%Y", ptm); */

	/* compute milliseconds */
	milliseconds = tv.tv_usec / 1000;

	/* add milliseconds */
	sprintf(milli, ".%03ld", milliseconds);
	strcat(timestamp, milli);
/*	strcat(timestamp, year); */
}

/*
 *
 *	getConfig()
 *	Get Server Configuration
 *
 *
 */
static int getConfig(void)
{
	int	status;
	char	*pstring;
	long	param;

	status = envGetLongConfigParam(
			&EPICS_IOC_LOG_PORT, 
			&param);
	if(status>=0){
		ioc_log_port = (unsigned short) param;
	}
	else {
		ioc_log_port = 7004U;
	}

	status = envGetLongConfigParam(
			&EPICS_IOC_LOG_FILE_LIMIT, 
			&ioc_log_fileLimit);
	if(status>=0){
		if (ioc_log_fileLimit<=0) {
			envFailureNotify (&EPICS_IOC_LOG_FILE_LIMIT);
			return IOCLS_ERROR;
		}
	}
	else {
		ioc_log_fileLimit = 10000;
	}

	pstring = envGetConfigParam(
			&EPICS_IOC_LOG_FILE_NAME, 
			sizeof ioc_log_fileName,
			ioc_log_fileName);

	/*
	 * its ok to not specify the IOC_LOG_FILE_COMMAND
	 */
	pstring = envGetConfigParam(
			&EPICS_IOC_LOG_FILE_COMMAND, 
			sizeof ioc_log_fileCommand,
			ioc_log_fileCommand);
	return IOCLS_OK;

}


/*
 *
 *	failureNotify()
 *
 *
 */
static void envFailureNotify(const ENV_PARAM *pparam)
{
	fprintf(stderr,
		"iocLogServer: EPICS environment variable `%s' undefined\n",
		pparam->name);
}


#ifdef UNIX
static int setupSIGHUP(struct ioc_log_server *pserver)
{
	int status;
	struct sigaction sigact;

	status = getDirectory();
	if (status<0){
		fprintf(stderr, "iocLogServer: failed to determine log file directory\n");
		return IOCLS_ERROR;
	}

	/*
	 * Set up SIGHUP handler. SIGHUP will cause the log file to be
	 * closed and re-opened, possibly with a different name.
	 */
	sigact.sa_handler = sighupHandler;
	sigemptyset (&sigact.sa_mask);
	sigact.sa_flags = 0;
	if (sigaction(SIGHUP, &sigact, NULL)){
		fprintf(stderr, "iocLogServer: %s\n", strerror(errno));
		return IOCLS_ERROR;
	}
	
	status = pipe(sighupPipe);
	if(status<0) {
		fprintf(stderr, "iocLogServer: failed to create pipe because `%s'\n", strerror(errno));
		return IOCLS_ERROR;
	}

	status = fdmgr_add_callback(
			pserver->pfdctx, 
			sighupPipe[0], 
			fdi_read,
			serviceSighupRequest,
			pserver);
	if (status<0) {
		fprintf(stderr,	"iocLogServer: failed to add SIGHUP callback\n");
		return IOCLS_ERROR;
	}
	return IOCLS_OK;
}

/*
 *
 *	sighupHandler()
 *
 *
 */
static void sighupHandler(int signo)
{
	(void) write(sighupPipe[1], "SIGHUP\n", 7);
}


/*
 *	serviceSighupRequest()
 *
 */
static void serviceSighupRequest(void *pParam)
{
	struct ioc_log_server	*pserver = (struct ioc_log_server *)pParam;
	char			buff[256];
	int			status;

	/*
	 * Read and discard message from pipe.
	 */
	(void) read(sighupPipe[0], buff, sizeof buff);

	/*
	 * Determine new log file name.
	 */
	status = getDirectory();
	if (status<0) {
		fprintf(stderr, "iocLogServer: failed to determine new log file name\n");
		return;
	}

	/*
	* If it's changed, open the new file.
	*/
	if (strlen(ioc_log_fileName)) {
		if (strcmp(ioc_log_fileName, pserver->outfile) == 0) {
			fprintf(stderr,	"iocLogServer: log file name unchanged; not re-opened\n");
		} else {
			status = openLogFile(pserver);
			if (status<0) {
				fprintf(stderr, "File access problems to `%s' because `%s'\n", ioc_log_fileName, strerror(errno));
				strcpy(ioc_log_fileName, pserver->outfile);
				status = openLogFile(pserver);
				if (status<0) {
					fprintf(stderr, "File access problems to `%s' because `%s'\n", ioc_log_fileName, strerror(errno));
					return;
				} else {
					fprintf(stderr, "iocLogServer: re-opened old log file %s\n", ioc_log_fileName);
				}
			} else {
				fprintf(stderr,	"iocLogServer: opened new log file %s\n", ioc_log_fileName);
			}
		}
	}
}


/*
 *
 *	getDirectory()
 *
 *
 */
static int getDirectory(void)
{
	FILE		*pipe;
	char		dir[256];
	int		i;

	if ((ioc_log_fileCommand[0] != '\0') && (ioc_log_fileName[0] != '\0')) {
		/*
		 * Use popen() to execute command and grab output.
		 */
		pipe = popen(ioc_log_fileCommand, "r");
		if (pipe == NULL) {
			fprintf(stderr, "Problem executing `%s' because `%s'\n", ioc_log_fileCommand, strerror(errno));
			return IOCLS_ERROR;
		}
		if (fgets(dir, sizeof(dir), pipe) == NULL) {
			fprintf(stderr, "Problem reading o/p from `%s' because `%s'\n", ioc_log_fileCommand, strerror(errno));
			return IOCLS_ERROR;
		}
		(void) pclose(pipe);

		/*
		 * Terminate output at first newline and discard trailing
		 * slash character if present..
		 */
		for (i=0; dir[i] != '\n' && dir[i] != '\0'; i++)
			;
		dir[i] = '\0';

		i = strlen(dir);
		if (i > 1 && dir[i-1] == '/') dir[i-1] = '\0';

		/*
		 * Use output as directory part of file name.
		 */
		if (dir[0] != '\0') {
			char *name = ioc_log_fileName;
			char *slash = strrchr(ioc_log_fileName, '/');
			char temp[256];

			if (slash != NULL) name = slash + 1;
			strcpy(temp,name);
			sprintf(ioc_log_fileName,"%s/%s",dir,temp);
		}
	}
	return IOCLS_OK;
}
#endif


/* THIS WORKS IN LINE!!  JUST TO SAVE IT.
    struct tm time_tm;

    *pclientTime  = strptime(pclient->ascii_time, "%a %b %d %T %Y", &time_tm);
    if (pclientTime) {
      strftime(pclientTime, NAME_SIZE, "%d-%b-%Y %T.00", &time_tm);
    } else {
      strcpy(pclientTime, "00-000-0000 00:00:00.00");
    }
    pclientTime[NAME_SIZE-1]=0;
*/
