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
#include <errno.h>
#include "constants.h"
#include "queue.h"

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
	int currentqueue;
} process_cb;

//for shared memory clock
static int *shared;
static int shmid;

//shared memory PCBs
static process_cb *block;
static int pcbmid;

//for pids
static pid_t *pidptr;
//for message queue
static int msqid;

void sighandler(int sigid){
	printf("Caught signal %d\n", sigid);
	//send kill message to children
	//access pids[] to kill each child
	int i = 0;
	while(pidptr[i] != '\0'){
		if(pidptr[i] != 0){
			kill(pidptr[i], SIGQUIT);
		}
		i++;
	}
	//kill(0, SIGQUIT);
	//cleanup shared memory
	detachshared();
	removeshared();
	deletequeue();
	exit(sigid);
}
int deletequeue(){
	//delete message queue
	struct msqid_ds *buf;
	if(msgctl(msqid, IPC_RMID, buf) == -1){
		perror("msgctl: remove queue failed.");
		return -1;
	}
}
int detachshared(){
	if((shmdt(shared) == -1) || (shmdt(block) == -1)){
		perror("failed to detach from shared memory");
		return -1;
	}
	
	
}
int removeshared(){
	if((shmctl(shmid, IPC_RMID, NULL) == -1) || (shmctl(pcbmid, IPC_RMID, NULL) == -1)){
		perror("failed to delete shared memory");
		return -1;
	}
	
}
int main(int argc, char **argv){
	
	
	//getopt
	extern char *optarg;
	extern int optind;
	int c, err = 0;
	int hflag=0, sflag=0, lflag=0, tflag=0;
	static char usage[] = "usage: %s -h  \n-l filename \n-i y \n-t z\n";
	
	char *filename, *x, *z;
	
	while((c = getopt(argc, argv, "hs:l:i:t:")) != -1)
		switch (c) {
			case 'h':
				hflag = 1;
				break;
			case 's':
				sflag = 1;
				x = optarg;//max number of slave processes
				break;
			case 'l':
				lflag = 1;
				filename = optarg;//log file 
				break;
			
			case 't':
				tflag = 1;
				z = optarg;//time until master terminates
				break;
			case '?':
				err = 1;
				break;
		}
		
	if(err){
		fprintf(stderr, usage, argv[0]);
		exit(1);
	}
	//help
	if(hflag){
		puts("-h for help\n-l to name log file\n-s for number of slaves\n-i for number of increments per slave\n-t time for master termination\n");
	}
	//set default filename for log
	if(lflag == 0){
		filename = "test.out";
	}
	puts(filename);
	//number of slaves
	int numSlaves=5; 
	if(sflag){//change numSlaves
		numSlaves = atoi(x);
	}
	//puts(x);
	
	//time in seconds for master to terminate
	int endTime=20;
	if(tflag){//change endTime
		endTime = atoi(z);
	}
	//puts(z);
	
	//message queue
	//int msqid;
	key_t msgkey;
	message_buf sbuf, rbuf;
	size_t buf_length = 0;
	
	if((msgkey = ftok("oss.c", 2)) == -1){
		perror("msgkey error");
		return 1;
	}
	if((msqid = msgget(msgkey, IPC_CREAT | 0666)) < 0){
		perror("msgget from oss");
		return 1;
	}
	
	//PCBs
	key_t pcbkey;
	process_cb *blockptr;
	//create key
	if((pcbkey = ftok("oss.c", 5)) == -1){
		perror("pcbkey error");
		return 1;
	}
	//get shared memory change to sizeof *block?
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
	//int shmid;
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
	clock[0] = 0;//initialize "clock" to zero
	clock[1] = 0;
	
	//create start time
	struct timespec start, now;
	clockid_t clockid;//clockid for timer
	clockid = CLOCK_REALTIME;
	long starttime, nowtime;
	if(clock_gettime(clockid, &start) == 0){
		starttime = start.tv_sec;
	}
	if(clock_gettime(clockid, &now) == 0){
		nowtime = now.tv_sec;
	}
	int totalProcesses = 0;//keep count of total processes created
	int currentnum = 0;//keep count of current processes in system
	int queue0[MAXQUEUE];//create queue0
	initqueue(queue0);
	int queue1[MAXQUEUE];
	initqueue(queue1);
	int queue2[MAXQUEUE];
	initqueue(queue2);
	//for forking children
	pid_t pids[numSlaves];//pid_t *pidptr points to this
	pidptr = pids;
	//initialize pids[]
	printf("initializing pids[]\n");
	int i;
	for(i = 0; i < numSlaves; i++){
		pids[i] = 1;
	}
	printf("pids[] initialized to 1\n");
	//printf("%d process id forked.\n", pids[0]);
	
	//interval between forking children
	int timetofork = 10000;
	int currentns, prevns = 0;
	//pid
	pid_t pid;
	int thispid;
	int childsec, childns;//for time sent by child
	int status;//for wait(&status)
	int sendnext = 1;//send next process message to run
	int loglength = 0;//for log file
	
	//initialize random number generator
	srand((unsigned) time(NULL));
	
	while(totalProcesses < 100 && clock[0] < 2 && (nowtime - starttime) < endTime){
		//signal handler
		signal(SIGINT, sighandler);
		
		//increment "system" clock
		clock[1] += rand() % 1000;
		if(clock[1] > 1000000000){
			clock[0] += 1;
			clock[1] -= 1000000000;
		}
		
		//check time
		currentns = clock[1];
		//if time to fork new process
		if(((currentns - prevns) >= timetofork) && (currentnum < numSlaves)){
			//set new previous 
			prevns = currentns;
			//find empty pids[]
			for(i = 0; i < numSlaves; i++){
				if(pids[i] == 1){
					break;
				}
			}
			printf("found open spot at pids[%d]\n", i);
			pids[i] = fork();
			if(pids[i] == -1){
				perror("Failed to fork");
				return 1;
			}
			if(pids[i] == 0){
				execl("user", "user", NULL);
				perror("Child failed to exec user");
				return 1;
			}
			totalProcesses++;	
			currentnum++;
			
			printf("adding %d to pcb\n", pids[i]);
			//initialize pcb pid
			blockptr[i].pid = pids[i];
			//set total time in system
			blockptr[i].timesys = rand() % 100000;
			printf("process has been given %d time\n", blockptr[i].timesys);
			blockptr[i].currentqueue = 0;
			
			printf("adding to queue\n");
			//put in queue0
			if(pushqueue(queue0, pids[i]) != 1){
				perror("Failed to add to queue0");
				return 1;
			}
			//write to file
			if(loglength < 1000){
				FILE *logfile;
				logfile = fopen(filename, "a");
				if(logfile == NULL){
					perror("Log file failed to open");
					return -1;
				}
				fprintf(logfile, "OSS: Creating process %d and adding to queue0 at time %d:%d\n", pids[i], clock[0], clock[1]);
				fclose(logfile);
				loglength++;
			}
			//increment "system" clock
			clock[1] += rand() % 1000;
			if(clock[1] > 1000000000){
				clock[0] += 1;
				clock[1] -= 1000000000;
			}
		}//end of fork new child loop
		
		if((sendnext == 1) && currentnum > 0){
			sendnext = 0;
			//check queue0
			pid = popqueue(queue0);
			printf("popping queue0\n");
			if(pid == 0){
				//queue0 is empty check queue1
				printf("queue0 is empty\n");
				pid = popqueue(queue1);
		
				if(pid == 0){
					//queue1 is empty check queue2
					printf("queue1 is empty\n");
					pid = popqueue(queue2);
					if(pid == 0){
						printf("queue2 is also empty\n");
						perror("all queues are empty");
						return 1;
					}
				}
			
			}
				printf("sending message to pid %d to run\n", pid);
				//send message to run
				//find pcb
				int x;
				for (x = 0; x < numSlaves; x++){
					if(blockptr[x].pid == pid){
						break;
					}
				}
				sbuf.mtype = pid;
				sbuf.mtext[0] = blockptr[x].currentqueue;//to determine num QUANTUMs
				//buf_length = sizeof(sbuf.mtext) + 1;
				if(msgsnd(msqid, &sbuf, MSGSZ, IPC_NOWAIT) < 0){
				//if(msgsnd(msqid, &sbuf, buf_length, IPC_NOWAIT) < 0) {
					printf("%d, %d, %d, %d\n", msqid, sbuf.mtype);//, sbuf.mtext[0], buf_length);
					perror("msgsnd from oss");
					return 1;
				}else{
					//write to file
					if(loglength < 1000){
						FILE *logfile;
						logfile = fopen(filename, "a");
						if(logfile == NULL){
							perror("Log file failed to open");
							return -1;
						}
						fprintf(logfile, "OSS: Dispatching process %d at time %d:%d\n", pid, clock[0], clock[1]);
						fclose(logfile);
						loglength++;
					}
				
					//increment "system" clock
					clock[1] += rand() % 1000;
					if(clock[1] > 1000000000){
						clock[0] += 1;
						clock[1] -= 1000000000;
					}
				}//end of successful message send
			
			//}//end of message send
			
		}//end of send next 
		//receive message from user
		if(currentnum > 0){
			//check mailbox for msg
			//get time from child
			errno = 0;
			//read message type 1 if one is there
			if(msgrcv(msqid, &rbuf, MSGSZ, 1, MSG_NOERROR | IPC_NOWAIT) < 0){
				if(errno != ENOMSG){
					perror("msgrcv in oss");
					return 1;
				}
				//printf("message time up from user not received.\n");
			}else{
				printf("received message from user\n");
				sendnext = 1;//send next process 
				//pid of sender
				pid = rbuf.mtext[0];
				//code from user
				if(rbuf.mtext[1] == 1){
					//process is terminating
					//write to file
					if(loglength < 1000){
						FILE *logfile;
						logfile = fopen(filename, "a");
						if(logfile == NULL){
							perror("Log file failed to open");
							return -1;
						}
						fprintf(logfile, "OSS: Child pid %d is terminating at time %d:%d. It ran for %d this burst\n", pid, clock[0], clock[1], rbuf.mtext[3]);
						fclose(logfile);
						loglength++;
					}
					
					pid = wait(&status);//make sure child terminated
					currentnum--;
					//find in pids[]
					int x;
					for(x = 0; x < numSlaves; x++){
						if(pids[x] == pid){
							printf("found pid that terminated\n");
							pids[x] = 1;
							break;
						}
					}
					//TODO update pcb
					printf("pcb block at %d has values: pid = %d total time in system = %d and totalcpu = %d\n", x, blockptr[x].pid, blockptr[x].timesys, blockptr[x].totalcpu);
					
				//end of if process terminates	
				//re queue process
				}else if(rbuf.mtext[1] == 0){
					//requeue
					//write to file
					if(loglength < 1000){
						FILE *logfile;
						logfile = fopen(filename, "a");
						if(logfile == NULL){
							perror("Log file failed to open");
							return -1;
						}
						fprintf(logfile, "OSS: Child pid %d is re queueing at time %d:%d. It ran for %d this burst\n", pid, clock[0], clock[1], rbuf.mtext[3]);
						fclose(logfile);
						loglength++;
					}
					//requeue
					//interrupted so move to queue0
					if(rbuf.mtext[2] == 1){
						if(pushqueue(queue0, pid) == 1){
							//successful pushqueue
							printf("pid %d pushed to queue0\n");
							//write to file
							if(loglength < 1000){
								FILE *logfile;
								logfile = fopen(filename, "a");
								if(logfile == NULL){
									perror("Log file failed to open");
									return -1;
								}
								fprintf(logfile, "OSS: Child pid %d was interrupted. Putting in queue0 at %d:%d\n", pid, clock[0], clock[1]);
								fclose(logfile);
								loglength++;
							}
						}else{
							perror("push to queue");
							return 1;
						}
					//not interrupted, move to next queue down
					}else{
						int x;
						for(x = 0; x < numSlaves; x++){
							if(blockptr[x].pid == pid){
								printf("found pcb for changing queue\n");
								break;
							}
						}
						if(blockptr[x].currentqueue < 2){
							//move down
							blockptr[x].currentqueue++;
							printf("now in queue%d\n", blockptr[x].currentqueue);
						}
						if(blockptr[x].currentqueue == 1){
							if(pushqueue(queue1, pid) == 1){
								//write to file
								if(loglength < 1000){
									FILE *logfile;
									logfile = fopen(filename, "a");
									if(logfile == NULL){
										perror("Log file failed to open");
										return -1;
									}
									fprintf(logfile, "OSS: Child pid %d put in queue1 at %d:%d\n", pid, clock[0], clock[1]);
									fclose(logfile);
									loglength++;
								}
							}
						}else{
							if(pushqueue(queue2, pid) == 1){
								//write to file
								if(loglength < 1000){
									FILE *logfile;
									logfile = fopen(filename, "a");
									if(logfile == NULL){
										perror("Log file failed to open");
										return -1;
									}
									fprintf(logfile, "OSS: Child pid %d put in queue2 at %d:%d\n", pid, clock[0], clock[1]);
									fclose(logfile);
									loglength++;
								}
							}
						}
					}
					
					//increment "system" clock
					clock[1] += rand() % 1000;
					if(clock[1] > 1000000000){
						clock[0] += 1;
						clock[1] -= 1000000000;
					}
					
					
				}//end re queue process
			}//end message receive
		}//end check for messages
		//get current time
		if(clock_gettime(clockid, &now) == 0){
			nowtime = now.tv_sec;
		}
		
	}//end of while loop
	
	//pid = wait(&status);
		
		
		
	//printf("User process %ld exited with status 0x%x.\n", (long)pid, status);
	//sleep(2);
	//TODO add back in when processing more than one
	//terminate children
	while(currentnum > 0){
		currentnum--;
		kill(pids[currentnum], SIGQUIT);
	} 
	//kill(0, SIGQUIT);
	printf("%d total processes were created.\n", totalProcesses);
	//code for freeing shared memory
	if(detachshared() == -1){
		return 1;
	}
	if(removeshared() == -1){
		return 1;
	}
	/* if(shmdt(shared) == -1){
		perror("failed to detach from shared memory");
		return 1;
	}
	if(shmctl(shmid, IPC_RMID, NULL) == -1){
		perror("failed to delete shared memory");
		return 1;
	} */
	
	//delete message queue
	if(deletequeue() == -1){
		return 1;
	}
	//struct msqid_ds *buf;
	/* if(msgctl(msqid, IPC_RMID, buf) == -1){
		perror("msgctl: remove queue failed.");
		return 1;
	} */
	return 0;
}