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
 *
 *      03-Mar-2015 Bob Hall
 *          Modified to set the program tag of messages with a
 *          facility tag that starts with "DpSlc" (from the VMS
 *          SLC Aida data provider processes) to FACET as a
 *          special case.  All AIDAPROD (including VMS SLC Aida
 *          data provider) processes send their messages to
 *          the LCLS iocLogMsgServer process on lcls-daemon2,
 *          where by default all messages have their program
 *          tag set to LCLS.  The complaint was that the VMS
 *          SLC Aida data provider processes were having their
 *          program tag set to LCLS although requests for
 *          services from these processes are known to only
 *          come from FACET clients.

 */

/*
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
*/
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

#include "iocLogMsgServer.h"
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
//	char throttleSecondsPv[100];
//	char throttleFieldsPv[100];
	osiSockIoctl_t	optval;
	char timestamp[ASCII_TIME_SIZE];

	int ntestrows=0;  /* delete me */
	ntestrows=0;

	// get environment variables
	status = getConfig();
	if(status<0){
		fprintf(stderr, "iocLogServer: EPICS environment underspecified\n");
		fprintf(stderr, "iocLogServer: failed to initialize\n");
		return IOCLS_ERROR;
	}

	/* initialize variables */
	initGlobals();

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
			strcpy(ioc_log_throttleSecondsPv, argv[i]+strlen(buff));
		}
		/* get throttle fields pv */
		strcpy(buff, "throttleFieldsPv=");
		if (strncmp(argv[i], buff, strlen(buff)) == 0) {
			strcpy(ioc_log_throttleFieldsPv, argv[i]+strlen(buff));
		}
		/* get database */
		strcpy(buff, "db=");
		if (strncmp(argv[i], buff, strlen(buff)) == 0) {
			strcpy(ioc_log_database, argv[i]+strlen(buff));
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

	/* open log file */
	status = openLogFile(ioc_log_fileName, &ioc_log_plogfile, ioc_log_fileLimit);
	status = openLogFile(ioc_log_verboseFileName, &ioc_log_pverbosefile, MAX_VERBOSE_FILESIZE*ioc_log_fileLimit);
	getTimestamp(timestamp, sizeof(timestamp));
	fprintf(ioc_log_plogfile, "\n\n========================================================================================================\n");
	fprintf(ioc_log_plogfile, "%s: %s STARTED on %s\n", timestamp, VERSION, ioc_log_hostname);

	printf("ioc_log_program_name=%s\nioc_log_throttleSecondsPv=%s\nioc_log_throttleFieldsPv=%s\nioc_log_database=%s\n", ioc_log_programName, ioc_log_throttleSecondsPv, ioc_log_throttleFieldsPv, ioc_log_database);
	fprintf(ioc_log_plogfile, "ioc_log_program_name=%s\nioc_log_throttleSecondsPv=%s\nioc_log_throttleFieldsPv=%s\nioc_log_database=%s\n", ioc_log_programName, ioc_log_throttleSecondsPv, ioc_log_throttleFieldsPv, ioc_log_database);

	/* setup chchannel access pv monitoring for logserver throttle settings */
	status = caStartChannelAccess();
	if (status == IOCLS_OK) {
		caStartMonitor(ioc_log_throttleSecondsPv, &ioc_log_throttleSecondsPvType);
		caStartMonitor(ioc_log_throttleFieldsPv, &ioc_log_throttleFieldsPvType);
	}
	getTimestamp(timestamp, sizeof(timestamp));
	fprintf(ioc_log_pverbosefile, "PV Monitoring:\nioc_log_programName=%s\nioc_log_throttleSeconds=%d\nioc_log_throttleFields=%d\n", ioc_log_programName, ioc_log_throttleSeconds, ioc_log_throttleFields);
	fprintf(ioc_log_plogfile, "%s: PV Monitoring:	 ioc_log_programName=%s, ioc_log_throttleSeconds=%d, ioc_log_throttleFields=%d\n", timestamp, ioc_log_programName, ioc_log_throttleSeconds, ioc_log_throttleFields);


	// connect to Oracle
//	if (!db_connect("MCCODEV")) {
	if (!db_connect(ioc_log_database)) {		
		getTimestamp(timestamp, sizeof(timestamp));
		fprintf(ioc_log_pverbosefile, "%s\n", "iocLogMsgServer: Error connecting to Oracle\n");
		fprintf(ioc_log_plogfile, "%s: ERROR connecting to %s!\n", timestamp, ioc_log_database);
	    	return IOCLS_ERROR;
	}

	fprintf(stderr, "connected to %s!\n", ioc_log_database);
	getTimestamp(timestamp, sizeof(timestamp));
	fprintf(ioc_log_plogfile, "%s: Successfully connected to %s\n", timestamp, ioc_log_database);

	pserver = (struct ioc_log_server *) 
	calloc(1, sizeof *pserver);
	if(!pserver){
		fprintf(stderr,  "iocLogServer: %s\n", strerror(errno));
		fprintf(ioc_log_plogfile, "iocLogServer: %s\n", strerror(errno));
		return IOCLS_ERROR;
	}

	pserver->pfdctx = (void *) fdmgr_init();
	if(!pserver->pfdctx){
		fprintf(stderr, "iocLogServer: %s\n", strerror(errno));
		fprintf(ioc_log_plogfile, "iocLogServer: %s\n", strerror(errno));
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
		fprintf(ioc_log_plogfile, "iocLogServer: sock create err: %s\n", sockErrBuf);
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
		fprintf(ioc_log_plogfile, "iocLogServer: bind err: %s\n", sockErrBuf );
		fprintf (ioc_log_plogfile, "iocLogServer: a server is already installed on port %u?\n", (unsigned)ioc_log_port);
		return IOCLS_ERROR;
	}

	/* listen and accept new connections */
	status = listen(pserver->sock, 10);
	if (status<0) {
        char sockErrBuf[64];
        epicsSocketConvertErrnoToString ( sockErrBuf, sizeof ( sockErrBuf ) );
		fprintf(stderr, "iocLogServer: listen err %s\n", sockErrBuf);
		fprintf(ioc_log_plogfile, "iocLogServer: listen err %s\n", sockErrBuf);
		return IOCLS_ERROR;
	}

	/*
	 * Set non blocking IO
	 * to prevent dead locks
	 */
	optval = TRUE;
	status = socket_ioctl(pserver->sock, FIONBIO, &optval);
	if(status<0){
	        char sockErrBuf[64];
        	epicsSocketConvertErrnoToString ( sockErrBuf, sizeof ( sockErrBuf ) );
		fprintf(stderr, "iocLogServer: ioctl FIONBIO err %s\n", sockErrBuf);
		fprintf(ioc_log_plogfile, "iocLogServer: ioctl FIONBIO err %s\n", sockErrBuf);
		return IOCLS_ERROR;
	}

#	ifdef UNIX
		status = setupSIGHUP(pserver);
		if (status<0) {
			return IOCLS_ERROR;
		}
#	endif

	getTimestamp(timestamp, sizeof(timestamp));
	fprintf(ioc_log_plogfile, "%s: ioc_log_programName=%s, throttleSecondsPv=%s, throttleFieldsPv=%s\n", timestamp, ioc_log_programName, ioc_log_throttleSecondsPv, ioc_log_throttleFieldsPv);

	status = fdmgr_add_callback(
			pserver->pfdctx, 
			pserver->sock, 
			fdi_read,
			acceptNewClient,
			pserver);
	if (status<0) {
		fprintf(stderr,	"iocLogServer: failed to add read callback\n");
		fprintf(ioc_log_plogfile, "iocLogServer: failed to add read callback\n");
		return IOCLS_ERROR;
	}
        
	/*
	 * Initialize connection to msg_handler.
	 */

	/* JROCK Connect to Oracle */
/*	if (!db_connect("MCCODEV")) {
	        fprintf(stderr, "%s\n", "iocLogMsgServer: Error connecting to Oracle\n");
        	return IOCLS_ERROR;
	}
*/
        /* RUN WITHOUT CONNECTION TO SPEAR MSGSENDER
        if (msgSenderInit() < 0) {
		fprintf(stderr, "iocLogServer: msgSender initialization failed\n");
		return IOCLS_ERROR;
	}
        */

	while(TRUE) {
		timeout.tv_sec = 60;            /*  1 min  */
		timeout.tv_usec = 0; 
		fdmgr_pend_event(pserver->pfdctx, &timeout); 
/*		if (pserver->poutfile) {
		  fflush(pserver->poutfile);
		}
*/
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
	printf("    Program=1\n");
	printf("    Message=2\n");
	printf("    Facility=4\n");
	printf("    Severity=8\n");
	printf("    Error=16 \n");
	printf("    Host=32\n");
	printf("    User=64\n");
	printf("    Status=128\n");
	printf("    Process=256\n\n");
}

static int isNumeric (const char * s)
{
    if (s == NULL || *s == '\0' || isspace(*s))
      return 0;
    char * p;
    strtod (s, &p);
    return *p == '\0';
}

static void initGlobals()
{
	char *pch;

	/* set defaults */
	ioc_log_throttleSeconds = 1;          /* 1 second */
	ioc_log_throttleFields = 3;           /* program/accelerator and msg field */
	strcpy(ioc_log_throttleSecondsPv, "SIOC:SYS0:OP00:MSGLOG_THRT_SEC");
	strcpy(ioc_log_throttleFieldsPv, "SIOC:SYS0:OP00:MSGLOG_THRT_FLD");
	strcpy(ioc_log_database, "MCCOMSG");

	strcpy(ioc_log_testDirectory, "");

	/* get hostname */
	gethostname(ioc_log_hostname, sizeof(ioc_log_hostname));
	printf("hostname=%s\n", ioc_log_hostname);

	// set default program 
	strcpy(ioc_log_programName, "LCLS");  /* LCLS accelerator */	
	
	gettimeofday(&ioc_log_insertErrorStartTime, NULL);  // reset insert error timer, try reconnect if getting errors for a long time
	ioc_log_prawdatafile = NULL;
	ioc_log_plogfile = NULL;
	ioc_log_pverbosefile = NULL;

	// define verbose file name
	pch=strrchr(ioc_log_fileName,'\\/');
	strncpy(ioc_log_verboseFileName, ioc_log_fileName, pch - ioc_log_fileName + 1);
	strcat(ioc_log_verboseFileName, "output.log");
	fprintf(stderr, "ioc_log_verboseFileName = %s\n", ioc_log_verboseFileName);

}


/* checkLogFile()
// checks file size and copies to <logfile>.log.bak if too big
*/
static int checkLogFile(char *filename, FILE** pfile, int maxsize)
{
	int fileSize=0;
	char newname[256];
	int rc=IOCLS_OK;
	int rrc = 0;
	char timestamp[ASCII_TIME_SIZE];

	if (*pfile) {
		fflush(*pfile);
	}

	/* get file size */
	fseek(*pfile, 0, SEEK_END);
	fileSize = ftell(*pfile);

	/* check if file is too big */
/*	if (fileSize > pserver->max_file_size) { */
	if (fileSize > maxsize) {
		strcpy(newname, filename);
		strcat(newname, ".bak");
		getTimestamp(timestamp, sizeof(timestamp));
		fprintf(*pfile, "%s: Logfile too big %d, Rename %s to %s\n", timestamp, fileSize, filename, newname);
		printf("%s: Logfile too big %d, Rename %s to %s\n", timestamp, fileSize, filename, newname);
		fclose(*pfile);
		rrc = rename(filename, newname);
		if (rrc != 0) fprintf(ioc_log_plogfile, "%s: ERROR renaming %s to %s, rc=%d\n", timestamp, filename, newname, rrc); /* don't return error, hopefully renaming works later */		
		/* reset logfile file pointer */
		*pfile = fopen(filename, "a+");
		if (!pfile) {
			*pfile = stderr;
			rc = IOCLS_ERROR;
			return rc;
		}
	}
	
	/* set file pos 
    pserver->filePos = ftell(pserver->poutfile); 
printf("pserver's filePos=%ld\n", pserver->filePos);
*/
	return rc;
}

/* raw data file is used to write backup data in case Oracle insert fails */
int writeToRawDataFile(char *appTime, char *program, char *facility, char *severity, char *code, char *host, char *user, char *status, char *process, char *text, int appTimeDef)
{
	int rc = IOCLS_OK;
	char date[ASCII_TIME_SIZE];
	char *pch;
	char directory[256];

	memset(directory, '\0', 256);

	// get date for raw data file name
	getDate(date, sizeof(date));
	fprintf(ioc_log_pverbosefile, "thedate = %s\n", date);

	// open new dated raw data file if none exists
	if (ioc_log_prawdatafile == NULL) {
		pch=strrchr(ioc_log_fileName,'\\/');
		strncpy(directory, ioc_log_fileName, pch - ioc_log_fileName + 1);
		fprintf(ioc_log_pverbosefile, "directory=%s\n", directory);
		strcpy(ioc_log_rawDataFileName, directory);
		strcat(ioc_log_rawDataFileName, "raw_data_");
		strcat(ioc_log_rawDataFileName, date);
		strcat(ioc_log_rawDataFileName, ".txt");
		fprintf(ioc_log_pverbosefile, "ioc_log_rawDataFileName = %s\n", ioc_log_rawDataFileName);

		// open the file for appending
		ioc_log_prawdatafile = fopen(ioc_log_rawDataFileName, "a+"); // open for appending, create if doesn't exist
		if (ioc_log_prawdatafile == NULL) {
			fprintf(ioc_log_pverbosefile, "******************\nERROR opening raw data file %s\n******************\n", ioc_log_rawDataFileName);
			fprintf(ioc_log_plogfile, "******************\nERROR opening raw data file %s\n******************\n", ioc_log_rawDataFileName);		
			return IOCLS_ERROR;
		}
	}

	// dump data to file
	fprintf(ioc_log_prawdatafile, "time=%s program=%s fac=%s sevr=%s code=%s host=%s user=%s stat=%s proc=%s %s\n", appTime, program, facility, severity, code, host, user, status, process, text);
	return rc;

}


/*
 *	openLogFile()
 *  set ioc_log_plogfile file handler
 * 
 * FIXME: does pserver need to be used for logfile now that Oracle is used??
 */
static int openLogFile(char *filename, FILE **pfile, int maxsize)
{
	int rc = IOCLS_OK;
	char timestamp[ASCII_TIME_SIZE];

	if (maxsize==0u) {
		printf("ERROR: ioc_log_fileLimit=0\n");
		return IOCLS_ERROR;
	}

/*	if (pserver->poutfile && pserver->poutfile != stderr){ 
		fclose (pserver->poutfile);
		pserver->poutfile = NULL;
printf("SET poutfile is NULL\n");
	}
*/
	if (*pfile && *pfile != stderr) {
		fclose(*pfile);
		*pfile = NULL;
		printf("Set %s file pointer to NULL\n", filename);
	}

    if (strlen(filename) > 0) {
		printf("********************LOG FILENAME is valid %s\n", filename);
		
		/* try opening for reading and writing */
		*pfile = fopen(filename, "a+"); // open for appending, create if doesn't exist
		if (pfile) {
			checkLogFile(filename, pfile, maxsize);
		} else { // problem opening file
			printf("******************\nERROR opening logfile %s\n******************\n", filename);
			*pfile = stderr;
			return IOCLS_ERROR;
		}
    } else {
		printf("INVALID log file name %s\n", filename);
		return IOCLS_ERROR;
	}

	printf("Successfully opened logfile %s\n", filename);
	getTimestamp(timestamp, sizeof(timestamp)); 
	printf("pfile=%p\n", *pfile);
	fprintf(*pfile, "%s: %s file opened\n", timestamp, filename);
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
	getTimestamp(pclient->ascii_time, sizeof(pclient->ascii_time)); 
	
	/* log */
	fprintf(ioc_log_plogfile, "%s: Accept new client %s\n", pclient->ascii_time, pclient->name); 
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
/*	checkLogFile(pclient->pserver); */
	// <FIXME:> implement a counter so not checking every time??	
	checkLogFile(ioc_log_fileName, &ioc_log_plogfile, ioc_log_fileLimit);
	checkLogFile(ioc_log_verboseFileName, &ioc_log_pverbosefile, MAX_VERBOSE_FILESIZE*ioc_log_fileLimit);

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
				fprintf(ioc_log_pverbosefile, "%s:%d socket=%d size=%d read error=%s\n", __FILE__, __LINE__, pclient->insock, size, sockErrBuf);
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

	//printf("readFromClient incoming recvbuf  %s\n\n", pclient->recvbuf);
	fprintf(ioc_log_pverbosefile, "readFromClient() %s incoming recvbuf\n", pclient->name);

	parseMessages(pclient); 
}


static void caEventHandler(evargs args)
{
printf("caEventHandler()\n");
	int *pvtype;
	char timestamp[ASCII_TIME_SIZE];
	getTimestamp(timestamp, sizeof(timestamp));

	if (args.status == ECA_NORMAL) {
		pvtype = (int *)ca_puser(args.chid);
		if (pvtype == &ioc_log_throttleSecondsPvType) {
			fprintf(ioc_log_pverbosefile, "event for throttle seconds pvtype\n");
			ioc_log_throttleSeconds = *((int*)args.dbr);
			fprintf(ioc_log_plogfile, "%s: CA EVENT for %s, ioc_log_throttleSeconds=%d\n", timestamp, ioc_log_throttleSecondsPv, ioc_log_throttleSeconds);
			fprintf(ioc_log_pverbosefile, "%s: CA EVENT for %s, ioc_log_throttleSeconds=%d\n", timestamp, ioc_log_throttleSecondsPv, ioc_log_throttleSeconds);
			fprintf(stderr, "%s: CA EVENT for %s, ioc_log_throttleSeconds=%d\n", timestamp, ioc_log_throttleSecondsPv, ioc_log_throttleSeconds);
		} else if (pvtype == &ioc_log_throttleFieldsPvType) {
			fprintf(ioc_log_pverbosefile, "event for throttle fields pvtype\n");
			ioc_log_throttleFields = *((int*)args.dbr);
			fprintf(ioc_log_plogfile, "%s: CA EVENT for %s, ioc_log_throttleFields=%d\n", timestamp, ioc_log_throttleFieldsPv, ioc_log_throttleFields);
			fprintf(ioc_log_pverbosefile, "%s: CA EVENT for %s, ioc_log_throttleFields=%d\n", timestamp, ioc_log_throttleFieldsPv, ioc_log_throttleFields);
			fprintf(stderr, "%s: CA EVENT for %s, ioc_log_throttleFields=%d\n", timestamp, ioc_log_throttleFieldsPv, ioc_log_throttleFields);
		}
	}

}

static void caConnectionHandler(struct connection_handler_args args)
{
	int rc;
	int dbtype;
	int *pvtype;
	char timestamp[ASCII_TIME_SIZE];

	getTimestamp(timestamp, sizeof(timestamp));

printf("caConnectionHandler()\n");
	pvtype = (int *)ca_puser(args.chid);
printf("pvtype=%p\n", pvtype);
	fprintf(ioc_log_plogfile, "==============================================================\n");
	if (pvtype == &ioc_log_throttleSecondsPvType) {
		printf("PV_THROTTLE_SECONDS type\n");
		fprintf(ioc_log_plogfile, "%s: %s\n", timestamp, ioc_log_throttleSecondsPv);
		dbtype = DBR_INT;
	} else if (pvtype == &ioc_log_throttleFieldsPvType) {
		printf("PV_THROTTLE_FIELDS type\n");
		fprintf(ioc_log_plogfile, "%s: %s\n", timestamp, ioc_log_throttleFieldsPv);
		dbtype = DBR_INT;
	}

	if (args.op == CA_OP_CONN_UP) {
		printf("\nmonitor STARTED\n");
		fprintf(ioc_log_plogfile, "%s: MONITOR started for pvtype=%p\n", timestamp, pvtype);
		/* start monitor */
/*		rc = ca_create_subscription(dbtype, 1, args.chid, DBE_VALUE, (caCh*)caEventHandler, &pvtype, NULL); */
		rc = ca_create_subscription(dbtype, 1, args.chid, DBE_VALUE, caEventHandler, &pvtype, NULL); 
		if (rc != ECA_NORMAL) {
			printf("ERROR ca_create_subscription(): rc=%d, msg='%s', pvtype=%p\n", rc, ca_message(rc), pvtype);
		}
	} else { /* connection not up, use default value */
		fprintf(stderr, "CA connection not established\n");
		fprintf(ioc_log_plogfile, "%s: ERROR CA connection not established for pvtype=%p ", timestamp, pvtype);
	}

	fprintf(ioc_log_plogfile, "==============================================================\n");
}

/* setupPVMonitors 
// Setup channel access pv monitors for throttle settings, one for each pv
*/
static int caStartMonitor(char *pvname, int *pvtype)
{
	int rc;
	int priority=80;
/*	int timeout=10; */
	chid chanid;
	char timestamp[ASCII_TIME_SIZE];

	getTimestamp(timestamp, sizeof(timestamp));


printf("caStartMonitor()\n");
	/* Create CA connections */
	rc = ca_create_channel(pvname, (caCh*)caConnectionHandler, pvtype, priority, &chanid);  
	if (rc != ECA_NORMAL) {
		printf("ERROR ca_create_channel(): rc=%d, msg='%s', pvname=%s\n", rc, ca_message(rc), pvname);
		fprintf(ioc_log_plogfile, "%s: ERROR ca_create_channel(): rc=%d, msg='%s', pvname=%s\n", timestamp, rc, ca_message(rc), pvname);
		return IOCLS_ERROR;
	}
	printf("ca_create_channel(): rc=%d, msg='%s', pvname=%s\n", rc, ca_message(rc), pvname);
	printf("pvname=%s, pvtype=%p\n", pvname, pvtype);
	fprintf(ioc_log_plogfile, "%s: ca_create_channel(): rc=%d, msg='%s', pvname=%s\n", timestamp, rc, ca_message(rc), pvname);
	fprintf(ioc_log_plogfile, "%s: pvname=%s, pvtype=%p\n", timestamp, pvname, pvtype);

	/* Check for channels that didn't connect */
/*    ca_pend_event(timeout); 
	rc = ca_pend_io(timeout);
	printf("ca_pend_io(): rc=%d, msg='%s', pvname=%s\n", rc, ca_message(rc), pvname);
*/
	rc = ca_state(chanid);
	printf("ca_state(): rc=%d, msg='%s', pvname=%s\n", rc, ca_message(rc), pvname);

	return IOCLS_OK;
}


/* start channel access */
static int caStartChannelAccess()
{
	int rc = IOCLS_OK;
	char timestamp[ASCII_TIME_SIZE];

	getTimestamp(timestamp, sizeof(timestamp));


	/* Start up Channel Access */
	printf("caStartChannelAccess()\n");
/*	rc = ca_context_create(ca_disable_preemptive_callback); */
	rc = ca_context_create(ca_enable_preemptive_callback); 
	if (rc != ECA_NORMAL) {
		fprintf(stderr, "ERROR ca_context_create(): rc=%d, msg='%s'\n", rc, ca_message(rc));
		fprintf(ioc_log_plogfile, "%s: ERROR ca_context_create(): rc=%d, msg='%s'\n", timestamp, rc, ca_message(rc));
		return IOCLS_ERROR;
    } else {
		rc = IOCLS_OK;
	}
	fprintf(stderr, "ca_context_create(): rc=%d, msg='%s'\n", rc, ca_message(rc));
	fprintf(ioc_log_plogfile, "%s: ca_context_create(): rc=%d, msg='%s'\n", timestamp, rc, ca_message(rc));	

	return rc;
}


// reconnect to Oracle
// only attempt to connect if successful disconnect, though looks like db_disconnect always returns success
static int dbReconnect()
{
	char timestamp[ASCII_TIME_SIZE];

	// first try to disconnect
	getTimestamp(timestamp, sizeof(timestamp));
	if (db_disconnect()) {
		fprintf(ioc_log_plogfile, "%s: Disconnected from Oracle\n", timestamp);
		fprintf(ioc_log_pverbosefile, "%s: Disconnected from Oracle\n", timestamp);
		// try to connect
		if (!db_connect(ioc_log_database)) {		
			getTimestamp(timestamp, sizeof(timestamp));
			fprintf(ioc_log_pverbosefile, "%s: Error connecting to Oracle\n", timestamp);
			fprintf(ioc_log_plogfile, "%s: ERROR connecting to %s!\n", timestamp, ioc_log_database);
			return IOCLS_ERROR;
		}
		fprintf(stderr, "connected to %s!\n", ioc_log_database);
		getTimestamp(timestamp, sizeof(timestamp));
		fprintf(ioc_log_plogfile, "%s: Successfully connected to %s\n", timestamp, ioc_log_database);
	} else {  // error disconnecting, though looks like db_disconnect always returns success
		fprintf(ioc_log_plogfile, "%s: ERROR disconnecting from Oracle\n", timestamp);
		fprintf(ioc_log_pverbosefile, "%s: ERROR disconnecting from Oracle\n", timestamp);
		return IOCLS_ERROR;
	}	

	return IOCLS_OK;
}

/* getThrottleString
// Concatenates valid columns to create string for database to use as constraint
// #define THROTTLE_PROGRAM     1 << 0    // 1 
// #define THROTTLE_MSG         1 << 1    // 2 
// #define THROTTLE_FACILITY    1 << 2    // 4 
// #define THROTTLE_SEVERITY    1 << 3    // 8 
// #define THROTTLE_CODE        1 << 4    // 16 
// #define THROTTLE_HOST        1 << 5    // 32 
// #define THROTTLE_USER        1 << 6    // 64 
// #define THROTTLE_STATUS      1 << 7    // 128 
// #define THROTTLE_PROCESS     1 << 8    // 256 */
static void getThrottleString(char *msg, char *facility, char *severity, char *code, char *host, char *user, char *status, char *process, char *throttleString, int throttleStringMask)
{
/*	printf("throttle mask:\n msg=%d\n facility=%d\n severity=%d\n error=%d\n host=%d\n user=%d\n status=%d\n process=%d\n", 
			THROTTLE_MSG, THROTTLE_FACILITY, THROTTLE_SEVERITY, THROTTLE_ERROR, THROTTLE_HOST, THROTTLE_USER, THROTTLE_STATUS, THROTTLE_PROCESS);
*/

	memset(throttleString, '\0', THROTTLE_MSG_SIZE);

	fprintf(ioc_log_pverbosefile, " throttleMask=%d, ", throttleStringMask);

	if (throttleStringMask & THROTTLE_PROGRAM) { strcpy(throttleString, ioc_log_programName); }
	if (throttleStringMask & THROTTLE_MSG) { strcat(throttleString, msg); }
	if (throttleStringMask & THROTTLE_FACILITY) { strcat(throttleString, facility); }
	if (throttleStringMask & THROTTLE_SEVERITY) { strcat(throttleString, severity); } 
	if (throttleStringMask & THROTTLE_CODE) { strcat(throttleString, code); }
	if (throttleStringMask & THROTTLE_HOST) { strcat(throttleString, host); } 
	if (throttleStringMask & THROTTLE_USER) { strcat(throttleString, user); } 
	if (throttleStringMask & THROTTLE_STATUS) { strcat(throttleString, status); }
	if (throttleStringMask & THROTTLE_PROCESS) { strcat(throttleString, process); }

	fprintf(ioc_log_pverbosefile, " throttleString='%s'\n", throttleString);
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

	fprintf(ioc_log_pverbosefile,"getThrottleTimestamp appTimestamp=%s\n", appTimestamp);

        if (appTimestamp == NULL || strcmp(appTimestamp, "") == 0) {
            fprintf(ioc_log_pverbosefile, "Skipping getThrottleTimestamp -> appTimestamp is empty or NULL");
            return;
        }

	/* if throttle seconds 0 or negative, set throttle timestamp to app timestamp */
	if (throttleSeconds <= 0 ) {
		strcpy(throttleTimestamp, appTimestamp);
		fprintf(ioc_log_plogfile, "Invalid ioc_log_throttleSeconds=%d, throttle with app timestamp\n", ioc_log_throttleSeconds);
		return;
	}

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
	strftime(throttleTimestamp, ASCII_TIME_SIZE, "%d-%b-%Y %H:%M:%S", &time);	
	fprintf(ioc_log_pverbosefile," throttleSeconds=%d, appTimestamp='%s', throttleTimestamp='%s'\n", throttleSeconds, appTimestamp, throttleTimestamp);
}

/*
 * parseMessages()
 * parse iocLogClient recvbuff and process messages
 * 1) Copy n complete messages into buffer
 * 2) tokenizer buffer to separate individual message
 * 3) parse individual message
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
	char facility[FACILITY_SIZE];
	char host[HOSTNODE_SIZE];
	char process[PROCESS_SIZE];
	char user[USER_NAME_SIZE];
	char text[MSG_SIZE];
	int appTimeDef = 0;
	int count=1;
	int commit=1;
	char throttleString[THROTTLE_MSG_SIZE];
	char logtimestamp[ASCII_TIME_SIZE];
	char program[PROGRAM_SIZE];  // optional program tag, overrides ioc_log_programName on a per message basis
	struct timeval currentTime;
	float elapsedMinutes=0;

	end = pclient->nChar;
	prevpch = pclient->recvbuf;
	index=0;

	// Get only complete messages, ignore last partial message
	/* find last carriage return */
	strncpy(buff, pclient->recvbuf, pclient->nChar);
	for (i=0; i<pclient->nChar; i++) {
		if (buff[i] == '\n') { /* found complete message */
			lastCarriageReturnIndex=i;
			//printf("real onerow buff=%s\n", buff); 
		} else if (strncmp(&buff[i-3], "\\n", 3) == 0) { /* weird \\n carriage return from iocsh, recognize it as a carriage return */
			lastCarriageReturnIndex=i-2;			
			buff[i-2]='\n';
			//printf("onerow buff=%s\n", buff); 
		}
	}

	// copy only complete messages into buffer
	memset(buff, '\0', sizeof(buff));
	strncpy(buff, pclient->recvbuf, lastCarriageReturnIndex);

	/* tokenize temp buffer to get individual messages */
	pch = strtok(buff, "\n");
	while (pch != NULL) {
		// start handling single message
		fprintf(ioc_log_pverbosefile, "\n-------------------------\n");
		getTimestamp(logtimestamp, sizeof(logtimestamp));	
		fprintf(ioc_log_pverbosefile, "%s: process single message\n", logtimestamp);

		strcpy(onerow, pch);
		isComplete=0;

		rowlen = strlen(onerow);

		/* get message attributes */
		ncharStripped = parseTags(nchar, pclient->name, onerow, appTime, status, severity, facility, host, code, process, user, &appTimeDef, program);

		//printf("ncharStripped=%d\n", ncharStripped);

        /* set the program tag for messages having a facility tag
           starting with "DpSlc" (VMS SLC Aida data provider messages)
           to FACET. */
        if (strncmp(facility, "DpSlc", 5) == 0) {
            strcpy(program, "FACET");
        }

		/* get message text */
		strncpy(text, &onerow[ncharStripped], rowlen-ncharStripped);
		text[rowlen-ncharStripped] = 0;

		getTimestamp(logtimestamp, sizeof(logtimestamp));	
		fprintf(ioc_log_pverbosefile, "%s: text='%s'\n", logtimestamp, text);
		//fprintf(ioc_log_pverbosefile, "rowlen=%d\n", rowlen);

		/* format throttling timestamp */
		getThrottleTimestamp(appTime, throttleTime, ioc_log_throttleSeconds);

		/* format throttling string */
		getThrottleString(text, facility, severity, code, host, user, status, process, throttleString, ioc_log_throttleFields);
		
		//printf("ioc_log_program_name=%s\n", ioc_log_program_name); 
		/* get logserver timestamp */
		getTimestamp(pclient->ascii_time, sizeof(pclient->ascii_time)); 
		//printf("client ascii_time=%s\n", pclient->ascii_time); 

		getTimestamp(logtimestamp, sizeof(logtimestamp));	
		fprintf(ioc_log_pverbosefile, "%s: START Oracle insert\n", logtimestamp);
		
		rc = db_insert(0, program, facility, severity, text, pclient->ascii_time, appTime, throttleTime, appTimeDef,
		               code, host, user, status, process, count, throttleString, commit); 

		getTimestamp(logtimestamp, sizeof(logtimestamp));	
		fprintf(ioc_log_pverbosefile, "%s: END Oracle insert\n", logtimestamp);

		if (rc == 1) {
			/* printf("%s\n", "SUCCESSFUL INSERT INTO ERRLOG TABLE"); */
			//fprintf(ioc_log_pverbosefile, "Successful insert from %s, %s\n", facility, host);
			//rc = fprintf(ioc_log_plogfile, "%s: SUCCESS inserting %s\n", pclient->ascii_time, onerow);  
			
			// close rawdata file if it's open
			if (ioc_log_prawdatafile != NULL) { 
				fprintf(ioc_log_plogfile, "%s: Closing %s, ioc_log_insertErrorStartTime=%ld\n", pclient->ascii_time, ioc_log_rawDataFileName, ioc_log_insertErrorStartTime.tv_sec);
				fprintf(ioc_log_pverbosefile, "%s: Closing %s\n", pclient->ascii_time, ioc_log_rawDataFileName);
				fclose(ioc_log_prawdatafile);
				ioc_log_prawdatafile = NULL;
			}
		} else {
			fprintf(ioc_log_pverbosefile, "ERROR INSERTING %s %s %s\n", facility, host, text);
			/* TODO: save entire message to data file for future processing */
			fprintf(ioc_log_pverbosefile, "unsuccessful row: '%s'\n", onerow);
			//rc = fprintf(ioc_log_plogfile, "%s: ERROR inserting %s\n", pclient->ascii_time, onerow);		
			writeToRawDataFile(appTime, program, facility, severity, code, host, user, status, process, text, appTimeDef);
			// get elapsed time
			gettimeofday(&currentTime, NULL);  
			elapsedMinutes = (currentTime.tv_sec - ioc_log_insertErrorStartTime.tv_sec)/60;
			// try reconnecting to db if elapsed time too long
			// since start time is initialized when program starts or last error occurred, this will immediately try to reconnect right after first error, then wait elapsed minutes
			if (elapsedMinutes > INSERT_ERROR_RECONNECT_MINUTES) {
				getTimestamp(logtimestamp, sizeof(logtimestamp));	
				fprintf(ioc_log_plogfile, "%s: Try reconnecting to %s, elapsedMinutes=%g\n", pclient->ascii_time, ioc_log_database, elapsedMinutes);
				fprintf(ioc_log_pverbosefile, "%s: Try reconnecting to %s, elapsedMinutes=%g\n", pclient->ascii_time, ioc_log_database, elapsedMinutes);
				dbReconnect();
				// reset error start time
				gettimeofday(&ioc_log_insertErrorStartTime, NULL);  
				getTimestamp(logtimestamp, sizeof(logtimestamp));
				fprintf(ioc_log_plogfile, "%s: Reset ioc_log_insertErrorStartTime=%ld\n", pclient->ascii_time, ioc_log_insertErrorStartTime.tv_sec);
				fprintf(ioc_log_pverbosefile, "%s: Reset ioc_log_insertErrorStartTime=%ld\n", pclient->ascii_time, ioc_log_insertErrorStartTime.tv_sec);
			}
		}


		pch = strtok(NULL, "\n");
		memset(onerow, '\0', THROTTLE_MSG_SIZE);
	}

	fprintf(ioc_log_pverbosefile, "  DONE PARSING RECVBUF \n\n");

	// clean up recvbuf 
	// check if still have partial message remaining at end of recvbuff
	// printf("lastCarriageReturnIndex=%d\n, pclient->nChar=%d\n",lastCarriageReturnIndex, pclient->nChar); 	
	lastMsgLen= pclient->nChar - lastCarriageReturnIndex - 1;
	/* handle truncated message */
	/* make sure this is a truncated message with no "\n" and not just end of clean recvbuf */
	if (lastMsgLen > 0) { /* shift partial message to front of recvbuf */	
		//printf("\n\npartial message remaining, lastCarriageReturnIndex=%d, nChar=%d \n", lastCarriageReturnIndex, pclient->nChar);
		//printf("  PARTIAL message='%s'\n",&pclient->recvbuf[lastCarriageReturnIndex]);
		memmove(pclient->recvbuf, &pclient->recvbuf[lastCarriageReturnIndex], lastMsgLen+1);  /* move incomplete entry to beginning */
		memset(&pclient->recvbuf[lastMsgLen+1], '\0', sizeof(pclient->recvbuf)-(lastMsgLen+1)); /* clear rest of buffer */
		pclient->nChar=lastMsgLen+1;
	} else { /* clear entire recbuf, no partial message remains */
		memset(pclient->recvbuf, '\0', sizeof(pclient->recvbuf));
		pclient->nChar=0;
	}

}


