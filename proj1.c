
/*
 * Project 1: File Sharing System
 * Modern Networking Concepts, CSE 589, Spring 2014
 * ----------------------------
 * @author: Bartlomiej Karmilowicz
 * @created: 1st February
 * 
 * HOW-TO: Start of as a Server or Client on specified port
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <libgen.h>

/* Constants */
#define MAXARGS 8 		// the largest is DOWNLOAD up to 7
#define BACKLOG 10 		// max queue for listening
#define CHUNK 512	    // DEFINE CHUNK SIZE
#define MESSAGE 5000 	// Buffer Size for Messages
#define MAXCON	4       // Server + 3 Unique peers

/* tags for SELECT MANAGEMENT; */
#define ADD 		1
#define DELETE		2 

/* helper tags */
 #define CLOSED 	1234

/* message codes */
 #define REGISTER   5421
 #define ADD_IP_LIST 4800
 #define SEND_FILE   6991
 #define REQUEST_FILE 7653
 #define NOTIFICATION 4331


typedef int bool;
#define true 1
#define false 0


/* struct holding all information about this local host. */

struct localHost {
	bool isServer; 			 /* value indicating if SERVER || CLIENT */
	char ipaddr[INET6_ADDRSTRLEN];	 /* IP ADDRESS OF THIS PROCESS */
	char port[50];			 /* listening port OF THIS PROCESS */
	char hostname[250];		 /* hostname OF THIS PROCESS */
	bool isRegistered; 
};

/* struct holding all information about connected peer */
/* used in connections list and Server IP list.		   */

struct peer {
	int socket;
	char hostname[250];
	char port[50];
	char ipaddr[INET6_ADDRSTRLEN];	
	bool client;
	struct peer *next;
};

/* static definitions, used throughout the whole scope of program */

static struct localHost localInfo;				
static int serverSocket;	     /* main listening socket */
static fd_set master_set; 	     /*master file descriptor set*/
static int fdmax = 0; 	   	     /*max file desc. #*/
static int connectionsCount = 0;     /*number of connections for this host*/


/* List of connections established with this host */

struct peer *connections_list;
struct peer *head;

/* UP-TO-DATE copy of Server's IP LIST. */

struct peer *IP_LIST;
struct peer *ip_head;


/* Function declarations */

//Command Functions
void cmd_CutAll();
int cmd_creator();
int cmd_mainmenu();
int cmd_myip();
int cmd_port();
int cmd_exit();
int cmd_download(int argc, char* argv[]);
int cmd_upload(int argc, char* argv[]);
int cmd_connect(int argc, char* argv[]);
int cmd_register(int argc, char* argv[]);
int cmd_list();
int cmd_terminate(int argc, char* argv[]);

//Connections List API
void addToConnections(char *hostName, char *hostPort, char *hostIP, int socketfd);
int removeFromConnections(int socketfd);
int getSocketFromList(int id);
struct peer* findConnection(int socketfd);
int findID(int socketfd);

//Server Ip List API
void DESTROY_IPLIST();
void SAVE_IPLIST(char *hostName, char *hostPort, char *hostIP);
void PRINT_IPLIST();
void sendIPLIST();

//Operations
static int command_execute(char *command);
int handle_command(int socket);
int receiveFile(int socket, char * filename, char * file_len, char * chunks_num);
int acceptConnection();
int setupConnection();
int getHostInfo();
int selector();
int updateSelect(int operation, int socketfd, fd_set* fdset, int* fdmax);




/*
 * Method to get the port number
 * @arg line struct sockaddr (can be typecasted)
 * @return port number
 */

in_port_t get_in_port(struct sockaddr *sa)
{
 	return (((struct sockaddr_in*)sa)->sin_port);
 	//printf("port is %d\n",ntohs(get_in_port((struct sockaddr *)p->ai_addr)));
}

/*
 * Method to get the port number
 * @arg line struct sockaddr (can be typecasted)
 * @return pointer to addr field
 */

void *get_in_addr(struct sockaddr *sa)
{
	return &(((struct sockaddr_in*)sa)->sin_addr);
}


/* 
 * Methods ensuring safe sending 
 * @arg socket, buffer with message, size of buffer
 * @return total bytes send
 */

int sendall(int s, char *buf, size_t len){
	int total = 0;
	int bytesleft = len;
	int n;

	while(total < len){
		n = send(s, buf+total, bytesleft, 0);
		//printf("SEND %d\n",n);
		if(n == -1){ break; }
		total += n;
		bytesleft -= n;
	}
	len = total;
	return total;
}

/* 
 * Methods ensuring safe receive 
 * @arg socket, buffer with message, size of buffer
 * @return total bytes received
 */

int recvall(int s, char *buf, size_t len){

	int total = 0;
	int bytesleft = len;
	int n;

	while(total < len){
		n = recv(s, buf+total, bytesleft, 0);
		//printf("RECEIVED from: %d\n", s);
		if(n == -1){ break; }
		if(n == 0){ return CLOSED;}
		total += n;
		bytesleft -= n;
	}
	
	len = total;

	return total;
}


/************** TIME FUNCTIONS *********************/ 

/* 
 * Method returning current time
 * @arg void
 * @return pointer to buffer with current time
 */

const char * getTime(){
	time_t timer;
    char buffer[25];
    struct tm* time_info;

    time(&timer);
    time_info = localtime(&timer);

    strftime(buffer, 25, "%F %H:%M:%S", time_info);
    char *result;
    result = malloc(strlen(buffer));
    sprintf(result, "%s", buffer);

   	return result;
}

