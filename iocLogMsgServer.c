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

#include	"dbDefs.h"
#include	"epicsAssert.h"
#include 	"fdmgr.h"
#include 	"envDefs.h"
#include 	"osiSock.h"
#include	"epicsStdio.h"

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
    int  logserver_id,
    char facility_name[41],
    char severity[21],
    char msg[681],
/*    char logServer_ascii_time[30],
    char app_ascii_time[30], */
    char logServer_ascii_time[32],
    char app_ascii_time[3], 
    int  app_timestamp_def,
    int  code,
    char  hostnode[41],
    char  user_name[41],
    int  status,
    char  process_name[41],
    long  msg_count,
    int   commit_flag
    );

static unsigned short ioc_log_port;
static long ioc_log_file_limit;
static char ioc_log_file_name[256];
static char ioc_log_file_command[256];

#define MSG_SIZE 681
#define NAME_SIZE 32
#define USER_NAME_SIZE 41
#define FACILITY_SIZE 41
#define SEVERITY_SIZE 21
#define ASCII_TIME_SIZE 31
#define DESTINATION_SIZE 81
#define HOSTNODE_SIZE 41
#define PROCESS_SIZE 41

struct iocLogClient {
	int insock;
	struct ioc_log_server *pserver;
	size_t nChar;
	char recvbuf[1024];
	char name[NAME_SIZE];
	char ascii_time[NAME_SIZE];
};

struct ioc_log_server {
	char outfile[256];
	long filePos;
	FILE *poutfile;
	void *pfdctx;
	SOCKET sock;
	long max_file_size;
};

#define IOCLS_ERROR (-1)
#define IOCLS_OK 0

static void acceptNewClient (void *pParam);
static void readFromClient(void *pParam);
static void logTime (struct iocLogClient *pclient);
static int getConfig(void);
static int openLogFile(struct ioc_log_server *pserver);
static void handleLogFileError(void);
static void envFailureNotify(const ENV_PARAM *pparam);
static void freeLogClient(struct iocLogClient *pclient);
static void writeMessagesToLog (struct iocLogClient *pclient);
/*static int stripTags(int nchar, char *hostIn, char *timeIn, char *text,
                     char *timeOut,
                     char *msgStatus, char *msgSeverity,
                     char *system,    char *host, 
                     char *msgErrCode, char *process, char *user, int *timeDef);
*/
static int stripTags(int nchar, char *hostIn, char *timeIn, char *text, char *timeOut, int *status, char *msgSeverity, 
                     char *system, char *host, int *errCode, char *process, char *user, int *timeDef);
static int convertClientTime(char *timeIn, char *timeOut); 