/* checks if there is another tag within this individual message */
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
	
	/* a tag found was found */
	/* check the tag is beginning of msg or not part of text, e.g. delta_time=" */
	if (found != NULL) rc=1;

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
//               Only parsing single message 
*/
static int parseTags(int nchar, char *hostIn, char *text, char *timeApp,  char *status, char *severity, 
                     char *facility, char *host, char *code, char *process, char *user, int *timeDef, char *program) {
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
	char timestamp[ASCII_TIME_SIZE];
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
	//memset(program, '\0', sizeof(program));
	strcpy(program, ioc_log_programName); // initialize program to default
	*timeDef=0;

	strcpy(facility, "IOC");
	maxTagLen = NAME_SIZE;    /* default maxTagLen to NAME_SIZE */

	fprintf(ioc_log_pverbosefile, "PARSE TAGS\n");
	fprintf(ioc_log_pverbosefile, "charPtr=%s\n", charPtr);

	/* start parsing message */
	while (charPtr) {
		tagPtr  = strchr(charPtr, '='); /* look for a valid tag */
		if (tagPtr) {
			nextTagPtr = strchr(tagPtr+1, '='); /* look for next tag */
			if (nextTagPtr) {
				/* make sure this is a tag and not a "=" within the message text */
				rc = hasNextTag(nextTagPtr, tmpPtr);					
				if (rc == 0) { /* no next tag, the "=" is within the message text */
					fprintf(ioc_log_pverbosefile, "no next tag for %s\n", nextTagPtr);
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
			else if ( !strncmp(charPtr,"program=",8)) { valPtr = program; maxTagLen = PROGRAM_SIZE; strcpy(thisTag,"program"); }
			else if ((!strncmp(charPtr,"time=",5)) && (charPtr[7]  == '-') && (charPtr[11] == '-') && (charPtr[16] == ' ') 
			                                       && (charPtr[19] == ':') && (charPtr[22] == ':')) {
				/* time=14-Feb-2012 14:24:02.09 */
				valPtr = timeApp; strcpy(thisTag,"time");
				maxTagLen = ASCII_TIME_SIZE;					
				/* there is a space in the time field (only field that allows a space before msg text), reset lastPtr just in case this is the last tag before msg text */
				lastPtr = strchr(tagPtr, ' ') + 1;
				lastPtr = strchr(lastPtr, ' '); 
			} else valPtr = 0;

			/* get value of the tag */
			if (valPtr) {
				charsize = lastPtr - tagPtr;
                                if (charsize < 1) {
                                    return 0;
                                }
				if (charsize > maxTagLen) charsize = maxTagLen;
				tagPtr++;
				strncpy(valPtr, tagPtr, charsize);
				valPtr[charsize-1] = 0;
				fprintf(ioc_log_pverbosefile, "Tag='%s', Value='%s'\n", thisTag, valPtr);
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
		fprintf(ioc_log_pverbosefile, "Host='%s'\n", host);		
	}
	// prepend "SLC" to anything coming from mcc host 
/*	if ((strncmp(host, "MCC", 3)==0) || (strncmp(host, "mcc.slac.stanford.edu", 21)==0)) {
		printf("current facility=%s\n", facility);
		strcpy(buff, "SLC-");
		strcat(buff, facility);
		strcpy(facility, buff);
		printf("new facility=%s\n", facility);
	}		
*/
	/* check if timestamp passed in */
	if (strlen(timeApp) == 0) { /* no timestamp defined, set default to logserver current time */
		getTimestamp(timestamp, ASCII_TIME_SIZE); 
/*		strcpy(timeApp, timeIn); */
		strcpy(timeApp, timestamp); 
		*timeDef = 1;
		fprintf(ioc_log_pverbosefile, "no timestamp defined, use log server's, set timeDef=1\n");
	} else {
		*timeDef = 0;
		fprintf(ioc_log_pverbosefile, "app timestamp defined, set timeDef=0\n");
		/* start TESTING 
		printf("ERROR!!!  CHANGE BACK TO *timeDef=0\n");
		*timeDef=1; strcpy(timeApp, timeIn); // always copy logserver_timestamp to app_timestamp for TESTING, delete when finished TESTING
		 END TESTING */
	}

	if (strstr(user, "blah") != NULL) {
		fprintf(ioc_log_plogfile, "user=%s, fac=%s\n", user, facility);
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

/* get current date in format YYYYMMDD */
static void getDate(char *datestr, int len)
{
	struct timeval tv;
	struct tm* ptm;

	memset(datestr, '\0', len);
	
	/* get time */
	gettimeofday(&tv, NULL);
	ptm = localtime(&tv.tv_sec);

	/* format to seconds */
	strftime(datestr, len, "%Y%m%d", ptm);
}

/* get current timestamp in format 14-Feb-2012 14:24:02.09 */
static void getTimestamp(char *timestamp, int len) 
{
	struct timeval tv;
	struct tm* ptm;
	long milliseconds;
	char milli[10];

	memset(timestamp, '\0', len);
	
	/* get time */
	gettimeofday(&tv, NULL);
	ptm = localtime(&tv.tv_sec);

	/* format to seconds */
	strftime(timestamp, len, "%d-%b-%Y %H:%M:%S", ptm);
/*	strftime(throttlingTimestamp, NAME_SIZE, "%d-%b-%Y %H:%M:%S", &time);	*/

	/* format year */
/*	strftime(year, 10, "%Y", ptm); */

	/* compute milliseconds */
	milliseconds = tv.tv_usec / 1000;

	/* add milliseconds */
	sprintf(milli, ".%04ld", milliseconds);
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
/*	struct ioc_log_server	*pserver = (struct ioc_log_server *)pParam; 
	int			status;
*/
	char			buff[256];


	/*
	 * Read and discard message from pipe.
	 */
	(void) read(sighupPipe[0], buff, sizeof buff);

	/*
	 * Determine new log file name.
	 */
/*	status = getDirectory();
	if (status<0) {
		fprintf(stderr, "iocLogServer: failed to determine new log file name\n");
		return;
	}
*/
	/*
	* If it's changed, open the new file.
	*/
/*	if (strlen(ioc_log_fileName)) {
		if (strcmp(ioc_log_fileName, pserver->outfile) == 0) {
			fprintf(stderr,	"iocLogServer: log file name unchanged; not re-opened\n");
		} else {
			status = openLogFileOld(pserver); 
			if (status<0) {
				fprintf(stderr, "File access problems to `%s' because `%s'\n", ioc_log_fileName, strerror(errno));
				strcpy(ioc_log_fileName, pserver->outfile);
				status = openLogFileOld(pserver);
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
*/
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
