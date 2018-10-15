/* MessageLogging_TestStandHello.cpp 
// Just used for stress testing iocLogMsgServer and Oracle db, not part of log server code 
// Invoked by running with iocLogMsgServer command line arguments testdir=<directory name> OR testrows=<number of dummy rows>
*/

#include "MessageLoggerTest.h"
#include <stdio.h>

#include <epicsExport.h>
#include <iocsh.h>
#include <errlog.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/time.h>

#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <ctime>

using namespace std;

// replace all occurences of token in stringin with replaceToken
string doStringReplace(string stringin, string token, string replaceToken) {
	// put space in between two tabs, so tokenizer finds all 10 columns
    size_t start_pos = 0;
	string stringout = stringin;
    while((start_pos = stringout.find(token, start_pos)) != std::string::npos) {
       	stringout.replace(start_pos, token.length(), replaceToken);
        start_pos += replaceToken.length()-1; // In case 'to' contains 'from', like replacing 'x' with 'yx'
    }

	return stringout;
}

void parseLine(string line)
{
	string facility, severity, text, timestamp, code, host, user, status, process, serial;
	string msg;
	
	// put space in between two tabs, so tokenizer finds all 10 columns
/*    size_t start_pos = 0;
	string token = "\t\t";
	string replaceToken = "\t99\t";
    while((start_pos = line.find(token, start_pos)) != std::string::npos) {
       	line.replace(start_pos, token.length(), replaceToken);
        start_pos += replaceToken.length()-1; // In case 'to' contains 'from', like replacing 'x' with 'yx'
    }
*/
	line = doStringReplace(line, "\t\t", "\tUNDEFINED\t");
//	printf("replaced double tabs\n");
//	printf("line='%s'\n", line.c_str());

	// tokenize the line
	char *pch;			
	char theline[2000];
	strcpy(theline, line.c_str());
	pch = strtok(theline,"\t");
	facility = pch;
	if (facility!="UNDEFINED") msg += "fac=" + facility;
//	printf("fac='%s'\n", facility.c_str());

	if (pch != NULL) {
		pch = strtok(NULL, "\t");
		severity = pch;
		if (severity!="UNDEFINED") msg += " sevr=" + severity;
//		printf("sevr='%s'\n", severity.c_str());
	}

	if (pch != NULL) {
		pch = strtok(NULL, "\t");
		text = pch;
		if (text=="UNDEFINED") text="";
//		printf("text='%s'\n", text.c_str());
	}

	struct tm time;
	char timech[80];
	string timestr;
	if (pch != NULL) {
		pch = strtok(NULL, "\t");
		timestamp = pch;	
//		if (timestamp == "UNDEFINED") timestamp = "00:00:00 2000-01-01";
		if (timestamp!="UNDEFINED") {
			// reformat time
			// reformat time string, coming from cm-log as 09:26:38 2012-02-10 --> change to 14-Feb-2012 10:10:38.00
			// put string into time struct
			strncpy(timech, timestamp.c_str(), 80);
		//printf("timech=%s, len=%d\n", timech, strlen(timech));
			strptime(timech, "%H:%M:%S %Y-%m-%d", &time);
			strftime(timech, 80, "%d-%b-%Y %H:%M:%S", &time);	
//			printf("timestamp='%s', timech='%s'\n", timestamp.c_str(), timech);
			timestr = timech;	
			msg += " time=" + timestr;
		}
	}

	if (pch != NULL) {
		pch = strtok(NULL, "\t");
		code = pch;
		if (code!="UNDEFINED") msg += " code=" + code;
//		printf("code='%s'\n", code.c_str());
	}

	if (pch != NULL) {
		pch = strtok(NULL, "\t");
		host = pch;
		if (host!="UNDEFINED") msg += " host=" + host;
//		printf("host='%s'\n", host.c_str());
	}

	if (pch != NULL) {
		pch = strtok(NULL, "\t");
		user = pch;
		if (user!="UNDEFINED") msg += " user=" + user;
//		printf("user='%s'\n", user.c_str());
	}

	if (pch !=NULL) {
		pch = strtok(NULL, "\t");
		status = pch;
		if (status!="UNDEFINED") msg += " stat=" + status;
//		printf("stat='%s'\n", status.c_str());
	}

	if (pch !=NULL) {
		pch = strtok(NULL, "\t");
		process = pch;
		// use pid for process
//		if (process!="UNDEFINED") msg += " proc=" + process;
//		printf("proc='%s'\n", process.c_str());
	}
	
	if (pch != NULL) {
		pch = strtok(NULL, "\t");
		serial = pch;		
//		if (serial!="UNDEFINED") msg += " serial=" + serial;
//		printf("serial='%s'\n\n", serial.c_str());
	}

	// put pid into process field
	pid_t pid;
	pid = getpid();
	char pidstr[10];
	sprintf(pidstr, "%d", pid);
	process = pidstr;
	if (msg.length() == 0) msg = "proc="+process;
	else msg += " proc=" + process;
//	printf("proc='%s'\n", process.c_str());

	msg += " "+text+"\n";	
	//printf("message : '%s'\n", msg.c_str());
	//string msg = "stat="+status+" sevr="+severity+" fac="+facility+" host="+host+" code="+code+" proc="+process+" user="+user+" time="+timech+" "+text+"\n";				
	//errlogPrintf(msg.c_str());

	// insert into database instead of calling errlogPrintf()
	char cfac[41], csev[21], ctext[681], ctime[31], ccode[257], chost[41], cuser[41], cstatus[21], cprocess[41];
	memset(ctext,'\0',sizeof(ctext));
	strcpy(cfac, facility.c_str());
	strcpy(csev, severity.c_str());
	strcpy(ctime, timestr.c_str());
	strcpy(ccode, code.c_str());
	strcpy(chost, host.c_str());
	strcpy(cuser, user.c_str());
	strcpy(cstatus, status.c_str());
	strcpy(cprocess, process.c_str());
	if (text.length() > 680)
		strncpy(ctext, text.c_str(), 680);
	else 
		strcpy(ctext, text.c_str());
	
/*		rc = db_insert(0, ioc_log_program_name, system, severity, text, pclient->ascii_time, appTime, throttleTime, appTimeDef,
		               code, host, user, status, process, count, throttleString, commit); 
*/
	int rc = db_insert(0, "TEST", cfac, csev, ctext, ctime, ctime, ctime, 0, ccode, chost, cuser, cstatus, cprocess, 1.0, ctext, 1);
printf("ctext='%s'\n", ctext);

	globalCounter++;
	printf("(%d) ----------------------\n", globalCounter);
	
}

