#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <sys/fcntl.h>

#define MAX 10

typedef struct{
	char dst;
	int cost;
} LinkStateItem; // {<dst ID>, <cost>}

typedef struct{
	char ID;
	int seq;
	int length;
	int TTL; // time to live
	LinkStateItem LSItems[MAX];
} LSP; // {ID, seq, length, TTL, {<dst1>, <cost1>}, {<dst2>, <cost2>}...}

typedef struct{
	char dst;
	int cost;
	int srcPort;
	int dstPort;
} RoutingItem; // {<dst>, <cost>, <src port>, <dst port>}

typedef struct{
	RoutingItem routingItems[MAX];
	int length;
} RoutingTable; // {<routing item1>, <routing item2>...}

typedef struct{
	char src;
	char dst;
	int srcPort;
	int dstPort;
	int cost;
	int connectFD; // for connection
	int sockFD; // for connect or listen
	int connectFlag; // flag for connection
} Link;

typedef struct{
	char routerID;
	int linkCount;
	Link links[MAX];
	time_t sendTime;
	LSP myLSP;
	LSP receivedLSP[MAX];
	int LSPSeq;
	int LSPRecved;
	RoutingTable myTable;
} Router;

// get the init routing table from init file
void initRoutingTable(Router *router){
	RoutingTable *table;
	RoutingItem *item;
	table = &(router->myTable);
	table->length = router->linkCount;

	int i;
	for(i = 0; i < router->linkCount; i++){
		item = &(table->routingItems[i]);
		item->dst = router->links[i].dst;
		item->cost = router->links[i].cost;
		item->srcPort = router->links[i].srcPort;
		item->dstPort = router->links[i].dstPort;
	}
}

void printRoutingTable(Router *router, FILE *file, char *filename){
	file = fopen(filename, "a+");
	char tableEntry[256];
	time_t curTime;
	struct tm *ts;
	char timeBuff[32];
	curTime = time(NULL);
	ts = localtime(&curTime);
	// convert time to local time
	strftime(timeBuff, sizeof(timeBuff), "%b %d %H:%M:%S\n", ts);
	printf("%s", timeBuff);
	fwrite(timeBuff, sizeof(char), strlen(timeBuff), file);

	char *head = "Dst\tCost\tSrc port\tDst port\n";
	printf("%s", head);
	fwrite(head, sizeof(char), strlen(head), file);
	int i;
	for(i = 0; i < router->myTable.length; i++){
		sprintf(tableEntry, "%c\t%d\t%d\t\t%d\n", 
			router->myTable.routingItems[i].dst,
			router->myTable.routingItems[i].cost,
			router->myTable.routingItems[i].srcPort,
			router->myTable.routingItems[i].dstPort);
		printf("%s", tableEntry);
		fwrite(tableEntry, sizeof(char), strlen(tableEntry), file);
	}
	fclose(file);
}

// create the lsp of each router
void createLSP(Router *router){
	LSP *packet;
	LinkStateItem *item;
	packet = &(router->myLSP);
	packet->ID = router->routerID;
	packet->seq = router->LSPSeq;
	packet->length = router->linkCount;
	packet->TTL = MAX;

	int i;
	for(i = 0; i < router->linkCount; i++){
		item = &(packet->LSItems[i]);
		item->dst = router->links[i].dst;
		item->cost = router->links[i].cost;
	}
}

void printLSP(LSP *lsp, FILE *file, char *filename){
	file = fopen(filename, "a+");
	char lspStr[256];
	char lsItem[64];
	sprintf(lspStr, "ID: %c, seq: %d, length: %d, TTL: %d\n", 
		lsp->ID, lsp->seq, lsp->length, lsp->TTL);
	int i;
	for(i = 0; i < lsp->length; i++){
		sprintf(lsItem, "%c: %d  ", lsp->LSItems[i].dst, lsp->LSItems[i].cost);
		strcat(lspStr, lsItem);
	}
	strcat(lspStr, "\n");
	printf("%s", lspStr);
	fwrite(lspStr, sizeof(char), strlen(lspStr), file);
	fclose(file);
}