/* 
 * Method returning wall clock time
 * Used to record point in time (time measurement)
 * @arg pointer to struct timeval
 * @return wallclock time
 */

double timer(struct timeval *timevalue){
	gettimeofday(timevalue, NULL);
	return(timevalue->tv_sec + (timevalue->tv_usec / 1000000.0));
}


/**************  LISTS MANAGEMENT *********************/
/* API to connection's list and SERVER IP LIST */

/* 
 * Method adding new connection to connection list
 * @arg name of host, port number, ip address, socket
 * @return void
 */

void addToConnections(char *hostName, char *hostPort, char *hostIP, int socketfd)
{	
	printf("Updating connections list..\n");

	//create new node
	struct peer *lastNode;
	lastNode = (struct peer *)malloc(sizeof(struct peer));

	//initiliaze node with passed information
	strncpy(lastNode->hostname, hostName, sizeof(lastNode->hostname));
	strncpy(lastNode->port, hostPort, sizeof(lastNode->port));
	strncpy(lastNode->ipaddr, hostIP, sizeof(lastNode->ipaddr));
	lastNode->socket = socketfd;
	lastNode->next = NULL;
	
	//position the pointers
	if(head == NULL){
		connections_list = lastNode;
		head = connections_list;
	}else{
		connections_list->next = lastNode;
		connections_list = connections_list->next;
	}

	connectionsCount += 1;
	printf("CONNECTION COUNT: %d\n", connectionsCount);

}

/* 
 * Method removing new connection from connection list
 * @arg name of host, port number, ip address, socket
 * @return void
 */

int removeFromConnections(int socketfd)
{
	printf("Updating connections list..\n");

	struct peer *prev = NULL;
	struct peer *del = head;
	bool found;

	//find the connection
	while(del != NULL){
		if(del->socket == socketfd){
			found = true;
			break;
		}else{
			prev = del;
			del = del->next;
		}
	}

	if(del == NULL){
		printf("No such entry exists, deletion stopped\n");
		return -1;
	}

	//reposition pointers
	if(prev != NULL){
		prev->next = del->next;
	}else if(del == head){
		head = del->next;
	}
	
	struct peer *tmp = head;
	struct peer *newTail = NULL;
	while(tmp != NULL){
		newTail = tmp;
		tmp = tmp->next;
	}
	connections_list = newTail;
	connectionsCount -= 1;
	printf("Connection Count: %d\n", connectionsCount);
	if(strcmp(del->hostname, "timberlake.cse.buffalo.edu") == 0){
		localInfo.isRegistered = false;
		cmd_CutAll();
	}

	//destroy old struct
	free(del);
	del = NULL;

	return 0;
}

/* 
 * Method to find the connection
 * @arg socket
 * @return struct peer *
 */

struct peer* findConnection(int socketfd)
{
	struct peer *del = head;
	bool found = false;

	while(del != NULL){
		if(del->socket == socketfd){
			found = true;
			//printf("Found the value\n");
			break;
		}else{
			del = del->next;
		}
	}
	if(found){
		return del;
	}
	return NULL;
}

/* 
 * Method to find the ID of connection
 * @arg socket
 * @return ID 
 */

int findID(int socketfd)
{	
	int ID = 1;
	bool found = true;
	struct peer *del = head;

	while(del != NULL){
		if(del->socket == socketfd){
			found = true;
			//printf("Found the value\n");
			break;
		}else{
			del = del->next;
			ID += 1;
		}
	}

	if(found){
		return ID;
	}
	return -1;
}

/* 
 * Method to find the socket from connection ID
 * @arg ID of connection
 * @return socket 
 */

int getSocketFromList(int id)
{	
	struct peer *del = head;
	int result;
	int i;

	i = 1;

	while(i != id){
		del = del->next;
		i = i + 1;
	}

	if(del == NULL){
		printf("Connection with specified ID does not exist\n");
		return EINVAL;
	}

	if(strcmp(del->hostname, "timberlake.cse.buffalo.edu") == 0){
		localInfo.isRegistered = false;
	}

	result = del->socket;
	return result;
}

/* 
 * Method to destroy current Server Ip List
 * @arg void
 * @return void
 */

void DESTROY_IPLIST()
{
	struct peer *temp;

	while(ip_head != NULL){
		temp = ip_head;
		ip_head = ip_head->next;
		free(temp);
	}

	//reset
	ip_head = NULL;
	IP_LIST = NULL;
}

/* 
 * Method to save the entry in Server IP list
 * Similiar logic as in Connections List
 * @arg host name, host port, host ip address
 * @return void
 */

void SAVE_IPLIST(char *hostName, char *hostPort, char *hostIP)
{
	struct peer *lastNode;
	lastNode = (struct peer *)malloc(sizeof(struct peer));
	strncpy(lastNode->hostname, hostName, sizeof(lastNode->hostname));
	strncpy(lastNode->port, hostPort, sizeof(lastNode->port));
	strncpy(lastNode->ipaddr, hostIP, sizeof(lastNode->ipaddr));
	lastNode->next = NULL;

	if(ip_head == NULL){
		IP_LIST = lastNode;
		ip_head = IP_LIST;
	}else{
		IP_LIST->next = lastNode;
		IP_LIST = IP_LIST->next;
	}

}

/* 
 * Method to print out the Server IP list
 * @arg void
 * @return void
 */