void parseLogFile(string filename)
{				
	// first check if directory and skip if it is
	DIR *pdir=NULL;
	pdir=opendir(filename.c_str());
	if(pdir != NULL) {
		printf("Skip directory, not a file %s...\n", filename.c_str());	
		return;
	} 

	// check if backup ~ file
	int found=-1;
	found = filename.find("~");
	if (found != -1) {
		printf("Skipping file %s\n", filename.c_str());
		closedir(pdir);
		return;		
	}	

	// process file
	string strline="";
  	ifstream myfile;
	myfile.open(filename.c_str());
	char line[700];	
	if (!myfile) { // error opening file
		printf("Error opening file %s\n", filename.c_str());
	} else {
		printf("Opened %s...\n", filename.c_str());
	    while (!myfile.eof()) {
   			getline(myfile, strline);
			if (strline.length() == 0) continue;
			parseLine(strline);
			// sleep 200=5000 messages/sec
			// missed messages are logged in /nfs/slac/g/lcls/cmlog/iocLogServerTestiocLogServer.log as "messages were discarded"
			//usleep(1000); // 1000 msg/sec
  		}	
		myfile.close();
	}
}

/* This is the command, which the vxWorks shell will call directly */
//void hello(const char *directory) {
void parseMessagesTest(const char *directory) {
	// get time
	time_t rawtime;
	struct tm *timeinfo; 
	string startTimestamp;
	string endTimestamp;
	struct timeval startm, endm;
	long mtime, seconds, useconds;
	double rowrate;    

	globalCounter=0;

	time(&rawtime);
	timeinfo = localtime(&rawtime);
	startTimestamp = asctime(timeinfo);
	printf("Start time = %s", startTimestamp.c_str());
	gettimeofday(&startm, NULL);

	string msg = "fac=TIMESTAMP sevr=START START "+startTimestamp;
//	errlogPrintf(msg.c_str());

	string thedir = "";
//	string thedir = "/afs/slac/g/lcls/epics/iocTop/MessageLogging_TestStand/iocBoot/siocMessageLogging_TestStand/";
    if (directory) {
		thedir += directory;
	}
	printf("Searching directory %s...\n", directory);
	
	// search files in directory
	DIR *pdir=NULL;
	struct dirent *de=NULL;

	pdir=opendir(thedir.c_str());
	if(pdir == NULL) {
		printf("Couldn't open directory");	
		return;
	} 

	// Loop while not NULL
	string file;
	int pos;
	while(de=readdir(pdir)) {
		//printf("%s\n",de->d_name);
		file = de->d_name;
		// ignore files starting with "."
		if (file.find_first_of(".") == 0) continue;
		// prepend directory
		file = thedir + "/" + file;
		//printf("file = %s\n", file.c_str());
		parseLogFile(file);
	}
  
	closedir(pdir);

	time(&rawtime);
	timeinfo = localtime(&rawtime);
	endTimestamp = asctime(timeinfo);
	gettimeofday(&endm, NULL);

	seconds  = endm.tv_sec  - startm.tv_sec;
    useconds = endm.tv_usec - startm.tv_usec;
    mtime = ((seconds) * 1000 + useconds/1000.0); // + 0.5;
	if (mtime == 0) mtime=1;
	rowrate = (globalCounter*1000)/(mtime);

	printf("\n\nStart time: %sEnd time: %s", startTimestamp.c_str(), endTimestamp.c_str());
	printf("Total Process Time: %ld seconds (%ld milliseconds)\n", seconds, mtime);
	printf("Total Rows: %d rows\n", globalCounter);
	printf("Rows/Second: %f rows/sec\n", rowrate);


//	msg = "fac=TIMESTAMP sevr=END END="+endTimestamp;
//	errlogPrintf(msg.c_str());

}