// 0 for TTL <= 0 or router has received before, discard lsp
// 1 for new lsp or topo changes
int addLSP(Router *router, LSP *lsp){
	int i, j, exist = 0, flag = 0;
	// check if TTL <= 0
	if(lsp->TTL <= 0){
		flag = 0;
		return flag;
	}
	for(i = 0; i < router->LSPRecved; i++){
		// compare ID and if the same ID
		if(lsp->ID == router->receivedLSP[i].ID){
			// compare seq and if is new seq
			if(lsp->seq > router->receivedLSP[i].seq){
				// update lsp
				router->receivedLSP[i].seq = lsp->seq;
				flag = 1;
				// compare each LS items in lsp
				for(j = 0; j < lsp->length; j++){
					// if not the same dst
					if(lsp->LSItems[j].dst != router->receivedLSP[i].LSItems[j].dst){
						router->receivedLSP[i].LSItems[j].dst = lsp->LSItems[j].dst;
						flag = 1;
						} // end if of same ls item dst
					// if the cost changed
					if(lsp->LSItems[j].cost != router->receivedLSP[i].LSItems[j].cost){
						router->receivedLSP[i].LSItems[j].cost = lsp->LSItems[j].cost;
						flag = 1;
					} // end if of same ls item cost			
				} // end for loop of each item in lsp
			} //end if of most recent
			exist = 1;
		} // end if of same lsp ID
	}

	// if it is a new lsp, add it to receivedLSP[]
	if(exist == 0){
		int index = router->LSPRecved;
		router->receivedLSP[index].ID = lsp->ID;
		router->receivedLSP[index].seq = lsp->seq;
		router->receivedLSP[index].length = lsp->length;
		router->receivedLSP[index].TTL = lsp->TTL;
		for(i = 0; i < lsp->length; i++){
			router->receivedLSP[index].LSItems[i].dst = lsp->LSItems[i].dst;
			router->receivedLSP[index].LSItems[i].cost = lsp->LSItems[i].cost;
		}
		router->LSPRecved++;
		flag = 1;
	}

	return flag;
}

// 0 for routing table unchanged
// 1 for routing table changed
int changeRoutingTable(Router *router, LSP *lsp){
	RoutingTable *table = &(router->myTable);
	int rawCost = 9999, rawSrcPort, rawDstPort;
	int newCost;
	int i, j, exist = 0, flag = 0; 
	// get the raw cost and ports to the lsp ID
	for(i = 0; i < table->length; i++){
		if(lsp->ID == table->routingItems[i].dst){
			rawCost = table->routingItems[i].cost;
			rawSrcPort = table->routingItems[i].srcPort;
			rawDstPort = table->routingItems[i].dstPort;
		}
	}
	// scan the lsp and routing table to do dijkstra algo
	for(i = 0; i < lsp->length; i++){
		exist = 0;
		for(j = 0; j < table->length; j++){
			if(lsp->LSItems[i].dst == table->routingItems[j].dst){
				// dst already in routing table
				exist = 1;
				newCost = rawCost + lsp->LSItems[i].cost;
				// if the new cost and raw cost are same
				if(newCost == table->routingItems[j].cost){
					// choose the lower ID, which is also the lower dst port
					if(table->routingItems[j].dstPort > rawDstPort){
						table->routingItems[j].srcPort = rawSrcPort;
						table->routingItems[j].dstPort = rawDstPort;
						flag = 1;
					}
				} // end if of same cost

				// if we find lower cost
				if(newCost < table->routingItems[j].cost){
					table->routingItems[j].cost = newCost;
					table->routingItems[j].srcPort = rawSrcPort;
					table->routingItems[j].dstPort = rawDstPort;
					flag = 1;
				} // end if of lower cost
			}
			// avoid A-B-A
			if(lsp->LSItems[i].dst == router->routerID){
				exist = 1;
			}
		}
		// add this lsp to routing table
		if(exist == 0){
			table->routingItems[table->length].dst = lsp->LSItems[i].dst;
			table->routingItems[table->length].cost = rawCost + lsp->LSItems[i].cost;
			table->routingItems[table->length].srcPort = rawSrcPort;
			table->routingItems[table->length].dstPort = rawDstPort;
			table->length++;
			flag = 1;
		}
	}

	return flag;
}