void PRINT_IPLIST(){
	struct peer *temp = ip_head;
	printf("******************************************************\n");
	while(temp != NULL){
		printf("%s | %s | %s\n",temp->hostname, temp->ipaddr, temp->port);
		temp = temp->next;
	}
	printf("******************************************************\n");
}

/* 
 * Method used exclusively by server
 * Broadcasts an IP List to all connected hosts
 * @arg void
 * @return void
 */

void sendIPLIST()
{	
	if(!localInfo.isServer){
		printf("Only Server Boradcasts IP LIST\n");
		return;
	}

	if(head == NULL){
		//nothing to send
		return;
	}

	printf("Broadcasting IP LIST to peers..\n");

	//buffer that will hold entire connection list from server
	char ipList[MESSAGE];
	char* scanner = ipList;
	int nbytes;

	//add message header
	nbytes = sprintf(scanner, "%d ", ADD_IP_LIST);
	scanner += nbytes;

	struct peer *list = head;

	while(list != NULL){
		nbytes = sprintf(scanner, "%s %s %s ", list->hostname, list->ipaddr, list->port);
		scanner += nbytes;
		list = list->next;
	}

	//tag the end of the buffer
	sprintf(scanner, "EOF EOF EOF");

	//reset pointer over list for sending
	list = head;

	//send IP LIST buffer to every socket listed
	while(list != NULL){
		if(sendall(list->socket, ipList, sizeof(ipList)) == -1){
			perror("sendIPLIST: send ");
			return;
		}
		list = list->next;
	}

}

/* 
 * MENU AND COMMAND HANDLING SYSTEM
 * CREDIT: previous project (OS_161 development) in CSE_421 taught by 
 * Geoffrey Challen. kern/startup/menu.c 
 * will be reffered in credit section as: OS161 menu interface
 */

/* 
 * Command used to print out supported commands on host
 * @arg void
 * @return void
 */

int cmd_mainmenu()
{

	if(!localInfo.isServer){
		char *clientmenu = 
		"\n....................HELP MENU......................\n"
		"                    			 			    	\n"
		"[ MYIP ] Display IP of this process 			    \n"
		"[ CREATOR ] Display programmer ID   			    \n"
		"[ LIST ] Display current connections  			    \n"
		"[ EXIT ] Exit the process 			 			    \n"
		"[ TERMINATE <id> ] Close connection 			    \n"
		"[ REGISTER <ip> <localInfo.port> ] Registration on server    \n"
		"[ CONNECT <ip/localInfo.hostname> <localInfo.port> ] New TCP connection\n"
		"[ UPLOAD <id> <file> ] Upload file to host <id>	\n"
		"[ DOWNLOAD <id> <file> | | ] Download from hosts   \n"
		"...................................................\n";
		printf("%s\n",clientmenu);

	}else{

		char *servermenu = 
		"\n....................HELP MENU......................\n"
		"                    			 			    	\n"
		"[ MYIP ] Display IP of this process 			    \n"
		"[ CREATOR ] Display programmer ID   			    \n"
		"[ LIST ] Display current connections  			    \n"
		"[ EXIT ] Exit the process 			 			    \n"
		"[ TERMINATE <id> ] Close connection 			    \n"
		"...................................................\n";
		printf("%s\n",servermenu);
	}

	return 0;
}


/* 
 * Command used to print out IP address
 * @arg void
 * @return void
 */

int cmd_myip()
{
	printf("IP: %s\n", localInfo.ipaddr);
	return 0;
}

/* 
 * Command used to print out port number
 * @arg void
 * @return void
 */

int cmd_port()
{
	printf("PORT: %s\n", localInfo.port);
	return 0;
}

/* 
 * Command used to print out authors information
 * @arg void
 * @return void
 */

int cmd_creator()
{
	char *creator = 
	"\n======================================\n"
	"whoami: bartlomiej karmilowicz        \n"
	"address: bartlomi@buffalo.edu         \n"
	"UBITname: bartlomi                    \n"
	"======================================\n";

	printf("%s\n", creator);
	return 0;
}

/* 
 * Command used to exit the system
 * @arg void
 * @return void
 */

int cmd_exit()
{
	//cmd_CutAll();
	exit(1);
}

/* 
 * Command used to download files from remote hosts
 * @arg argument count, pointer to char * array
 * @return 0 success, errno fail
 */

int cmd_download(int argc, char* argv[])
{

	//check if eligible for download
	if(localInfo.isServer){
		printf("Command Not Avaiable on Server\n");
		return EINVAL;
	}

	if(argc < 3){
		printf("Peer ID and Filename required\n");
		return EINVAL;
	}

	printf("ARG COUNT %d\n", argc);

	if(argc % 2 == 0){
		printf("Provide connection ID and fileName for every host\n");
		return EINVAL;
	}

	int index = 1;
	char * filename;
	int socketfd;
	char message[MESSAGE];

	//Loop for every pair [ID, FILENAME]
	//send request for file to each of listed hosts
	while(index < argc){

		socketfd = getSocketFromList(atoi(argv[index]));
		if(socketfd == EINVAL){
			printf("Upload Failed.\n");
			return EINVAL;
		}

		filename = argv[index + 1];

		//REQUEST file from remote host 
		sprintf(message, "%d %s\n", REQUEST_FILE, filename);

		if(sendall(socketfd, message, sizeof(message)) == -1){
			perror("UPLOAD: message ");
			return -1;
		}

		index += 2;
	}

	return 0;
}

/* 
 * Command used to upload file to remote host
 * @arg argument count, pointer to char * array
 * @return 0 success, errno fail
 */

