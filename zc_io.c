#include "zc_io.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <semaphore.h>
#include <pthread.h>
#include <string.h>


// The zc_file struct is analogous to the FILE struct that you get from fopen.
struct zc_file {
  // Insert the fields you need here.
  int fd; //file deciptor
  off_t fileOffset; //varibale to keep offset of file
  size_t sizeoffile;  //total size of file
  char * addr; //to keep track of base
  off_t lastwriteoffset; //to store last write offset
  size_t lastwritesize; //to store last write size
  pthread_mutex_t lock; //for synchroniztion
  int newfile; //for new file flag
  int numberofreads; //for synchronization
  sem_t s1; //for synchronization
};

#define handle_error(msg) \
           do { perror(msg); exit(EXIT_FAILURE); } while (0)
           
zc_file *zc_open(const char *path) {
  // To implement
  zc_file * file = malloc(sizeof(zc_file));
 int fd = open(path, O_CREAT | O_RDWR, 0666);
  struct stat sizing; //builtin strcut to find the size of the file
  if(fd >= 0 && fstat(fd, &sizing) >= 0){
  	file->fd = fd;
  	file->fileOffset = 0;
  	file->sizeoffile = sizing.st_size;
  	//when a new file is created it has 0 size, which cannot be mapped and mmap gives invalid argument error so
  	// to solve that we will map file on few bytes on memory
  	if(file->sizeoffile == 0){
  		file->addr = mmap(NULL, file->sizeoffile + 10, PROT_READ | PROT_WRITE, MAP_SHARED,file->fd, 0);
  		file->sizeoffile = file->sizeoffile + 10;
  		file->newfile = 1;
  	} else {  //if file is not new
  		file->addr = mmap(NULL, file->sizeoffile, PROT_READ | PROT_WRITE, MAP_SHARED,file->fd, 0);
  		file->newfile = 0;
  	}//initializing the mutex
  	if(pthread_mutex_init(&file->lock, NULL) != 0){
  		printf("\n mutex init has failed\n");
  	}
  	sem_init(&file->s1,1,1); //initializing the semaphore
  	file->lastwriteoffset = 0;
  	file->lastwritesize = 0;
  	file->numberofreads = 0;
  	if(file->addr == MAP_FAILED){  //to handle any error given by mmap
  		handle_error("mmap");
        	return NULL;
  }
  	return file;
  }
  return NULL;
}

int zc_close(zc_file *file) {
// To implement
  munmap(file->addr, file->sizeoffile);  //un-mapping all the mapped memory
  close(file->fd); //closing the file 
  pthread_mutex_destroy(&file->lock); //destroying the mutex
  return -1;
}
           
const char *zc_read_start(zc_file *file, size_t *size) {
  // To implement
  pthread_mutex_lock(&file->lock); //locking the mutex to read
  file->numberofreads++;
  if(file->numberofreads == 1){
 	sem_wait(&file->s1); //making writer wait
  }
 
  size_t temp = *size;
  size_t tempoffset = file->fileOffset;
  if(file->fileOffset > file->sizeoffile){
  	return NULL;
  }
  
  if(*size  <= file->sizeoffile){
  	*size = *size + file->fileOffset;
  	*size = temp;
  	if(*size == file->sizeoffile){ //assiging the size that has been read by the function
  		*size = file->sizeoffile - file->fileOffset;
  	}
  	file->fileOffset += temp;
  }
 pthread_mutex_unlock(&file->lock);  //unlocking the mutex 
 
  if(file->addr != MAP_FAILED){
  	return file->addr + tempoffset;
  }
  else{
  	return NULL;
  }
  
}

void zc_read_end(zc_file *file) {
  // To implement
 pthread_mutex_lock(&file->lock);  //locking the mutex to update the values for synchronization
 file->numberofreads--; //incrementing the number of reads on files after finishing the read
 if(file->numberofreads == 0){
 	sem_post(&file->s1);  //signaling the writer to execute if the reader is the last reader
 }
 pthread_mutex_unlock(&file->lock); //unclocking the mutex
  
}

char *zc_write_start(zc_file *file, size_t size) {
  // To implement
  sem_wait(&file->s1);
  size_t temp = size;
  size_t temp2 = file->sizeoffile;
  file->lastwritesize = size;
  struct stat sizing;  //builtin struct to know the size of the file
 
  if(size > file->sizeoffile){  //increasing the size of the file inorder to accoomodate the write requests 
  	ftruncate(file->fd, size); 
        if(fstat(file->fd, &sizing) >= 0){
  		file->sizeoffile = sizing.st_size;
  	}
 }
  if(size  <= file->sizeoffile){  //if the requested size is less than file size but greater that the filesize+fileoffset we expand file
  	size = size + file->fileOffset;
  if(size > file->sizeoffile){
  	 ftruncate(file->fd, size);
      	if(fstat(file->fd, &sizing) >= 0){
  		file->sizeoffile = sizing.st_size;
  	        file->addr = mmap(NULL, file->sizeoffile, PROT_WRITE, MAP_SHARED,file->fd, 0);
        }
  	 size = file->sizeoffile;
  	}
  	//if we have a new file then whenever we increase the size of the file for write, we have to re-map the file with that
  	//increased size. That's why we are unmapping the previous file and mapping the file again with greater size
  	//otherwise it will give segmentation fault error.
  	if(file->newfile == 1){  
  	munmap(file->addr, temp2);
  	file->addr = mmap(NULL, size, PROT_WRITE, MAP_SHARED,file->fd, 0);
  }
  size = temp;
  	
  if(size == file->sizeoffile){
  	size = file->sizeoffile - file->fileOffset;
  }
  //updating values of different values for file
  file->lastwriteoffset = file->fileOffset;
  file->fileOffset += temp;
  }
  if(file->addr != MAP_FAILED){
       //returning the buffer by adding the last write offset so that the required content is returned
  	return file->addr  + file->lastwriteoffset ;  
  }
  else{
  	return NULL;
  }
}

void zc_write_end(zc_file *file) {
  // To implement
  sem_post(&file->s1);
}

off_t zc_lseek(zc_file *file, long offset, int whence) {
  // To implement
  sem_wait(&file->s1);
  if(whence == 0){
  	file->fileOffset = 0 + offset;
  	sem_post(&file->s1);
  	return file->fileOffset;
  }
  if(whence == 1){
  	file->fileOffset = file->fileOffset + offset;
  	sem_post(&file->s1);
  	return file->fileOffset;
  }
  if(whence == 2){
  	file->fileOffset = file->sizeoffile + offset;
  	sem_post(&file->s1);
  	return file->fileOffset;
  }
  return -1;
}

int zc_copyfile(const char *source, const char *dest) {
  // To implement
  zc_file *zcfilesrc = zc_open(source);
  zc_file *zcfiledes = zc_open(dest);
  
  size_t size = zcfilesrc->sizeoffile;
  
  if(zcfilesrc!=NULL && zcfiledes!=NULL){
  //using zc_write_start function will return the buffer on which memcpy will copy the content of the file
  	memcpy(zc_write_start(zcfiledes, size), zcfilesrc->addr, size);
  	zc_close(zcfilesrc);
  	zc_close(zcfiledes);
  	return 0;
  }
  return -1;
}
