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
#include "piheader.h"

/* Commits the log to disk */
void writeList(int fd, DirEnt *list) {
  lseek(fd, 0, SEEK_SET);
  write(fd, MAGIC, 6);
  DirEnt *curr = list;
  while(curr != NULL) {
    write(fd, curr, sizeof(DirEnt));
    curr = curr->next;
  }
}

/* If the log entry exists, returns the date last modified */
time_t modDate(DirEnt *list, char name[255]) {
  if(list == NULL)
    return 0;

  DirEnt *curr = list;
  while(curr != NULL) {
    if(strncmp(curr->name, name, 255) == 0)
      return curr->lastModified;
    curr = curr->next;
  }

  return 0;
}

/* If the log entry exists, return the 1 */
int listContains(DirEnt *list, char name[255]) {
  if(list == NULL)
    return 0;
  
  DirEnt *curr = list;
  while(curr != NULL) {
    if(strncmp(curr->name, name, 255) == 0)
      return 1;
    curr = curr->next;
  }

  return 0;
}

int main(int argc, char **argv) {
  /* Create a socket */
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  
  /* Create the address struct */
  struct sockaddr_in sockaddr;

  /* Set the server's port number, same as on the server */
  int portno = 5001;

  /* Setup fileds */
  sockaddr.sin_family = AF_INET;

  /* Server's ip address, running on the pi it is the pi */
  inet_aton("192.168.1.149", &sockaddr.sin_addr.s_addr);
  sockaddr.sin_port = htons(portno);

  connect(sockfd, (struct sockaddr *) &sockaddr, sizeof(sockaddr));
  
  /* Read in log if it exists */
  int logfd = open("log.txt", O_RDWR | O_CREAT, 0666);
  if(logfd < 0) {
    printf("Error opening log.");
    exit(1);
  }

  /* Setup log structures */
  DirEnt *logHead = NULL;
  DirEnt *curr = logHead;

  /* Check for existing log file */
  char magicTest[6];
  int br = read(logfd, &magicTest, 6);

  /* Log file already exists */
  if(br > 0 && strncmp(magicTest, MAGIC, 7) == 0) {
    printf("Log loaded.\n");

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
  /* New log must be created */
  else {
    printf("New log created.\n");
  }

  /* Check for files that require updating */
  curr = logHead;
  DIR *dir;
  struct dirent *ent;
  dir = opendir("./");

  DirEnt *downList = NULL;
  DirEnt *dcurr = downList;
  if(dir) {
    while((ent = readdir(dir)) != NULL) {
      if(strncmp(ent->d_name, "[pibxtemp]", 10) == 0) {
        rename(ent->d_name, &ent->d_name[10]);
        DirEnt *entry = malloc(sizeof(DirEnt));
        memcpy(entry->name, &ent->d_name[10], 255);
        entry->lastModified = 0;
        entry->next = NULL;
        if(downList == NULL) {
          downList = entry;
          dcurr = entry;
        }
        else {
          dcurr->next = entry;
          dcurr = dcurr->next;
        }
      }
    }
  }
  rewinddir(dir);

  if(dir) {
    while((ent = readdir(dir)) != NULL) {
      /* Get stat on the file we are considering */
      struct stat *attrib = malloc(sizeof(struct stat));
      stat(ent->d_name, attrib);

      /* If the file does not exist or was modified */
      if(listContains(logHead, ent->d_name) == 0 ||
      (long)modDate(logHead, ent->d_name) != (long)attrib->st_mtime) {
        /* Make sure the file isn't ./ or ../ */
        if(strncmp(ent->d_name, ".", 2) == 0 ||
        strncmp(ent->d_name, "..", 3) == 0 ||
        strncmp(ent->d_name, "log.txt", 8) == 0 ||
        strncmp(ent->d_name, ".DS_Store", 9) == 0 ||
        strncmp(ent->d_name, "serverLog.txt", 14) == 0 ||
        attrib->st_size <= 0)
          continue;

        /* Update the log file */
        DirEnt *newEntry = malloc(sizeof(DirEnt));
        memcpy(newEntry->name, ent->d_name, 255);
        newEntry->next = NULL;
        newEntry->lastModified = attrib->st_mtime;
        if(curr == NULL) {
          curr = newEntry;
          logHead = newEntry;
        }
        else if(listContains(logHead, ent->d_name) == 0) {
          /* Navigate curr to the end */
          while(curr->next != NULL)
            curr = curr->next;

          /* Insert node */
          curr->next = newEntry;
          curr = curr->next;
        }
        else if(listContains(logHead, ent->d_name) == 1) {
          /* Find existing node */
          while(curr != NULL) {
            if(strncmp(curr->name, ent->d_name, 255) == 0)
              break;
            curr = curr->next;
          }
          
          /* Modify node */
          curr->lastModified = newEntry->lastModified;
        }

        /* Upload the file to the server, begin by sending header */
        if(listContains(downList, ent->d_name) == 0) {
          SerTran *upload = malloc(sizeof(SerTran));
          memcpy(upload->type, UPLOAD, 7);
          memcpy(upload->name, ent->d_name, strnlen(ent->d_name, 255));
          upload->size = attrib->st_size;
          send(sockfd, upload, sizeof(SerTran), 0);
          recv(sockfd, upload, 6, 0);

          /* Upload file */
          char upbuff[512];
          int uffd = open(ent->d_name, O_RDONLY);
          int bytesRead = 0;
          do {
            bytesRead = read(uffd, &upbuff, 512);
            if(bytesRead == 0)
              break;
            send(sockfd, &upbuff, bytesRead, 0);
            recv(sockfd, &upbuff, 6, 0);
          } while(1);
          send(sockfd, END, 7, 0);
          recv(sockfd, &upbuff, 6, 0);

          close(uffd);
        }
      }
      else {
        printf("%-67.67s up to date\n", ent->d_name);
      }
    }
  }
  /* Send end transaction */
  SerTran *end = malloc(sizeof(SerTran));
  memcpy(end->type, END, 7);
  send(sockfd, end, sizeof(SerTran), 0);

  /* Check for deleted files */
  curr = logHead;
  while(curr != NULL) { 
    /* Search through the directory for the file in the log */
    int contains = 0;
    dir = opendir("./");
    if(dir) {
      while((ent = readdir(dir)) != NULL) {
        if(strncmp(curr->name, ent->d_name, 255) == 0) {
          contains = 1;
          break;
        }
      }
    }
    
    /* If the file was not contained, send server delete file message */
    if(contains == 0) {
      printf("%-69.30s deleted.\n", curr->name);
      sockfd = socket(AF_INET, SOCK_STREAM, 0);
      connect(sockfd, (struct sockaddr *) &sockaddr, sizeof(sockaddr));
      SerTran *del = malloc(sizeof(SerTran));
      memcpy(del->type, DELETE, 7);
      memcpy(del->name, curr->name, 255);
      send(sockfd, del, sizeof(SerTran), 0);
 
      /* Should wipe from list */
      DirEnt *delCurr = logHead;
      while(curr != delCurr && strncmp(delCurr->next->name, curr->name, 255) != 0)
        delCurr = delCurr->next;
      delCurr->next = curr->next;
    }
    
    curr = curr->next;
  }

  /* Commit log.txt */
  writeList(logfd, logHead);
  close(sockfd);

  exit(1);
}