int cmd_upload(int argc, char* argv[])
{	

	if(localInfo.isServer){
		printf("Command Not Avaiable on Server\n");
		return EINVAL;
	}

	//check arguments size
	if(argc < 3){
		printf("Peer ID and Filename required\n");
		return EINVAL;
	}
	
	//get socket ID from list
	int socketfd = getSocketFromList(atoi(argv[1]));
	if(socketfd == EINVAL){
		printf("Upload Failed.\n");
		return EINVAL;
	}

	//declarations
	char chunk[CHUNK];
	char * filename;
	FILE *file;
	int numChunks;
	int result;
	int file_len;

	filename = argv[2];
	file = fopen (filename,"rb");

	if(file == NULL){
		perror("FILE: fopen ");
		return -1;
	}

	if ((result = fseek(file, 0, SEEK_END)) != 0){
		perror("FILE: fseek ");
		return result;
	}

	//FILE size and number of chunks needed
	file_len = ftell(file);
	numChunks = file_len / CHUNK;
	if(file_len % CHUNK != 0){
		numChunks += 1;
	}

	rewind(file);

	//prepare info message for targeted host about file.
	//SEND_FILE, FILE_NAME, FILE_SIZE, NumOfChunks

	char *bname;
    char *filePath = strdup(filename);
    bname = basename(filePath);

	char sendBuff[MESSAGE];
	sprintf(sendBuff, "%d %s %d %d\n", SEND_FILE, bname, file_len, numChunks);

	if(sendall(socketfd, sendBuff, sizeof(sendBuff)) == -1){
		perror("UPLOAD: message ");
		return;
	}
	
	int totallyRead = 0;
	int readBytes = 0;
	int totallySent = 0;

	struct timeval t0,t1;
	double start, finish;
	
	start = timer(&t0);

	while(totallyRead < file_len){
	    readBytes = fread(chunk, sizeof(char), sizeof(chunk), file);
	    totallyRead += readBytes;
	    if((totallySent += sendall(socketfd, chunk, readBytes)) == -1){
			perror("UPLOAD: sendingFile ");
		return;

		}
	}

	finish = timer(&t1);

	fclose(file);

	double txRate = (double)(totallySent * 8) / (finish - start);

	printf("\n");
	printf("***********************************************************\n");
	printf("UPLOAD COMPLETED %d%\n", ((totallySent/file_len) * 100));
	printf("FILE SIZE: %d Bytes Sent: %d CHUNKS: %d\n", file_len, totallySent, numChunks);
	printf("UPLOAD TIME: %fs\n", (finish - start)); 
	printf("UPLOAD RATE Tx: %f bits/s\n", txRate);
	printf("***********************************************************\n");
	printf("\n");

	return 0;

}

/* 
 * Command used to validate connection
 * @arg host name
 * @return 0 success, errno fail
 */

int validateConnection(char* HOSTNAME)
{
	//check for self connection
	if(strcmp(HOSTNAME, localInfo.hostname) == 0){
		printf("CONNECTION FAILED: You cannot self connect\n");
		return -1;
	}

	//check if server
	if(localInfo.isServer){
		printf("Only clients can make connections\n");
		return -1;
	}
	
	//check if registered
	if(!localInfo.isRegistered){
		printf("CONNECTION REJECTED: In order to make connections, register on server\n");
		return -1;
	}
	//check if max connection number is met
	if(connectionsCount == MAXCON){
		printf("No more connections allowed. MAX 4\n");
		return -1;
	}

	//check if already connected
	struct peer *scanner = head;
	while(scanner != NULL){
		if(strcmp(scanner->hostname, HOSTNAME) == 0){
			printf("Already Connected to this Peer\n");
			return -1;
		}
		scanner = scanner->next;
	}

	//validate with SERVER IP LIST
	scanner = ip_head;
	bool found = false;
	while(scanner != NULL){
		if(strcmp(scanner->hostname, HOSTNAME) == 0){
			found = true;
		}
		scanner = scanner->next;
	}

	if(!found){
		printf("PEER you are trying to connect is not registered with server. Try later..\n");
		return -1;
	}
	return 0;
}

/* 
 * Command used to connect to remote host
 * @arg argument count, pointer to char * array
 * @return 0 success, errno fail
 */

int cmd_connect(int argc, char* argv[]){
	
	//check the arguments number
	if(argc != 3){
		printf("Please Provide IP Address and Port Of Peer\n");
		return EINVAL;
	}
	
	struct addrinfo hints, *servinfo;
	int result;
	int peerConnection;
	char ipaddr[INET6_ADDRSTRLEN];

	char* peer = argv[1];

    //safe PORT copying
    int pp = atoi(argv[2]);
    char peerPort[50];
    sprintf(peerPort, "%d", pp);

    //validate if connection allowable
    result = validateConnection(peer);
    
    if(result == -1){
    	return EHOSTUNREACH;
    }

    memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	//Connection process initiated
	if((result = getaddrinfo(peer, peerPort, &hints, &servinfo)) < 0){
		printf("getaddrinfo error:\n");
		return result;
	}

	if((peerConnection = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) < 0){
		perror("connect socket :");
		return peerConnection;
	}


	if(connect(peerConnection, servinfo->ai_addr, servinfo->ai_addrlen) < 0){
		perror("connect connect :");
		return -1;
	}

	//printout info message about remote host
	inet_ntop(servinfo->ai_family, get_in_addr((struct sockaddr *)servinfo->ai_addr), ipaddr, sizeof(ipaddr));
	printf("\nCLIENT CONNECTING TO %s\n", ipaddr);

	//prepare message containing listening port number and hostnamt
	char sendBuff[200];
	sprintf(sendBuff, "%s\n", localInfo.port);

	if(sendall(peerConnection, sendBuff, sizeof(sendBuff)) == -1){
		perror("Register: send ");
		close(peerConnection);
		return -1;
	}

	//update connections list 
	addToConnections(peer, peerPort, ipaddr, peerConnection);

	//update descriptor in FD_SET
	updateSelect(ADD, peerConnection, &master_set, &fdmax);

	printf("\n");

	return 0;
}

