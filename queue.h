#include <stdlib.h>


//initialize queue
void initqueue (int queue[]){
	int i;
	for(i = 0; i < MAXQUEUE; i++){
		queue[i] = 0;
	}
}
//pop queue 
int popqueue (int queue[]){
	int pid = 0;
	if(queue[0] != 0){
		pid = queue[0];
		int i;
		for(i = 1; i < MAXQUEUE; i++){
			//if(queue[i] != 0){
				queue[(i - 1)] = queue[i];
			//}
		}
		queue[(MAXQUEUE - 1)] = 0;
	}
	return pid;
}
//push queue
int pushqueue (int queue[], int pid){
	int putin = 0;
	int i;
	for(i = 0; i < MAXQUEUE; i++){
		if(queue[i] == 0){
			queue[i] = pid;
			putin = 1;
			break;
		}		
	}
	return putin;
}