// ./routed_LS <routerID> <logFile> <initFile>
int main(int argc, char const *argv[]){
	const char *filename = argv[3];
	FILE *initFile = fopen(filename, "r");
	char line[256];
	char *delim = ",<>\n";
	int nbytes;
	int recvFlag;

	const char *ID = argv[1];
	Router router;
	router.routerID = ID[0];
	router.linkCount = 0;

	// configure router from the inti file
	while(fgets(line, sizeof(line), initFile)){
		char *pch = strtok(line, delim);
		if(pch[0] == router.routerID){
			Link link;
			link.src = pch[0];
			pch = strtok(NULL, delim);
			link.srcPort = atoi(pch);
			pch = strtok(NULL, delim);
			link.dst = pch[0];
			pch = strtok(NULL, delim);
			link.dstPort = atoi(pch);
			pch = strtok(NULL, delim);
			link.cost = atoi(pch);
			router.links[router.linkCount] = link;
			router.linkCount++;
		}
	}
	fclose(initFile);

	// open log file
	const char *logname = argv[2];
	FILE *logFile;

	//init and print routing table
	initRoutingTable(&router);
	printRoutingTable(&router, logFile, logname);
	// creat LSP and recv buffer
	router.LSPSeq = 0;
	router.LSPRecved = 0;
	createLSP(&router);
	LSP tmpRecv;

	// create socket and set up sockaddr
	struct sockaddr_in servAddr[router.linkCount];
	struct sockaddr_in clieAddr[router.linkCount];
	int i;
	for(i = 0; i < router.linkCount; i++){
		router.links[i].sockFD = socket(AF_INET, SOCK_STREAM, 0);
		if(router.links[i].sockFD < 0){
			perror("socket creation");
			exit(1);
		}

		bzero(&servAddr[i], sizeof(servAddr[i]));
		servAddr[i].sin_family = AF_INET;
		servAddr[i].sin_addr.s_addr = INADDR_ANY;
		servAddr[i].sin_port = htons(router.links[i].srcPort);

		bzero(&clieAddr[i], sizeof(clieAddr[i]));
		clieAddr[i].sin_family = AF_INET;
		clieAddr[i].sin_addr.s_addr = INADDR_ANY;
		clieAddr[i].sin_port = htons(router.links[i].dstPort);

		router.links[i].connectFlag = 0;
	}

	// try to connect, if failure, create new socket for listen
	for(i = 0; i < router.linkCount; i++){
		// if connected
		if(connect(router.links[i].sockFD, (struct sockaddr *)&clieAddr[i], 
			sizeof(clieAddr[i])) == 0){
			router.links[i].connectFlag = 1;
			// get connected socket
			router.links[i].connectFD = router.links[i].sockFD;
		}
		// if connect failed
		else{
			router.links[i].connectFlag = 0;
			// create listen socket
			router.links[i].sockFD = socket(AF_INET, SOCK_STREAM, 0);
			if(router.links[i].sockFD < 0){
				perror("socket creation");
				exit(1);
			}

			// bind socket with server port
			if(bind(router.links[i].sockFD, (struct sockaddr *)&servAddr[i], 
				sizeof(servAddr[i])) < 0){
				perror("bind socket");
				exit(1);
			}

			// set to non-block
			if(fcntl(router.links[i].sockFD, F_SETFL, O_NDELAY) < 0){
				perror("cannot set non-block\n");
				exit(1);
			}

			// listen on the port
			if(listen(router.links[i].sockFD, MAX) < 0){
				perror("listen on port");
				exit(1);
			}
		} // end else of connect failed
	} // end for

	// init router timer
	time_t curTime;
	time(&router.sendTime);

	while(1){
		// scan all port and accept connections
		for(i = 0; i < router.linkCount; i++){
			if(router.links[i].connectFlag == 0){
				router.links[i].connectFD = accept(router.links[i].sockFD, 
					NULL, sizeof(struct sockaddr_in));
				// if connect success
				if(router.links[i].connectFD > 0){
					router.links[i].connectFlag = 1;
					// set connectFD to non-block
					if(fcntl(router.links[i].connectFD, F_SETFL, O_NDELAY) < 0){
						perror("cannot set non-block\n");
					}
				} // end if of connect success
			}
		} // end for of scan all port and accept connections

		// check time and send data
		time(&curTime);
		if(difftime(curTime, router.sendTime) >= (double)5.0){
			router.sendTime = curTime;
			// update seq
			router.myLSP.seq++;
			for(i = 0; i < router.linkCount; i++){
				if(router.links[i].connectFlag == 1){
					nbytes = send(router.links[i].connectFD, &router.myLSP, sizeof(LSP), 0);
					if(nbytes < 0){
						perror("send");
					}
					// if nbytes > 0, send it
				} // end if of send
			} // end for of send data
		} // end if of difftime

		// recv data from all ports
		for(i = 0; i < router.linkCount; i++){
			if(router.links[i].connectFlag == 1){
				nbytes = recv(router.links[i].connectFD, &tmpRecv, sizeof(LSP), 0);
				if(nbytes > 0){
					// recv correctly
					recvFlag = addLSP(&router, &tmpRecv);
					// recv new lsp and need to forward it
					if(recvFlag > 0){		
						tmpRecv.TTL--;
						int j;
						for(j = 0; j < router.linkCount; j++){
							if(j != i && router.links[j].connectFlag == 1){
								nbytes = send(router.links[j].connectFD, &tmpRecv, sizeof(LSP), 0);
								if(nbytes < 0){
									perror("send");
								}
								// if nbytes > 0, forward it
							}
						} // end for loop of forward

						if(changeRoutingTable(&router, &tmpRecv)){
							// print routing table and the lsp causing table changed
							printRoutingTable(&router, logFile, logname);
							printLSP(&tmpRecv, logFile, logname);
							// print the source of the lsp
							char lspSrc[64];
							sprintf(lspSrc, "This LSP came from %c\n", router.links[i].dst);
							printf("%s", lspSrc);
							// print the source to log file
							logFile = fopen(logname, "a+");
							fwrite(lspSrc, sizeof(char), strlen(lspSrc), logFile);
							fclose(logFile);
						}
					} // end if of recvFlag > 0
				} // end if of recv data correctly
			}
		} // end for of recv data
	} // end while(1)

	// close sockets
	for(i = 0; i < router.linkCount; i++){
		close(router.links[i].connectFD);
		close(router.links[i].sockFD);
	}

	fclose(logFile);
	return 0;
}