/* 
 * Command used to register host with server
 * @arg argument count, pointer to char * array
 * @return 0 success, errno fail
 */

int cmd_register(int argc, char* argv[])
{

	if(localInfo.isServer){
		printf("Only Clients Can Register!\n");
		return EOPNOTSUPP;
	}

	if(argc != 3){
		printf("Please Provide IP Address and Port Of Server\n");
		return EINVAL;
	}

	if(localInfo.isRegistered){
		printf("Already Registered!\n");
		return EACCES;
	}

	struct addrinfo hints, *servinfo;
	int result;
	int serverConnection;
	char s[INET6_ADDRSTRLEN];

	//extract arguments
	char* server = argv[1];
    //safe PORT copying
    int pp = atoi(argv[2]);
    char serverPort[50];
    sprintf(serverPort, "%d", pp);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if((result = getaddrinfo(server, serverPort, &hints, &servinfo)) < 0){
		printf("getaddrinfo error:\n");
		return result;
	}

	if((serverConnection = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) < 0){
		perror("register socket :");
		return serverConnection;
	}


	if(connect(serverConnection, servinfo->ai_addr, servinfo->ai_addrlen) < 0){
		perror("register connect :");
		return -1;
	}

	inet_ntop(servinfo->ai_family, get_in_addr((struct sockaddr *)servinfo->ai_addr), s, sizeof(s));
	printf("\nCLIENT CONNECTING TO %s\n", s);

	//prepare message containing listening port number
	char sendBuff[200];
	sprintf(sendBuff, "%s\n", localInfo.port);

	if(sendall(serverConnection, sendBuff, sizeof(sendBuff)) == -1){
		perror("Register: send ");
		close(serverConnection);
		return -1;
	}
	
	memset(&sendBuff[0], 0, sizeof(sendBuff));
	if((result = recvall(serverConnection, sendBuff, sizeof(sendBuff))) == -1){
		printf("handle_command recvall failed\n");
		return -1;
	}

	localInfo.isRegistered = true;

	//print message from server
	printf("%s\n", sendBuff);

	//update connections list 
	addToConnections(server, serverPort, s, serverConnection);

	//update descriptor in FD_SET
	updateSelect(ADD, serverConnection, &master_set, &fdmax);

	printf("\n");

	freeaddrinfo(servinfo);

	return 0;
}

/* 
 * Command used to print out list of current connections
 * @arg void
 * @return 0 success, errno fail
 */

int cmd_list()
{
	struct peer *headcpy = head;
	int i = 1;

	printf("\nID | HOSTNAME | IP | PORT | socket\n");
	printf("***********************************************************\n");

	while(headcpy != NULL){
		printf("%d. %s | %s | %s | %d \n", i, headcpy->hostname, headcpy->ipaddr, headcpy->port, headcpy->socket);
		headcpy = headcpy->next;
		i = i + 1;
	}
	if(i == 1){
		printf("0 (ZERO) Established Connections... \n");
	}
	printf("***********************************************************\n");
	printf("\n");
	return 0;
}

/* 
 * Command used to terminate desired connection
 * @arg argument count, pointer to char * array
 * @return 0 success, errno fail
 */

int cmd_terminate(int argc, char* argv[])
{
	if(argc != 2){
		printf("Please provide Connection Socket\n");
		return EINVAL;
	}

	int socket = getSocketFromList(atoi(argv[1]));
	if (removeFromConnections(socket) != 0){
		return -1;
	}

	close(socket);
	updateSelect(DELETE, socket, &master_set, &fdmax);

	if(localInfo.isServer){
		sendIPLIST();
	}

	return 0;
}

/* 
 * Command used to terminate all connections
 * after connection to server is lost.
 * @arg void
 * @return void
 */

void cmd_CutAll()
{
	int i = 1;
	int socket;
	while(i <= connectionsCount){
		socket = getSocketFromList(i);
		removeFromConnections(socket);
		close(socket);
		updateSelect(DELETE, socket, &master_set, &fdmax);
		i += 1;
	}
	printf("Connection Lost to Server. Terminated all connections..\n");
}



/* 
 * COMMAND TABLE
 * Maps command to appropiate function.
 * CREDIT: OS161 menu interface
 */

static struct {
	const char *name;
	int (*func)(int nargs, char **args);
} command_table[] = {
	/* menus, info, help */
	{ "HELP",		cmd_mainmenu },
	{ "MYIP", cmd_myip},
	{ "CREATOR", cmd_creator},
	{ "LIST", cmd_list},
	{ "MYPORT", cmd_port},
	/* operation calls */
	{ "EXIT", cmd_exit},
	{ "TERMINATE", cmd_terminate},
	{ "REGISTER", cmd_register},
	{ "CONNECT", cmd_connect},
	{ "UPLOAD", cmd_upload},
	{ "DOWNLOAD", cmd_download},
	{ NULL, NULL }
};


