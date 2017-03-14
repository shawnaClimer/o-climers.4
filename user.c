#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <ctype.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/msg.h>
#include "constants.h"

//for message queue
#define MSGSZ	20
typedef struct msgbuf {
	long mtype;
	int mtext[MSGSZ];
} message_buf;
//for PCBs
typedef struct pcb {
	long totalcpu;
	long timesys;
	long timeburst;
	int pid;
} process_cb;
//for shared memory clock
static int *shared;

//for pcb shared memory
static process_cb *block;

void sighandler(int sigid){
	printf("Caught signal %d\n", sigid);
	
	//cleanup shared memory
	detachshared();
	
	exit(sigid);
}
int detachshared(){
	if((shmdt(shared) == -1) || (shmdt(block) == -1)){
		perror("failed to detach from shared memory");
		return -1;
	}else{
		return 1;
	}
}

int main(int argc, char **argv){
	//pcb
	key_t pcbkey;
	int pcbmid;
	process_cb *blockptr;
	//create key
	if((pcbkey = ftok("oss.c", 5)) == -1){
		perror("pcbkey error");
		return 1;
	}
	//get shared memory
	if((pcbmid = shmget(pcbkey, (sizeof(process_cb) * MAXQUEUE), IPC_CREAT | 0666)) == -1){
		perror("failed to create pcb shared memory");
		return 1;
	}
	//attach to shared memory
	if((block = (process_cb *)shmat(pcbmid, NULL, 0)) == (void *)-1){
		perror("failed to attach to pcb memory");
		return 1;
	}
	blockptr = block;	
	//shared memory
	key_t key;
	int shmid;
	//int *shared;
	int *clock;
	void *shmaddr = NULL;
	
	if((key = ftok("oss.c", 7)) == -1){
		perror("key error");
		return 1;
	} 
	//get the shared memory
	if((shmid = shmget(key, (sizeof(int) * 2), IPC_CREAT | 0666)) == -1){
		perror("failed to create shared memory");
		return 1;
	}
	//attach to shared memory
	if((shared = (int *)shmat(shmid, shmaddr, 0)) == (void *)-1){
		perror("failed to attach");
		if(shmctl(shmid, IPC_RMID, NULL) == -1){
			perror("failed to remove memory seg");
		}
		return 1;
	}
	clock = shared;
	
	/* int startSec, startNs;//start "time" for process
	startSec = clock[0];
	startNs = clock[1];
	int runTime = rand() % 100000;
	int endSec = startSec;
	int endNs = startNs + runTime;
	if(endNs > 1000000000){
		endSec++;
		endNs -= 1000000000;
	} */
	
	//message queue
	int msqid;
	key_t msgkey;
	message_buf sbuf, rbuf;
	size_t buf_length = 0;
	
	if((msgkey = ftok("oss.c", 2)) == -1){
		perror("msgkey error");
		return 1;
	}
	if((msqid = msgget(msgkey, 0666)) < 0){
		perror("msgget from user");
		return 1;
	}
	int mypid = getpid();
	printf("my pid is %d\n", mypid);
	//loop for critical section
	int timeisup = 0;
	int timeran = 0;
	//int more = 0;
	while(timeisup == 0){
		//signal handler
		signal(SIGINT, sighandler);
		
		//look for message type PID critical section "token"
		if(msgrcv(msqid, &rbuf, MSGSZ, mypid, 0) < 0){
			//printf("message not received.\n");
		}else{
			printf("critical section token received.\n");	
			//TODO check for I/O interrupt
			timeran = (QUANTUM * rbuf.mtext[0]);//number of QUANTUMs to run
			
			//find my pcb
			int foundpcb = 0;
			int i;
			for(i = 0; i < MAXQUEUE; i++){
				if(blockptr[i].pid == mypid){
					foundpcb = 1;
					break;
				}
			}
			//update pcb
			if(foundpcb == 1){
				printf("found my pcb\n");
				//check timesys > timeran
				if(blockptr[i].timesys > (blockptr[i].totalcpu + timeran)){
					//more = 1;//needs more time
					blockptr[i].totalcpu += timeran;
					blockptr[i].timeburst = timeran;
				}else{
					timeran = (blockptr[i].totalcpu + timeran) - blockptr[i].timesys;
					blockptr[i].totalcpu += timeran;
					blockptr[i].timeburst = timeran;
					timeisup = 1;//done
				}
				
			}//end found and updated pcb
			
			//blockptr[i].timesys = runTime;
			//check time 
			clock = shared;
			
			
			//release critical section
			//message type 1
			sbuf.mtype = 1;
			sbuf.mtext[0] = mypid;
			sbuf.mtext[1] = timeisup;//1 for terminated, 0 for re-queue
			sbuf.mtext[2] = clock[0];
			sbuf.mtext[3] = clock[1];
			sbuf.mtext[4] = timeran;
			
			buf_length = sizeof(sbuf.mtext) + 1;
			//buf_length = 0;
			//send message
			if(msgsnd(msqid, &sbuf, MSGSZ, IPC_NOWAIT) < 0){
			//if(msgsnd(msqid, &sbuf, buf_length, IPC_NOWAIT) < 0) {
				printf("%d, %d\n", msqid, sbuf.mtype);//, sbuf.mtext[0], buf_length);
				perror("msgsnd from user");
				return 1;
			}else{
				printf("critical section token sent.\n");
			}
		}//end received message token
		
	}//end of while loop
	
			
	//code for freeing shared memory
	if(detachshared() == -1){
		return 1;
	}
	/* if(shmdt(shared) == -1){
		perror("failed to detach from shared memory");
		return 1;
	} */
	
	
	return 0;
}