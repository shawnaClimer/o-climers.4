//used for max num processes
#define MAXQUEUE	18
#define QUANTUM		10000 

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
	int startsec;
	int startns;
	int waitsec;
	int waitns;
} process_cb;