/* 
 * Important piece of handling user inputted commands
 * function parses the input and stores it in array args word by word
 * Checks the first word against command table, on success executes mapped command
 * 
 * @arg command from stdin
 * @return 0 success, errno fail
 * CREDIT: OS161 menu interface
 */


static int command_execute(char *command)
{
	int i, result, nargs;
	char *args[20];
	char *token;
	char *rest;
	int k;
	nargs = 0;
	
	//tokanize the command
	for (token = strtok_r(command, " ", &rest); token != NULL;
		token = strtok_r(NULL, " ", &rest)) {
		if(nargs >= MAXARGS){
			return E2BIG;
		}
		args[nargs++] = token;
	}

	//remove '\n' from argument to match command_table
	int index = nargs - 1;
	token = args[index];
	int len = strlen(token);
	token[len-1] = '\0';
	args[index] = token;

	//recognize the command by matching against command table
	for (i=0; command_table[i].name; i++) {
		if (*command_table[i].name && !strcmp(args[0], command_table[i].name)) {
			result = command_table[i].func(nargs, args);
			return result;
		}

	}

	return EINVAL;
}


/* 
 * CONNECT TO GOOGLE server, use their dns look up the IP address of host
 * @arg void
 * @return 0 success, errno fail
 * CREDIT: method mentioned on the class blog
 * getsockname: 
 * http://stackoverflow.com/questions/20941027/getsockname-c-not-setting-value
 */

int getHostInfo()
{

	int sockfd;
	struct addrinfo hints, *res;
	int result;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET; /*check if we can use UNSPEC */
	hints.ai_socktype = SOCK_STREAM;

	//google server information
	if((result = getaddrinfo("8.8.8.8", "53", &hints, &res)) != 0){
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(result));
		return result;
	}

	if((sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == -1){
		perror("getHostInfo: socket ");
		return -1;
	}

	if((result = connect(sockfd, res->ai_addr, res->ai_addrlen)) == -1){
		perror("getHostInfo: connect ");
		close(sockfd);
		return result;
	}

	/* now the imlocalInfo.portant info */
	/* extract information */

	struct sockaddr_in local_host;
	socklen_t size = sizeof(local_host);

	if((result = getsockname(sockfd, (struct sockaddr*)&local_host, &size)) == -1){
		printf("GetSockName failed %s\n", strerror(EINVAL));
		close(sockfd);
		return result;
	}

	
	void *addr;
	addr = &(local_host.sin_addr);

	inet_ntop(local_host.sin_family, addr, localInfo.ipaddr, sizeof(localInfo.ipaddr));

	//rest of host info

	gethostname(localInfo.hostname, sizeof(localInfo.hostname));

	if(!(localInfo.isServer) && (strcmp(localInfo.hostname, "timberlake.cse.buffalo.edu") == 0)){
		printf("Cannot run client on timberlake. Reserved for Server. %s\n", strerror(EINVAL));
		return -1;
	}

	close(sockfd);
	freeaddrinfo(res);

	return 0;
	
}

/* 
 * Method for setting up MAIN connection for local socket
 * @arg void
 * @return 0 success, errno fail
 * CREDIT: BEEJ's tutorial page 28
 */

int setupConnection()
{
	struct addrinfo hints, *hostAddr;
	int result;
	int yes = 1;

	if((result = getHostInfo()) != 0){
		return -1;
	}
	
	//load up structures
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET; /*check if we can use UNSPEC */
	hints.ai_socktype = SOCK_STREAM;

	if((result = getaddrinfo(localInfo.ipaddr, localInfo.port, &hints, &hostAddr)) != 0){
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(result));
		return result;
	}

	if((serverSocket = socket(hostAddr->ai_family, hostAddr->ai_socktype, hostAddr->ai_protocol)) == -1){
		perror("client: socket ");
		return -1;
	}

	//ALLOW SOCKET TO BE REUASBLE
	if((result = setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))) == -1){
		perror("setsockopt");
		return result;
	}

	if((result = bind(serverSocket, hostAddr->ai_addr, hostAddr->ai_addrlen)) == -1){
		perror("bind");
		return result;
	}

	return 0;
}

/* 
 * Method used to manage FD_SET for select()
 * SUPPORTED OPPERATIONS: ADD, DELETE
 * @arg Operation Code, socket, fdset, pointer to fdmax
 * @return success 0, errno fail
 */

int updateSelect(int operation, int socketfd, fd_set* fdset, int* fdmax)
{
	
	if(fdmax == NULL){
		return -1;
	}

	switch(operation){
		case ADD :
			FD_SET(socketfd, fdset);
			//if socketfd  is bigger than fdmax, update accordingly
			if(socketfd > *fdmax){
				*fdmax = socketfd;
			//	printf("max fd updated to: %d\n", socketfd);
			}
		break;

		case DELETE :
			//printf("Operation REMOVING from set\n");
			FD_CLR(socketfd, fdset);
			if(socketfd == *fdmax){
				(*fdmax)--;
			}
			break;

		default :
			printf("INVALID ARGUMENT\n");
			return -1;
	}
	return 0;
}

/* 
 * Method for accepting incoming connection
 * @arg void
 * @return 0 success, errno fail
 * CREDIT: BEEJ's tutorial page 28
 */

