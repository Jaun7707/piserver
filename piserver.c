#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <dirent.h>
#include <sys/mman.h>
#include "piheader.h"

struct sigaction act, old;
static int *pushFD;

/* A simple structure for sending arguments to threads */
typedef struct obArg {
  int var1, var2;
} ObArg;

/* Exit by sending the server a DC message */
void exitHandle() {
  printf("\nExiting.\n");
  exit(1);
}

/* Commits the log to disk */
void writeList(int fd, DirEnt *list) {
  lseek(fd, 0, SEEK_SET);
  write(fd, MAGIC, 6);
  DirEnt *curr = list;
  while(curr != NULL) {
    write(fd, curr, sizeof(DirEnt));
    curr = curr->next;
    free(curr);
  }
}

void observe(void *a) {
  ObArg *b = (ObArg*)a;
  waitpid(b->var1, NULL, 0);
  close(b->var2);
}
/* A thread function for handling initial client connections. This
 * function is responsible for sending a download request for each
 * file in the directory */
void boot(void *a) {
  /* Get function arguments pid, confd */
  ObArg *args = (ObArg*)a;
  
  /* Basic structs for new server port */
  int socketfd, confd, portno, chillen, pid;
  portno = 5007;
  socketfd = socket(AF_INET, SOCK_STREAM, 0);

  if(socketfd < 0) {
    printf("Boot thread could not create socket.\n");
    exit(1);
  }

  /* Initialization */
  struct sockaddr_in servaddr, chiladdr;
  bzero((char*)&servaddr, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = INADDR_ANY;
  servaddr.sin_port = htons(portno);

  if(bind(socketfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
    printf("Boot thread could not bind socket.");
    exit(1);
  }

  chillen = sizeof(chiladdr);
  listen(socketfd, 1);
  confd = accept(socketfd, (struct sockaddr *) &chiladdr, &chillen);

  /* Client has connected to port, send data */
  DIR *dir;
  struct dirent *ent;
  dir = opendir("./Storage");
  if(dir) {
    while((ent = readdir(dir)) != NULL) {
      /* Get some stats */
      struct stat *attrib = malloc(sizeof(struct stat));
      stat(ent->d_name, attrib);

      /* Send some meta data to this port */
      SerTran *bootReq = malloc(sizeof(SerTran));

      /* Get ready */
      recv(confd, &bootReq->name, 7, 0);

      memcpy(bootReq->type, DOWNLD, 7);
      memcpy(bootReq->name, ent->d_name, 255);
      bootReq->lastModified = attrib->st_atime;
      
      send(confd, bootReq, sizeof(SerTran), 0);
      
      /* Wait for download to complete, yes: DOWNLD, no: END */
      recv(confd, &bootReq->name, 7, 0);
      if(strncmp(bootReq->name, DOWNLD, 7) == 0) {
        memcpy(bootReq->name, ent->d_name, 255);
        memcpy(bootReq->type, DOWNLD, 7);
        bootReq->size = attrib->st_size;
        send(args->var2, bootReq, sizeof(SerTran), 0); /* Download begins in push */ 
      }
      free(attrib);
      free(bootReq);
    }
  }
  SerTran *bootEnd = malloc(sizeof(SerTran));
  recv(confd, &bootEnd->name, 7, 0);
  strncpy(bootEnd->name, END, 7);
  send(confd, bootEnd, sizeof(SerTran), 0);
  close(confd);
  free(bootEnd);
  printf("Finished boot sync with client.\n");
}

/* A thread function for handling pushed user updates. These
 * include things such as file delete notications, file create
 * notifications, etc. */
void push() {
  int socketfd, confd, portno, chillen, pid;
  portno = 5002;
  socketfd = socket(AF_INET, SOCK_STREAM, 0);
  if(socketfd < 0) {
    printf("Push thread could not create socket.\n");
    exit(1);
  }
  
  /* Setup structs */
  struct sockaddr_in servaddr, chiladdr;
  bzero((char*)&servaddr, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = INADDR_ANY;
  servaddr.sin_port = htons(portno);

  /* Bind the socket */
  if(bind(socketfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
    printf("Push thread could not bind socket.\n");
    exit(1);
  }

  chillen = sizeof(chiladdr);
  listen(socketfd, 10);

  while(1) {
    /* Maintain a list of clients */
    confd = accept(socketfd, (struct sockaddr *) &chiladdr, &chillen);
    int index = 0;
    while(index != 9 && pushFD[index] != 0)
      index++;
    pushFD[index] = confd;

    /* Fork for the new connection */
    if(confd < 0) {
      printf("Could not accept socket.\n");
      exit(1);
    }
    pid = fork();
    if(pid > 0) {
      /* Setup observer to close confd */
      ObArg *obargs = malloc(sizeof(ObArg));
      obargs->var1 = pid;
      obargs->var2 = confd;
      pthread_t observer;
      pthread_create(&observer, NULL, (void*)&observe, (void*)obargs);
      pthread_t bootThread;
      pthread_create(&bootThread, NULL, (void*)&boot, (void*)obargs);
    }
    if(pid < 0) {
      printf("Could not fork.\n");
      exit(1);
    }
    /* Child process handles client */
    if(pid == 0) {
      int first = 0;
      SerTran *msg = malloc(sizeof(SerTran));
      printf("SOCKFD %d connected to push.\n", confd);

      while(strncmp(msg->type, DSCN, 7) != 0) {
        /* Handle download request */
        if(strncmp(msg->type, DOWNLD, 7) == 0) {
          printf("%-58.58s sending to SOCKFD %2d\n", msg->name, confd);
          /* Setup the file path */ 
          char filepath[265];
          strncpy(filepath, "./Storage/", 10);
          strncpy(&filepath[10], msg->name, 255);
          /* Send file to client */
          char upbuff[512];
          int uffd = open(filepath, O_RDONLY);
          int bytesRead = 0;
          do {
            bytesRead = read(uffd, &upbuff, 512);
            if(bytesRead == 0)
              break;
            send(confd, &upbuff, bytesRead, 0);
            recv(confd, &upbuff, 6, 0);
          } while(1);
          send(confd, END, 7, 0);
          recv(confd, &upbuff, 6, 0);

          close(uffd);
        }
        recv(confd, msg, sizeof(SerTran), 0);
      }
      /* Push watch process exit */
      printf("SOCKFD %d disconnected from push, releasing index %d.\n", confd, index);
      free(msg);
      pushFD[index] = 0;
      close(confd);
      exit(1);
    }
  }

  printf("User is disconnecting from push.\n");
  exit(1);
}

/* General server creation process is as follows:
 * create socket for the server
 * create a sockaddr_in (socket address) struct
 * setup that struct with appropriate fields
 * bind socket with a port on your local machine */
int main(int argc, char **argv) {
  pushFD = mmap(NULL, sizeof(int)*10, PROT_READ | PROT_WRITE,
  MAP_SHARED | MAP_ANONYMOUS, -1, 0);

  /* Setup exit action */
  memset(&act, 0, sizeof(act));
  act.sa_handler = exitHandle;
  sigaction(SIGINT, &act, &old);

  DirEnt *logHead = NULL;
  DirEnt *curr = logHead; 

  /* Creating a file descriptor for reading from */
  int socketfd, confd, portno, chillen, pid, tid;

  /* Create a new thread for push notifications */
  pthread_t pushThread;
  tid = pthread_create(&pushThread, NULL, (void*) &push, NULL);

  /* Random port number I thought looked nice */
  portno = 5001;

  /* int socket(int domain, int type, int protocol); */
  socketfd = socket(AF_INET, SOCK_STREAM, 0);

  if(socketfd < 0) {
    printf("Could not create socket.\n");
    exit(1);
  }

  /* User bzero() function to zero out the bytes */
  struct sockaddr_in servaddr, chiladdr;
  bzero((char *) &servaddr, sizeof(servaddr));

  /* Setup the struct */
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = INADDR_ANY;
  servaddr.sin_port = htons(portno);

  /* int bind(int sockfd, struct sockaddr *my_addr, int addrlen); */
  if(bind(socketfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0 ) {
    printf("Could not bind socket.\n");
    exit(1);
  }

  chillen = sizeof(chiladdr);

  /* Just accepting 1 client */
  listen(socketfd, 10);
  

  while(1) {
    confd = accept(socketfd, (struct sockaddr *) &chiladdr, &chillen);
    if(confd < 0) {
      printf("Could not accept socket.\n");
      exit(1);
    }
    pid = fork();
    if(pid < 0) {
      printf("Could not fork.\n");
      exit(1);
    }
    /* Child process handles client */
    if(pid == 0) {
      int filesSent = 0;
      SerTran *tranRequest = malloc(sizeof(SerTran));
      int shouldBreak = 0;
      do {
        char downbuff[512];
        recv(confd, tranRequest, sizeof(SerTran), 0);
        send(confd, READY, 6, 0);
        if(strncmp(tranRequest->type, DELETE, 7) == 0) {
          /* Delete the file locally */
          printf("%-70.30s deleted.\n", tranRequest->name);
          char filepath[265];
          char delPath[284];
          strncpy(filepath, "./Storage/", 10);
          strncpy(delPath, "./Backup/", 9);
          char datebuff[20];
          time_t now = time(NULL);
          strftime(datebuff, 20, "%Y-%m-%d %H:%M:%S", localtime(&now));
          strncpy(&filepath[10], tranRequest->name, 255);
          strncpy(&delPath[9], datebuff, 19);
          strncpy(&delPath[28], tranRequest->name, 255);
          //remove(filepath); send to backup now
          rename(filepath, delPath);

          /* Tell clients to delete the file as well */
          SerTran *tellDel = malloc(sizeof(SerTran));
          strncpy(tellDel->type, DELETE, 7);
          strncpy(tellDel->name, tranRequest->name, 255);
          int i;
          for(i = 0; i < 10; i++) {
            if(pushFD[i] != 0)
              send(pushFD[i], tellDel, sizeof(SerTran), 0);
          }
          exit(1);
        }
        if(strncmp(tranRequest->type, END, 7) == 0)
          break;
 
        /* Open a the file (if it exists) for writting to */
        char filepath[265];
        strncpy(filepath, "./Storage/", 10);
        strncpy(&filepath[10], tranRequest->name, 255);
        int wffd = open(filepath, O_TRUNC | O_CREAT | O_RDWR, 0666);
        filesSent++;
 
        /* Read file contents */
        float progress = 0;
        while(1) {
          printf("\r%-50.50s\t%3.1f%%\t%13d B", tranRequest->name,
          (progress / tranRequest->size) * 100,
          tranRequest->size);

          int bytesRead = recv(confd, &downbuff, 512, 0);
          progress += bytesRead;
          if(strncmp(downbuff, END, 7) == 0)
            break;
          write(wffd, &downbuff, bytesRead);
          send(confd, READY, 6, 0);
        }
        send(confd, READY, 6, 0);
        
        printf("\n");
        close(wffd);

        /* Tell clients to download this file */
        SerTran *pushDownload = malloc(sizeof(SerTran));
        memcpy(pushDownload->type, DOWNLD, 7);
        memcpy(pushDownload->name, tranRequest->name, 255);
        pushDownload->size = tranRequest->size;
        struct sockaddr_in sockaddr1, sockaddr2;
        socklen_t socklen1 = sizeof(sockaddr1);
        socklen_t socklen2 = sizeof(sockaddr2);
        int i;
        for(i = 0; i < 10; i++) {
          if(pushFD[i] != 0) {
            getpeername(confd, (struct sockaddr *)&sockaddr1, &socklen1);
            getpeername(pushFD[i], (struct sockaddr *)&sockaddr2, &socklen2);
            if(sockaddr1.sin_addr.s_addr == sockaddr2.sin_addr.s_addr)
              continue;
            send(pushFD[i], pushDownload, sizeof(SerTran), 0);
          }
        }
        free(pushDownload);
      } while(1);

      close(confd);
      exit(1);
    }
    /* Parent process closes his descriptor and waits for more */
    else {
      close(confd);
    }
  }

  exit(1);
}
