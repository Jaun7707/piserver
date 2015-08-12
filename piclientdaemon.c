#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>
#include <signal.h>
#include "piheader.h"

/* Globals for exiting */
struct sigaction act, old;
int sockfd;
int pid;
pthread_mutex_t mutex;
pthread_cond_t cond;
pthread_t thread;
pthread_t bootThread;

/* Returns whether or not the file is contained, and if so, if
 * it is within the date range given.
 */
int withinDate(char *name, int range, time_t time) {
  DIR *dir;
  struct dirent *ent;
  dir = opendir("./");

  DirEnt *downList = NULL;
  DirEnt *dcurr = downList;
  if(dir) {
    while((ent = readdir(dir)) != NULL) {
      if(strncmp(ent->d_name, name, 255) == 0) {
        struct stat *attrib = malloc(sizeof(struct stat));
        stat(ent->d_name, attrib);
        printf("%f\n", difftime(attrib->st_atime, time));
        if(abs(difftime(attrib->st_atime, time)) <= range) {
          return 1;
        }
        free(attrib);       
      }
    }
  }
  closedir(dir);

  return 0;
}

/* A function for thread executiong that verifies files local files
 * with those found on the server.
 */
void boot() {
  sleep(10);

  /* Connect to boot server */
  struct sockaddr_in sockaddr;
  int bsockfd = socket(AF_INET, SOCK_STREAM, 0);
  int portno = 5007;
  sockaddr.sin_family = AF_INET;
  inet_aton("192.168.1.149", &sockaddr.sin_addr.s_addr);
  sockaddr.sin_port = htons(portno);
  connect(bsockfd, (struct sockaddr *) &sockaddr, sizeof(sockaddr));

  pthread_mutex_lock(&mutex);
  send(bsockfd, READY, 7, 0);
 
  /* Recive and manage download requests */
  SerTran *bootReq = malloc(sizeof(SerTran));
  while(1) {
    recv(bsockfd, bootReq, sizeof(SerTran), 0);
    if(strncmp(bootReq->name, END, 7) == 0)
      break;
    /* Date checks should go here */
    if(strncmp(bootReq->name, "piclient", 255) == 0 ||
    strncmp(bootReq->name, "piclientdaemon", 255) == 0 ||
    strncmp(bootReq->name, "piserver", 255) == 0 ||
    strncmp(bootReq->name, ".", 255) == 0 ||
    strncmp(bootReq->name, "..", 255) == 0) { /* ||
    withinDate(bootReq->name, 30, bootReq->lastModified) == 1) { */
      send(bsockfd, END, 7, 0);
    }
    else {
      send(bsockfd, DOWNLD, 7, 0);
      pthread_cond_wait(&cond, &mutex);
      /* Download begins in push */
    }
    /* Wait for push to finish */
    send(bsockfd, READY, 7, 0);
  }
  printf("Finished boot sync with server files.\n");
  pthread_mutex_unlock(&mutex);
  close(bsockfd);
  free(bootReq);
}

/* Handle push requests from the server. Here we will delete,
 * download, and modify exisiting files.
 */
void push() {
  /* Connect to the push port of the server */
  struct sockaddr_in sockaddr;
  sockfd = socket(AF_INET, SOCK_STREAM, 0);  
  int portno = 5002;
  sockaddr.sin_family = AF_INET;
  inet_aton("192.168.1.149", &sockaddr.sin_addr.s_addr);
  sockaddr.sin_port = htons(portno);
  connect(sockfd, (struct sockaddr *) &sockaddr, sizeof(sockaddr));

  /* Setup log structures */
  DirEnt *logHead = NULL;
  DirEnt *curr = logHead; 

  /* Wait for messages to come in */
  while(1) {
    /* Recieve header */
    SerTran *msg = malloc(sizeof(SerTran));
    recv(sockfd, msg, sizeof(SerTran), 0);
    
    pthread_mutex_lock(&mutex);
    
    /* If should delete */
    if(strncmp(msg->type, DELETE, 7) == 0) {
      if(remove(msg->name) == 0)
        printf("%-70.70s deleted\n", msg->name);
    }

    /* If should download */
    if(strncmp(msg->type, DOWNLD, 7) == 0) {
      printf("%-66.66s downloading\n", msg->name);
      send(sockfd, msg, sizeof(SerTran), 0);

      /* Setup a temporary name */
      char filepath[265];
      strncpy(filepath, "[pibxtemp]", 10);
      strncpy(&filepath[10], msg->name, 255);
 
      /* Download the file */
      int wffd = open(filepath, O_TRUNC | O_CREAT | O_RDWR, 0666); 
 
      /* Read file contents */
      float progress = 0;
      char downbuff[512];
      while(1) {
        if(strncmp(msg->name, "serverLog.txt", 255) != 0) {
          printf("\r%-51.51s\t%3.1f%%\t%12d B", msg->name,
          (progress / msg->size) * 100, msg->size);
        }

        int bytesRead = recv(sockfd, &downbuff, 512, 0);
        progress += bytesRead;
        if(strncmp(downbuff, END, 7) == 0)
          break;
        write(wffd, &downbuff, bytesRead);
        send(sockfd, READY, 6, 0);
      }
      send(sockfd, READY, 6, 0);
        
      printf("\n");
      close(wffd);

      /* If that message was the serverLog */
      if(strncmp(msg->name, "serverLog.txt", 255) == 0) {
        /* Open the serverLog */
        int logfd = open("[pibxtemp]serverLog.txt", O_RDWR, 0666);

        if(logfd < 0) {
          printf("Error opening server log.");
          exit(1);
        }

        /* Check for existing log file */
        char magicTest[6];
        read(logfd, &magicTest, 6);
        /* Log file already exists */
        if(strcmp(magicTest, MAGIC) == 0) {
          printf("Server log loaded.\n");
          /* Begin reading in the linked list */
          do {
            DirEnt *link = malloc(sizeof(DirEnt));
            read(logfd, link, sizeof(DirEnt));
            if(logHead == NULL) {
              logHead = link;
              curr = link;
            }
            else {
              curr->next = link;
              curr = link;
            }
          } while(curr->next != NULL);         
        }
      }
    }

    free(msg);
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
  }
}

/* Exit by sending the server a DC message */
void exitHandle() {
  SerTran *msg = malloc(sizeof(SerTran));
  memcpy(msg->type, DSCN, 7);
  printf("\nExiting.\n");
  send(sockfd, msg, sizeof(SerTran), 0);
  free(msg);
  waitpid(pid, NULL, 0);
  exit(1);
}

/* Periodically run piclient and recieve server updates */
int main(int argc, char **argv) {
  /* Setup exit action */
  memset(&act, 0, sizeof(act));
  act.sa_handler = exitHandle;
  sigaction(SIGINT, &act, &old);  

  /* Initiate synchornization structs */
  pthread_mutex_init(&mutex, NULL);
  pthread_cond_init(&cond, NULL);

  /* Create a thread to handle push notifications */
  int tid = pthread_create(&thread, NULL, (void*)&push, NULL);
  /* Create a thread to handle initial connection */
  int btid = pthread_create(&bootThread, NULL, (void*)&boot, NULL); 

  /* Run piclient */
  while(1) {
    sleep(2);
    pthread_mutex_lock(&mutex);
    pid = fork();
    if(pid < 0) {
      printf("Could not create piclient.\n");
    }
    else if(pid == 0) {
      execl("./piclient", NULL);
    }
    else if(pid > 0) {
      waitpid(pid, NULL, 0);
      pthread_mutex_unlock(&mutex);
    }
  }
  
  exit(1);
}