int acceptConnection()
{
	printf("\n");
	int newConnection;
	struct sockaddr_storage remoteaddr; //client address
	socklen_t addrlen;
	int port;
	int result;

	addrlen = sizeof(remoteaddr);
	//accept connection
	newConnection = accept(serverSocket, (struct sockaddr *)&remoteaddr, &addrlen);
	
	if(newConnection == -1){
		perror("accept");
		return -1;
	}

	char remoteIP[INET6_ADDRSTRLEN];
	char remoteName[250];
	char remotePort[50];

	//printout remote host IP
	inet_ntop(remoteaddr.ss_family, get_in_addr((struct sockaddr *)&remoteaddr), remoteIP, INET6_ADDRSTRLEN);
	printf("\nNEW Connection Request From: %s\n", remoteIP);

	//extract message from host with its listening port
	char message[200];
	if((result = recvall(newConnection, message, sizeof(message))) == -1){
		printf("acceptConnection: recvall failed \n");
		return -1;
	}

	//resolve IP to a name
	char remoteHost[300];
	result = getnameinfo((struct sockaddr*)&remoteaddr, sizeof(remoteaddr), remoteHost, sizeof(remoteHost), NULL, 0, 0);
	if(result != 0){
		printf("acceptConnection: getnameinfo error\n");
		return -1;
	}

	sscanf(message, "%s", remotePort);
	printf("Connection from %s accepted\n", remoteHost);
	addToConnections(remoteHost, remotePort, remoteIP, newConnection);

	//update FD_SET with new Socket
	if(updateSelect(ADD, newConnection, &master_set, &fdmax) != 0){
		printf("acceptConnection: updateSelect failed\n");
		return -1;
	}

	//reuse message buffer and send confirmation
	if(localInfo.isServer){
		memset(&message[0], 0, sizeof(message));
		sprintf(message, "Registration Successful.");
		if(sendall(newConnection, message, sizeof(message)) == -1){
			perror("acceptConnection: send");
			return -1;
		}
	}
	//broadcast IP list
	if(localInfo.isServer){
		sendIPLIST();
	}

	printf("\n");

	return 0;
}

/* 
 * Method for receiving file chunks
 * @arg socket, file name, file size, number of chunks
 * @return 0 success, errno fail
 * CREDIT: 
 * http://stackoverflow.com/questions/8679547/send-large-files-over-socket-in-c 
 */

int receiveFile(int socket, char * filename, char * file_len, char * chunks_num)
{
	int len = atoi(file_len);
	int chunksNum = atoi(chunks_num);
	
	FILE *recvFile;

	//open file for writting
	recvFile = fopen(filename,"wb");
	if(recvFile == NULL){
		perror("receiveFile: fopen ");
		return -1;
	}

	char chunk[CHUNK];
	int bytes = 0;
	int writtenTotal = 0;
	int receivedTotal = 0;
	int expected = 0;
	int chunkCount = 0;

	if(len < CHUNK){
		expected = len;
	}else{
		expected = CHUNK;
	}

	struct timeval t0,t1;
	double start, finish;

	start = timer(&t0);

	while(true)	{

		if((bytes = recvall(socket, chunk, expected)) == -1){
			printf("handle_command recvall failed\n");
			return -1;
		}
		
		int written = fwrite(chunk, sizeof(char), bytes, recvFile);
		writtenTotal += written;
        receivedTotal += bytes;
        chunkCount++;

        fflush(stdout);

        if(writtenTotal >= len){
        //Downloading Completed
        	break;
        }

        if(len - writtenTotal < CHUNK){
        	expected = len - writtenTotal;
        }

	}	

	finish = timer(&t1);
	
	fclose(recvFile);

	double rxRate = (double)(receivedTotal * 8) / (finish - start);
	printf("\n");
	printf("***********************************************************\n");
	printf("DOWNLOAD COMPLETED %d%\n", ((receivedTotal / len) * 100));
	printf("FILE SIZE: %d Received: %d CHUNKS: %d out of %d\n", len, receivedTotal, chunkCount, chunksNum);
	printf("DOWNLOAD TIME: %fs\n", (finish - start)); 
	printf("DOWNLOAD RATE Rx: %f bits/s\n", rxRate);
	printf("***********************************************************\n");
	printf("\n");

	if(receivedTotal < len){
		printf("Downloading uncompleted.. Try again.\n");
		return ECOMM;
	}

	return 0;

}

/* 
 * Method for handling messages
 * @arg socket
 * @return 0 success, errno fail
 */

