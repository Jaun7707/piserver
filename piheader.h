/* The piheader file includes server transaction types in addition to 
 * commonly used structures. SerTran is used in initiating file transfers
 * and sending appropriate data. DirEnt is used in maintaining the list
 * of files.
 */
char *MAGIC = "MAGIC";
char *END = "END000";
char *UPLOAD = "UPLOAD";
char *DELETE = "DELETE";
char *READY = "READY";
char *DSCN = "DSCN00";
char *DOWNLD = "DOWNLD";

/* A simple server transaction request */
typedef struct serTran {
  char type[7];           /* The type, upload or download */
  char name[255];         /* The name of the file to be used */
  int size;               /* The size of the file in bytes */
  time_t lastModified;    /* The date last modified for boot seq */
} SerTran;

/* A log entry for use in linked lists */
typedef struct dirEnt {
  char name[255];         /* The name of the file */
  time_t lastModified;    /* The date last modified */
  struct dirEnt *next;    /* The next entry of the list */
} DirEnt;
