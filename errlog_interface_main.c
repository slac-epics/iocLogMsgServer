#include <stdio.h>
#include <string.h>

#define PATH_MAX 10000

FILE *fp;
char var1[100];
long  cnt;
int status;
char path[PATH_MAX];

extern int db_connect(char server[31]);
extern int db_disconnect();
extern int db_commit();
extern int db_insert(long app_time,
                     long orig_time,
                     char hostnode[41],
                     char msg[321],
                     char  severity[21],
                     char  destination[81],
                     long  status,
                     long  error_code,
                     long  verbosity,
                     char  process_name[41],
                     char  facility_name[41],
                     int   commit_flag);


main(int argc, char *argv[])
{
  if ( argc == 1 )
  {
    printf("%s\n", "No Parms: Enter 1 to do commit after each row or 0 to do commit after many rows.");
    exit(1);
  }

  int n;
  int loopcnt;

  int commit_flag = atoi( argv[1] );


  if ( !db_connect("SLACDEV") )
  {
    printf("%s\n", "Error connecting to Oracle");
    exit(1);
  }

  printf("%s\n", "Connected to Oracle");


  commit_flag = 1;
  loopcnt = 1000;

  for (n = 1; n < loopcnt; n++)
  {
    if ( db_insert(100+n,
                   100+n,
                   "hostnode",
                   "msg",
                   "severity",
                   "destination",
                   n,
                   n,
                   n,
                   "process_name",
                   "facility_name",
                   commit_flag)
       )
    {
      printf("FOR ROW %d: %s\n", n, "SUCCESSFUL INSERT INTO ERRLOG TABLE");
    }
    else
    {
      printf("FOR ROW %d: %s\n", n, "ERROR INSERTING INTO ERRLOG TABLE");
    }
  }

  if ( !commit_flag )
  {
    if ( db_commit() )
    {
      printf("COMMITTED %d ROWS\n", n-1);
    }
  }



  if ( db_disconnect() )
  {
    printf("%s\n", "DISCONNECTED FROM ORACLE");
  }

  exit(0);
}