int handle_command(int socket)
{
	char message[MESSAGE];
	int result;
	int code;

	//receive message
	if((result = recvall(socket, message, sizeof(message))) == -1){
		printf("handle_command recvall failed\n");
		return -1;
	}

	//if result 0, close connection
	if(result == CLOSED){
		struct peer *closedSocket = findConnection(socket);
		printf("%s closed the connection\n", closedSocket->hostname);
		updateSelect(DELETE, socket, &master_set, &fdmax);
		removeFromConnections(socket);
		if(localInfo.isServer){
		//broadcast new IP list
			sendIPLIST();
		}
		return 0;
	}

	//printf("NEW MESSAGE: %s\n", message);
	
	//EXTRACTING MESSAGE 
	char* newptr = message;
	int nbytes;

	char hostName[200];
	char port[50];
	char ipaddr[INET6_ADDRSTRLEN];

	//extract header
	sscanf(newptr, "%d %n", &code, &nbytes);
	newptr += nbytes;

	//SERVER IP UPDATE
	switch(code) {

		case ADD_IP_LIST :
		{
			DESTROY_IPLIST();
			printf("\n%s UPDATED SERVER IP LIST:\n", getTime());
			while(true){
				//parse out the IP LIST
				sscanf(newptr, "%s %s %s %n", hostName, ipaddr, port, &nbytes);
				if(strcmp(hostName, "EOF") == 0) {break;}
				//printf("%s %s %s\n",hostName, ipaddr, port);
				SAVE_IPLIST(hostName, port, ipaddr);
				newptr += nbytes;
			}
			PRINT_IPLIST();
			printf("\n");
			break;
		}
		case SEND_FILE :
		{
			char filename[50];
			char file_len[50];
			char chunks_num[50];
			sscanf(newptr, "%s %s %s", filename, file_len, chunks_num);
			struct peer *sender = findConnection(socket);
			printf("DOWNLOADING FILE: %s FROM: %s\n", filename, sender->hostname);
			if ((result = receiveFile(socket, filename, file_len, chunks_num)) != 0){
				return result;
			}
			break;
		}
		case REQUEST_FILE : 
		{
			char filename[50];
			int connectionID;
			sscanf(newptr, "%s", filename);

			if((connectionID = findID(socket)) == -1){
				printf("REQUEST_FILE: connection does not exist\n");
				return EHOSTUNREACH;
			}

			char ID[20];
			sprintf(ID, "%d", connectionID);
			int argCount = 3;
			char *arguments[] = {"UPLOAD", ID, filename};

			if((result = cmd_upload(argCount, arguments)) != 0){
				printf("ERROR: Could not upload file.\n");
				char message[MESSAGE];
				sprintf(message, "%d %s\n",NOTIFICATION, "REQUEST FAILED: COULD NOT UPLOAD FILE!");
				sendall(socket, message, sizeof(message));
				return EHOSTUNREACH;
			}
			break;
		}
		case NOTIFICATION :
		{	
			struct peer *sender = findConnection(socket);
			printf("\n**** NOTIFICATION FROM: %s ****\n", sender->hostname);
			printf(">> %s\n", newptr);
			break;
		}
		default :
			printf("Invalid Message Code, cannot understand\n");
			return -1;
	}

	return 0;	
}

/*
 * FUNCTION with Main LOOP monitoring sockets stored in FD_SET
 * 
 * @arg void
 * @return 0 success, errno fail
 * CREDIT: 
 * http://publib.boulder.ibm.com/infocenter/iseries/v5r3/index.jsp?topic=%2Frzab6%2Frzab6xnonblock.htm
 * BEEJ's tutorial (God Bless him for this tutorial)
 */
 
int selector()
{
	//backup file descriptor set
	fd_set working_set; 
	int i = 0;

	//timeout values	
	struct timeval tv;
	tv.tv_sec = 2;
	tv.tv_usec = 500000;

	FD_ZERO(&master_set);
	FD_ZERO(&working_set);

	fdmax = serverSocket;
	
	updateSelect(ADD, serverSocket, &master_set, &fdmax);
	updateSelect(ADD, STDIN_FILENO, &master_set, &fdmax);

	while(true){
		//copy master to working set

		printf("\r%s:%s >> ", localInfo.hostname, localInfo.port); 
		fflush(stdout);
		
		working_set = master_set;
		//memcpy(&working_set, &master_set, sizeof(master_set));

		if(select(fdmax+1, &working_set, NULL, NULL, NULL) < 0){
			perror("select: ");
			return -1;
		}

		for(i = 0; i <= fdmax; i++){
			if(FD_ISSET(i, &working_set)){
				if(i == serverSocket){
					acceptConnection();
				}else if(i == STDIN_FILENO){
					char* command = NULL;
					size_t nbytes;
					int result;
					getline(&command, &nbytes, stdin);
					result = command_execute(command);
					if(result){
						printf("Menu command failed: %s\n", strerror(result));
					}
				}else{
					//pass socket requiring attention
					printf("Incoming Message..\n");
					handle_command(i);
				}
			}
		}
	}
	return -1;
}


/*
 * Program Initialization
 */

int main(int argc, char *argv[])
{

	int result;

	//check arguments
	if(argc != 3 || strlen(argv[1]) != 1 || strlen(argv[2]) != 4){
		printf("Please provide 2 arguments %s\n", strerror(EINVAL));
		return 1;
	}

	//extracting arguments 
	char *type = argv[1];

	//port number without /n char.
	int pp = atoi(argv[2]);
    char buffer[50];
    sprintf(buffer, "%d", pp);
	strncpy(localInfo.port, buffer, sizeof(localInfo.port));


	if(strcmp(type, "s") == 0){
		localInfo.isServer = true;
	}else if(strcmp(type, "c") == 0){
		localInfo.isServer = false;
	}else{
		printf("Please type 'c' for client OR 's' for server %s\n", strerror(EINVAL));
		return 1;
	} 

	//prepare the connection 
	if((result = setupConnection()) != 0){
		printf("Connection Initiliaziation failed.\n");
		return 1;
	}

	//listen on socket for incoming connections
	if(listen(serverSocket, BACKLOG) == -1){
		perror("listen");
		return -1;
	}

	if(localInfo.isServer){
		printf("\n********** Server Initialized **********\n");
	}else{
		printf("\n********** Client Initialized **********\n");
	}

	printf("TIME: %s\n", getTime());
	printf("hostname: %s\n", localInfo.hostname);
	printf("IP: %s port:%s\n",localInfo.ipaddr, localInfo.port);
	printf("****************************************\n\n");


	if(selector() == -1){
		printf("SELECT loop error Error\n");
	}

	return 0;
}