int isNumeric (const char * s);

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
int main(void)
{
        int msgNum = 0;
	struct sockaddr_in serverAddr;	/* server's address */
	struct timeval timeout;
	int status;
	struct ioc_log_server *pserver;

	osiSockIoctl_t	optval;

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
	status = bind (	pserver->sock, 
			(struct sockaddr *)&serverAddr, 
			sizeof (serverAddr) );
	if (status<0) {
        char sockErrBuf[64];
        epicsSocketConvertErrnoToString ( sockErrBuf, sizeof ( sockErrBuf ) );
		fprintf(stderr, "iocLogServer: bind err: %s\n", sockErrBuf );
		fprintf (stderr,
			"iocLogServer: a server is already installed on port %u?\n", 
			(unsigned)ioc_log_port);
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
	if (status<0) {
		fprintf(stderr,
			"File access problems to `%s' because `%s'\n", 
			ioc_log_file_name,
			strerror(errno));
		return IOCLS_ERROR;
	}

	status = fdmgr_add_callback(
			pserver->pfdctx, 
			pserver->sock, 
			fdi_read,
			acceptNewClient,
			pserver);
	if(status<0){
		fprintf(stderr,
			"iocLogServer: failed to add read callback\n");
		return IOCLS_ERROR;
	}
        
	/*
	 * Initialize connection to msg_handler.
	 */

        /* JROCK Connect to Oracle */
        if ( !db_connect("MCCODEV") )
        {
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
	while(TRUE) {
		timeout.tv_sec = 60;            /*  1 min  */
		timeout.tv_usec = 0; 
		fdmgr_pend_event(pserver->pfdctx, &timeout); 
		if (pserver->poutfile) 
		  fflush(pserver->poutfile);
	}

      /* disconnect from  Oracle */
      if ( db_disconnect() )
        {
        fprintf(stderr, "%s\n", "iocLogMsgServer: Disconnected from Oracle\n");
	}

        /* RUN WITHOUT CONNECTION TO MSGSENDER
        msgSenderExit();
        */

	exit(0); /* should never reach here */
}

int isNumeric (const char * s)
{
    if (s == NULL || *s == '\0' || isspace(*s))
      return 0;
    char * p;
    strtod (s, &p);
    return *p == '\0';
}

/*
 * seekLatestLine (struct ioc_log_server *pserver)
 */
static int seekLatestLine (struct ioc_log_server *pserver)
{
    static const time_t invalidTime = (time_t) -1;
    time_t theLatestTime = invalidTime;
    long latestFilePos = -1;
    int status;

    /*
     * start at the beginning
     */
    rewind (pserver->poutfile);

    while (1) {
        struct tm theDate;
        int convertStatus;
        char month[16];

        /*
         * find the line in the file with the latest date
         *
         * this assumes ctime() produces dates of the form:
         * DayName MonthName dayNum 24hourHourNum:minNum:secNum yearNum
         */
        convertStatus = fscanf (
            pserver->poutfile, " %*s %*s %15s %d %d:%d:%d %d %*[^\n] ",
            month, &theDate.tm_mday, &theDate.tm_hour, 
            &theDate.tm_min, &theDate.tm_sec, &theDate.tm_year);
        if (convertStatus==6) {
            static const char *pMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
            static const unsigned nMonths = sizeof(pMonths)/sizeof(pMonths[0]);
            time_t lineTime = (time_t) -1;
            unsigned iMonth;

            for (iMonth=0; iMonth<nMonths; iMonth++) {
                if ( strcmp (pMonths[iMonth], month)==0 ) {
                    theDate.tm_mon = iMonth;
                    break;
                }
            }
            if (iMonth<nMonths) {
                static const int tm_epoch_year = 1900;
                if (theDate.tm_year>tm_epoch_year) {
                    theDate.tm_year -= tm_epoch_year;
                    theDate.tm_isdst = -1; /* dont know */
                    lineTime = mktime (&theDate);
                    if ( lineTime != invalidTime ) {
                        if (theLatestTime == invalidTime || 
                            difftime(lineTime, theLatestTime)>=0) {
                            latestFilePos =  ftell (pserver->poutfile);
                            theLatestTime = lineTime;
                        }
                    }
                    else {
                        char date[128];
                        size_t nChar;
                        nChar = strftime (date, sizeof(date), "%a %b %d %H:%M:%S %Y\n", &theDate);
                        if (nChar>0) {
                            fprintf (stderr, "iocLogServer: strange date in log file: %s\n", date);
                        }
                        else {
                            fprintf (stderr, "iocLogServer: strange date in log file\n");
                        }
                    }
                }
                else {
                    fprintf (stderr, "iocLogServer: strange year in log file: %d\n", theDate.tm_year);
                }
            }
            else {
                fprintf (stderr, "iocLogServer: strange month in log file: %s\n", month);
            }
        }
        else {
            char c = fgetc (pserver->poutfile);
 
            /*
             * bypass the line if it does not match the expected format
             */
            while ( c!=EOF && c!='\n' ) {
                c = fgetc (pserver->poutfile);
            }

            if (c==EOF) {
                break;
            }
        }
    }

    /*
     * move to the proper location in the file
     */
    if (latestFilePos != -1) {
	    status = fseek (pserver->poutfile, latestFilePos, SEEK_SET);
	    if (status!=IOCLS_OK) {
		    fclose (pserver->poutfile);
		    pserver->poutfile = stderr;
		    return IOCLS_ERROR;
	    }
    }
    else {
	    status = fseek (pserver->poutfile, 0L, SEEK_END);
	    if (status!=IOCLS_OK) {
		    fclose (pserver->poutfile);
		    pserver->poutfile = stderr;
		    return IOCLS_ERROR;
	    }
    }

    pserver->filePos = ftell (pserver->poutfile);

    if (theLatestTime==invalidTime) {
        if (pserver->filePos!=0) {
            fprintf (stderr, "iocLogServer: **** Warning ****\n");
            fprintf (stderr, "iocLogServer: no recognizable dates in \"%s\"\n", 
                ioc_log_file_name);
            fprintf (stderr, "iocLogServer: logging at end of file\n");
        }
    }

	return IOCLS_OK;
}


/*
 *	openLogFile()
 *
 */
static int openLogFile (struct ioc_log_server *pserver)
{
	enum TF_RETURN ret;

	if (ioc_log_file_limit==0u) {
		pserver->poutfile = stderr;
		return IOCLS_ERROR;
	}

	if (pserver->poutfile && pserver->poutfile != stderr){
		fclose (pserver->poutfile);
		pserver->poutfile = NULL;
	}
    if (strlen(ioc_log_file_name)) {
	pserver->poutfile = fopen(ioc_log_file_name, "r+");
	if (pserver->poutfile) {
		fclose (pserver->poutfile);
		pserver->poutfile = NULL;
		ret = truncateFile (ioc_log_file_name, ioc_log_file_limit);
		if (ret==TF_ERROR) {
			return IOCLS_ERROR;
		}
		pserver->poutfile = fopen(ioc_log_file_name, "r+");
	}
	else {
		pserver->poutfile = fopen(ioc_log_file_name, "w");
	}

	if (!pserver->poutfile) {
		pserver->poutfile = stderr;
		return IOCLS_ERROR;
	}
	strcpy (pserver->outfile, ioc_log_file_name);
	pserver->max_file_size = ioc_log_file_limit;
        return seekLatestLine (pserver);
    }
    return IOCLS_OK;
}


/*
 *	handleLogFileError()
 *
 */
static void handleLogFileError(void)
{
	fprintf(stderr,
		"iocLogServer: log file access problem (errno=%s)\n", 
		strerror(errno));

      /* disconnect from Oracle */
      if ( db_disconnect() )
        {
        fprintf(stderr, "%s\n", "iocLogMsgServer: Disconnected from Oracle\n");
	}
        /*  RUN WITHOUT CONNECTION TO MSGSENDER
        msgSenderExit();
        */

	exit(IOCLS_ERROR);
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

	logTime(pclient);
	
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
	// try clearing pclient->recvbuf
	memset(pclient->recvbuf, '\0', sizeof(pclient->recvbuf));

	logTime(pclient);

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
	writeMessagesToLog (pclient);
}

/*
 * writeMessagesToLog()
 */
static void writeMessagesToLog (struct iocLogClient *pclient)
{
	int logServer_id = 1;
	int status = 0;
    size_t lineIndex = 0;
    char msg2write[MSG_SIZE];
    int app_time_def = 0;
	
	while (TRUE) {
		size_t nchar;
		size_t nTotChar;
        size_t crIndex;

		int ntci;
		int ncharStripped;

        char ascii_time[NAME_SIZE];
        char app_ascii_time[NAME_SIZE];
//        char msgStatus[NAME_SIZE];
        int status;
/*        char msgErrCode[NAME_SIZE]; */
        int errCode;
        char msgSeverity[SEVERITY_SIZE]; 
        char system[FACILITY_SIZE];
        char timeOut[ASCII_TIME_SIZE];
        char host[HOSTNODE_SIZE];
        char process[PROCESS_SIZE];
        char user[USER_NAME_SIZE];
        char pclientTime[NAME_SIZE];
        char timeConverted;

		if ( lineIndex >= pclient->nChar ) {
			pclient->nChar = 0u;
			break;
		}

		/*
		 * find the first carrage return and create
		 * an entry in the log for the message associated
		 * with it. If a carrage return does not exist and 
		 * the buffer isnt full then move the partial message 
		 * to the front of the buffer and wait for a carrage 
		 * return to arrive. If the buffer is full and there
		 * is no carrage return then force the message out and 
		 * insert an artificial carrage return.
		 */
		nchar = pclient->nChar - lineIndex;
        for ( crIndex = lineIndex; crIndex < pclient->nChar; crIndex++ ) {
            if ( pclient->recvbuf[crIndex] == '\n' ) {
                /* printf ("====>MSG BUF IS *****%s*****\n", pclient->recvbuf); */
                break;
            }
        }
		if ( crIndex < pclient->nChar ) {
			nchar = crIndex - lineIndex;
        }
        else {
		    nchar = pclient->nChar - lineIndex;
			if ( nchar < sizeof ( pclient->recvbuf ) ) {
				if ( lineIndex != 0 ) {
					pclient->nChar = nchar;
					memmove ( pclient->recvbuf, 
                        & pclient->recvbuf[lineIndex], nchar);
				}
				break;
			}
		}

		/*
		 * reset the file pointer if we hit the end of the file
		 */
		nTotChar = strlen(pclient->name) +
				strlen(pclient->ascii_time) + nchar + 3u;
		assert (nTotChar <= INT_MAX);
		ntci = (int) nTotChar;

	      if (pclient->pserver->poutfile) {
		if ( pclient->pserver->filePos+ntci >= pclient->pserver->max_file_size ) {
			if ( pclient->pserver->max_file_size >= pclient->pserver->filePos ) {
				unsigned nPadChar;
				/*
				 * this gets rid of leftover junk at the end of the file
			 	 */
				nPadChar = pclient->pserver->max_file_size - pclient->pserver->filePos;
				while (nPadChar--) {
					status = putc ( ' ', pclient->pserver->poutfile );
					if ( status == EOF ) {
						handleLogFileError();
					}
				}
			}

#			ifdef DEBUG
				fprintf ( stderr,
					"ioc log server: resetting the file pointer\n" );
#			endif
			fflush ( pclient->pserver->poutfile );
			rewind ( pclient->pserver->poutfile );
			pclient->pserver->filePos = ftell ( pclient->pserver->poutfile );
		}
              }
              
	
              assert (nchar<INT_MAX);
              ncharStripped = stripTags(nchar, pclient->name, pclient->ascii_time, &pclient->recvbuf[lineIndex],
                app_ascii_time, &status, msgSeverity, system, host, &errCode, process, user, &app_time_def);

/*              ncharStripped = stripTags(
                nchar, pclient->name, pclient->ascii_time,
                &pclient->recvbuf[lineIndex],
                app_ascii_time, msgStatus, msgSeverity, system, host,
                msgErrCode, process, user, &app_time_def);

                if (isNumeric(msgErrCode))
                   { errCode = atoi(msgErrCode); }
                else
                   { errCode = 0; }
===============
TO DO:
convert msgStatus to status, msgErrCode to errCode
                int status;
                int errCode;
================
*/

/*
printf("%s\n", "===================================");
printf("%s\n", "TESTMESSAGECONTENTS:");
printf("*%s* *%s* *%s* *%s* *%s*\n", ascii_time, msgStatus, msgSeverity, system, host);
printf("%s\n", "===================================");
*/

/* JROCK TESTING - RUN WITHOUT CONNECTION TO MSGSENDER
              status = msgSenderWrite(
                ascii_time, msgStatus, msgSeverity, system, host,
                &pclient->recvbuf[lineIndex]+ncharStripped,
                (int) nchar-ncharStripped);
              if (status<0) handleLogFileError();
*/
              /* JROCK TEST: Oracle */
              /* set up data first */ 
	      /* dummy this up: app_time, orig_time are both 99999 */
              /* dummy this up: status is 1, error_code is 2, verbosity is 3 */
              /* set commit flag to 1.  May only want to commit every X rows tho... */
              strncpy(msg2write, &pclient->recvbuf[lineIndex]+ncharStripped, (int) nchar-ncharStripped);
              msg2write[(int) nchar-ncharStripped] = 0;

/*
              printf ("inserting *%s* *%s* *%s* *%s*\n",system, host, msg2write, msgSeverity); 
              printf("pclient time is %s\n", pclient->ascii_time);
*/
/*
              don't need this conversion: Oracle has to do it anyway
              convertClientTime(pclient->ascii_time, pclientTime);

*/
              if ( 
                 db_insert(0, 
                   logServer_id,
                   system,
                   msgSeverity,
                   msg2write,
                   pclient->ascii_time,  
                   app_ascii_time,
                   app_time_def,
                   errCode,  /* errCode */
                   host,
                   user,
                   status,   /* msgStatus */
                   process,  /* process */
                   1, /* msgCount */
                   1) /* commit flag */
                  ) 
                  {
                  /* printf("%s\n", "SUCCESSFUL INSERT INTO ERRLOG TABLE"); */
                  printf("%s %s %s\n", system,host,"msg2Oracle");
                  }
               else
                  {
                  printf("%s for message:  %s %s %s\n", "ERROR INSERTING INTO ERRLOG TABLE", system, host, msg2write);
                  }
                 
	      if (pclient->pserver->poutfile) {
		/*
		 * NOTE: !! change format string here then must
		 * change nTotChar calc above !!
		 */
		status = fprintf(
			pclient->pserver->poutfile,
			"%s %s %.*s\n",
			pclient->name,
			pclient->ascii_time,
			(int) nchar-ncharStripped,
			&pclient->recvbuf[lineIndex]+ncharStripped);
		if (status<0) {
			handleLogFileError();
		} else {
		    if (status != (ntci-ncharStripped)) {
			    fprintf(stderr, "iocLogServer: didnt calculate number of characters correctly?\n");
		    }
		    pclient->pserver->filePos += status;
                }
              }
              lineIndex += nchar+1u;
	}

/* JROCK TESTING - RUN WITHOUT CONNECTION TO MSGSENDER
        msgSenderFlush();
*/
}
static int convertClientTime(char *timeIn, 
                             char *timeOut)
{
    struct tm time_tm;
    int converted = 0;
    char convTime[NAME_SIZE];

    *convTime  = strptime(timeIn, "%a %b %d %T %Y", &time_tm);
    if (convTime) {
      strftime(convTime, NAME_SIZE, "%d-%b-%Y %T.00", &time_tm);
      converted = 1;
    } else {
      strcpy(convTime, "00-000-0000 00:00:00.00");
      converted = 0;
    }
    convTime[NAME_SIZE-1]=0;
    strcpy(timeOut, convTime);

    return converted;
}

/* the first 4 parameters are input from the pclient structure */
/* the others are the parsed tag strings */
/*static int stripTags(int nchar, char *hostIn, char *timeIn, char *text,
                     char *timeOut,
                     char *msgStatus, char *msgSeverity,
                     char *system,    char *host, 
                     char *msgErrCode, char *process, char *user, int *timeDef
                     ) {
*/
static int stripTags(int nchar, char *hostIn, char *timeIn, char *text, char *timeOut, int *status, char *msgSeverity, 
                     char *system, char *host, int *errCode, char *process, char *user, int *timeDef) {
	char * charPtr = text;
	char * tagPtr;
	char * lastPtr;
	char * valPtr;
	int    ncharStripped = 0;
	int    charsize;
//	int status;
//	int errCode;
	int maxTagLen;
	char msgStatus[NAME_SIZE];
	char msgErrCode[NAME_SIZE];

/*  Initialize outputs.
**  default is IOC
**  host defaults to the host in pclient
*/
//	msgStatus[0]   = 0;
	msgSeverity[0] = 0;
	strcpy(system, "IOC");
	host[0]        = 0;
	timeOut[0]     = 0;
	maxTagLen = NAME_SIZE;    /* default maxTagLen to NAME_SIZE */
	char thisTag[1000];
	memset(thisTag, '\0', 1000);
	memset(msgErrCode, '\0', strlen(msgErrCode));
	memset(msgStatus, '\0', strlen(msgStatus));
	*status=0;
	*errCode=0;

/**  Look for tags at the beginning of the text and strip them out.
**  Stop looking if there are no more tags or an unknown tag.
*/
  /* IF THERE ARE TAGGED VALUES WITH EMBEDDED SPACES, THIS WILL TRUNCATE THE TAG VAL AT
     THE FIRST SPACE - NEEDS FIXING.  INSTEAD, GO FOR THE LAST SPACE BEFORE THE "="
     PERHAPS STRTOK WOULD BE A BETTER PLAN.
     FOR NOW, DONT ALLOW EMBEDDED SPACES
  */
	while (charPtr) {
		tagPtr  = strchr(charPtr, '=');
		if (tagPtr) lastPtr = strchr(tagPtr, ' ');
		else lastPtr = 0;
/* ==============
TO DO:
make sure all tags are here!  document them.
reconcile with fwdClis, etc.
==============
*/
		/* find a tag */
		if (tagPtr && lastPtr) {
			if ( !strncmp(charPtr,"stat=",5)) { valPtr = msgStatus; maxTagLen = NAME_SIZE; strcpy(thisTag,"stat"); }
			else if ( !strncmp(charPtr,"sevr=",5)) { valPtr = msgSeverity; maxTagLen = SEVERITY_SIZE; strcpy(thisTag,"sevr"); }
			else if ( !strncmp(charPtr,"fac=" ,4)) { valPtr = system; maxTagLen = FACILITY_SIZE; strcpy(thisTag,"fac"); }
			else if ( !strncmp(charPtr,"facility=" ,9)) { valPtr = system; maxTagLen = FACILITY_SIZE; strcpy(thisTag,"facility"); }
			else if ( !strncmp(charPtr,"host=",5)) { valPtr = host; maxTagLen = HOSTNODE_SIZE; strcpy(thisTag,"host"); }
			else if ( !strncmp(charPtr,"code=",5)) { valPtr = msgErrCode; maxTagLen = NAME_SIZE; strcpy(thisTag,"code"); }
			else if ( !strncmp(charPtr,"proc=",5)) { valPtr = process; maxTagLen = PROCESS_SIZE; strcpy(thisTag,"proc"); }
			else if ( !strncmp(charPtr,"user=",5)) { valPtr = user; maxTagLen = USER_NAME_SIZE; strcpy(thisTag,"user"); }
			else if ((!strncmp(charPtr,"time=",5)) && (charPtr[7]  == '-') && (charPtr[11] == '-') && (charPtr[16] == ' ')) {
				valPtr = timeOut; strcpy(thisTag,"time");
				lastPtr = tagPtr + 24;
			} else valPtr = 0;

			/* get value of the tag */
			if (valPtr) {
				charsize = lastPtr - tagPtr;
/*        it's not always NAME_SIZE!  this check depends on the specific tag
        chop off the length to the max len allowed
        if (charsize > NAME_SIZE) charsize = NAME_SIZE;*/
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
	printf("Msg: %s\n", text); 

	/* now clean up data and handle defaults */
	/* no host defined, set default */
	if (strlen(host) == 0) { 	
/*   strncpy(host, hostIn, NAME_SIZE); */
		strncpy(host, hostIn, HOSTNODE_SIZE);
		host[NAME_SIZE-1]=0;
		/* Remove IP domain name */
		charPtr = strchr(host, '.');
		if (charPtr) *charPtr = 0;
	}

	/* no timestamp defined, set default to logserver current time */
	if (strlen(timeOut) == 0) { 
		struct tm time_tm;
		valPtr = strptime(timeIn, "%a %b %d %T %Y", &time_tm);
		if (valPtr) strftime(timeOut, NAME_SIZE, "%d-%b-%Y %T.00", &time_tm);
		else strcpy(timeOut, "00-000-0000 00:00:00.00");    	
		timeOut[NAME_SIZE-1]=0;
		*timeDef = 1;
		printf("no timestamp defined, use log server's, set timeDef=1\n");
	} else {
		*timeDef = 0;
		printf("set timeDef=0\n");
	}

	/* convert status and errCode to integers */
	if (isdigit(msgStatus[0])) {
		printf("msgStatus is numeric! msgStatus=%s\n", msgStatus);
		*status = atoi(msgStatus);
	} else {
		printf("msgStatus is NOT numeric! msgStatus=%s\n", msgStatus);
	}
	if (isdigit(msgErrCode[0])) {
		printf("msgErrCode is numeric! msgErrCode=%s\n", msgErrCode);
		*errCode = atoi(msgErrCode);
	} else {
		printf("msgErrCode is NOT numeric! msgErrCode=%s\n", msgErrCode);
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
		writeMessagesToLog (pclient);
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


/*
 *
 *	logTime()
 *
 */
static void logTime(struct iocLogClient *pclient)
{
	time_t		sec;
        struct tm       sec_tm;
	char		*pcr;

	sec = time (NULL);
        localtime_r(&sec, &sec_tm);
        strftime(pclient->ascii_time, sizeof (pclient->ascii_time),
                  "%a %b %d %T %Y", &sec_tm);
	pclient->ascii_time[sizeof(pclient->ascii_time)-1] = '\0';
	pcr = strchr(pclient->ascii_time, '\n');
	if (pcr) {
		*pcr = '\0';
	}
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
			&ioc_log_file_limit);
	if(status>=0){
		if (ioc_log_file_limit<=0) {
			envFailureNotify (&EPICS_IOC_LOG_FILE_LIMIT);
			return IOCLS_ERROR;
		}
	}
	else {
		ioc_log_file_limit = 10000;
	}

	pstring = envGetConfigParam(
			&EPICS_IOC_LOG_FILE_NAME, 
			sizeof ioc_log_file_name,
			ioc_log_file_name);

	/*
	 * its ok to not specify the IOC_LOG_FILE_COMMAND
	 */
	pstring = envGetConfigParam(
			&EPICS_IOC_LOG_FILE_COMMAND, 
			sizeof ioc_log_file_command,
			ioc_log_file_command);
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
		fprintf(stderr, "iocLogServer: failed to determine log file "
			"directory\n");
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
	if(status<0){
                fprintf(stderr,
                        "iocLogServer: failed to create pipe because `%s'\n",
                        strerror(errno));
                return IOCLS_ERROR;
        }

	status = fdmgr_add_callback(
			pserver->pfdctx, 
			sighupPipe[0], 
			fdi_read,
			serviceSighupRequest,
			pserver);
	if(status<0){
		fprintf(stderr,
			"iocLogServer: failed to add SIGHUP callback\n");
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
	if (status<0){
		fprintf(stderr, "iocLogServer: failed to determine new log "
			"file name\n");
		return;
	}

	/*
	 * If it's changed, open the new file.
	 */
      if (strlen(ioc_log_file_name)) {
	if (strcmp(ioc_log_file_name, pserver->outfile) == 0) {
		fprintf(stderr,
			"iocLogServer: log file name unchanged; not re-opened\n");
	}
	else {
		status = openLogFile(pserver);
		if(status<0){
			fprintf(stderr,
				"File access problems to `%s' because `%s'\n", 
				ioc_log_file_name,
				strerror(errno));
			strcpy(ioc_log_file_name, pserver->outfile);
			status = openLogFile(pserver);
			if(status<0){
				fprintf(stderr,
                                "File access problems to `%s' because `%s'\n",
                                ioc_log_file_name,
                                strerror(errno));
				return;
			}
			else {
				fprintf(stderr,
				"iocLogServer: re-opened old log file %s\n",
				ioc_log_file_name);
			}
		}
		else {
			fprintf(stderr,
				"iocLogServer: opened new log file %s\n",
				ioc_log_file_name);
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

	if ((ioc_log_file_command[0] != '\0') &&
            (ioc_log_file_name[0]    != '\0')) {

		/*
		 * Use popen() to execute command and grab output.
		 */
		pipe = popen(ioc_log_file_command, "r");
		if (pipe == NULL) {
			fprintf(stderr,
				"Problem executing `%s' because `%s'\n", 
				ioc_log_file_command,
				strerror(errno));
			return IOCLS_ERROR;
		}
		if (fgets(dir, sizeof(dir), pipe) == NULL) {
			fprintf(stderr,
				"Problem reading o/p from `%s' because `%s'\n", 
				ioc_log_file_command,
				strerror(errno));
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
			char *name = ioc_log_file_name;
			char *slash = strrchr(ioc_log_file_name, '/');
			char temp[256];

			if (slash != NULL) name = slash + 1;
			strcpy(temp,name);
			sprintf(ioc_log_file_name,"%s/%s",dir,temp);
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