void sendMessagesTest(int nrows) {
	// get time
	time_t rawtime;
	struct tm *timeinfo; 
	string startTimestamp;
	string endTimestamp;
	struct timeval startm, endm;
	long mtime, seconds, useconds;
	double rowrate;    
	char timestamp[31];

	globalCounter=0;

	time(&rawtime);
	timeinfo = localtime(&rawtime);
	startTimestamp = asctime(timeinfo);
	printf("Start time = %s", startTimestamp.c_str());
	gettimeofday(&startm, NULL);

//	14-Feb-2012 10:10:38.00
	strcpy(timestamp, "14-Feb-2012 10:10:38.00");

	for (int i=0; i<nrows; i++) {
//		db_insert(0, "TEST", "cfac", "csev", "ctext", timestamp, timestamp, timestamp, 1, "ccode", "chost", "cuser", "cstatus", "cprocess", 1.0, "ctext", 1);
		db_insert(0, "TEST", "cfac", "csev", "", "", "", "", 0, "", "", "", "", "", 1.0, "", 1);
	}			
	globalCounter=nrows;

	time(&rawtime);
	timeinfo = localtime(&rawtime);
	endTimestamp = asctime(timeinfo);
	gettimeofday(&endm, NULL);

	seconds  = endm.tv_sec  - startm.tv_sec;
    useconds = endm.tv_usec - startm.tv_usec;
    mtime = ((seconds) * 1000 + useconds/1000.0); // + 0.5;
	if (mtime == 0) mtime=1;
	rowrate = (globalCounter*1000)/(mtime);

	printf("\n\nStart time: %sEnd time: %s", startTimestamp.c_str(), endTimestamp.c_str());
	printf("Total Process Time: %ld seconds (%ld milliseconds)\n", seconds, mtime);
	printf("Total Rows: %d rows\n", globalCounter);
	printf("Rows/Second: %f rows/sec\n", rowrate);


//	msg = "fac=TIMESTAMP sevr=END END="+endTimestamp;
//	errlogPrintf(msg.c_str());

}
