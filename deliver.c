/*
** client.c -- a stream socket client demo
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdbool.h>

#define MAXDATASIZE 100 // max number of bytes we can get at once
#define MAX_NAME 500
#define MAX_DATA 1000

// Define the types of control packages
#define LOGIN 	1
#define LO_ACK 	2
#define LO_NAK	3
#define EXIT 	4
#define JOIN 	5
#define JN_ACK 	6
#define JN_NAK	7
#define LEAVE_SESS 	8
#define NEW_SESS	9
#define NS_ACK		10
#define MESSAGE 	11
#define QUERY		12
#define QU_ACK		13
#define INVITE 		14

#define STDIN 0

struct lab3message {
	unsigned int type;
	unsigned int size;
	unsigned char source[MAX_NAME];
	unsigned char data[MAX_DATA];
};

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	 if (sa->sa_family == AF_INET) {
	 	return &(((struct sockaddr_in*)sa)->sin_addr);
	 }
	 return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void login(char *client_ID, char *password, char *server_IP, char* server_Port);
void join(char *session_ID);
void leave();
void create(char *session_ID);
void list();
void quit();
void invite(char *client_ID);
void send_message(char *text);
char* parse_input(char* input);
char* pack_message(unsigned int type, unsigned int size, 
	char source[MAX_NAME], char data[MAX_DATA]);
void decode(char *message);

struct client{
	char client_ID[MAX_NAME];
	char password[MAX_NAME];
	bool connected;
	char session_ID[MAX_NAME];
	bool connected_session;
	int client_socket; 
};

struct client current_user;

int main(int argc, char *argv[]) {

	struct timeval tv;
	fd_set readfds, master;
	int numbytes;
	char buf[256];
	int max;

	current_user.connected = false;
	current_user.connected_session = false;
	current_user.client_socket = -1;

	tv.tv_sec = 20;
	tv.tv_usec = 500000;

	FD_ZERO(&readfds);
	FD_ZERO(&master);
	FD_SET(STDIN, &master);
	max = STDIN;
	char* command;
	while (1) {
		readfds = master;
		// collect data from stdin
		// don't care about writefds and exceptfds:
		select(max+1, &readfds, NULL, NULL, NULL);

		char input[1024] = {0};
		if (FD_ISSET(STDIN, &readfds)) {
			fgets(input, sizeof(input), stdin);

			int len = strlen(input) - 1;
			if (input[len] == '\n'){
				input[len] = '\0';
			}

			command = parse_input(input);
			if (command == NULL){
				printf("you fucked up\n");
			}
		} 

		if (current_user.connected){
			if (FD_ISSET(current_user.client_socket, &readfds)){

				numbytes = recv(current_user.client_socket, buf, sizeof(buf), 0);
				buf[numbytes] = '\0';

				decode(buf);
			} 
		}

		if (current_user.client_socket != -1 && current_user.connected && max != current_user.client_socket){
			FD_SET(current_user.client_socket, &master);
			max = current_user.client_socket;
		} else if (current_user.client_socket != -1 && !current_user.connected && max == current_user.client_socket){
			max = STDIN;
			FD_CLR(current_user.client_socket, &master);
		}
	}

	 
	return 0;
}

// parse the commands
char* parse_input(char* input){
	int length, nargs;
	int valid = 1;
	int invalid = 0;
	char* command="valid";
	char* fail = NULL;

	const char s[2] = " ";
	char *subString;
	char **args = malloc(sizeof(char*) * 4);
	nargs = 0;

	length = strlen(input);

	if (length >= 1){
		if (input[0] == '/'){
			// recieved a command
			subString = strtok(input, s);



			// LOGIN
			if (strcmp(subString, "/login") == 0){
				// get the sub arguments
				subString = strtok(NULL, s);
				while (subString != NULL && nargs < 4){
					
					args[nargs] = (char *)malloc(strlen(subString) + 1);
					strcpy(args[nargs], subString);
					nargs++;
					subString = strtok(NULL, s);
				}

				if (subString != NULL){
					printf("Too many arguments\n");
					return fail;
				} else if (nargs != 4) {
					printf("Too few arguments, nargs is %d\n", nargs);
					return fail;
				}

				login(args[0], args[1], args[2], args[3]);


			// LOGOUT
			} else if (!strcmp(subString, "/logout")){
				if (current_user.connected){
					char message[100] = "15:0:";
					strcat(message, current_user.client_ID);
					send(current_user.client_socket, message, strlen(message), 0);
					close(current_user.client_socket);
					current_user.connected = false;
				} else {
					printf("not logged in!\n");
				}


			// JOIN
			} else if (!strcmp(subString, "/joinsession")){
				if (!current_user.connected){
					printf("Login before joining a session\n");
					return fail;
				}
				if (current_user.connected_session == true){
					printf("Already connected! Please leave the current session\n");
					return fail;
				}
				// get the sub arguments
				subString = strtok(NULL, s);
				if (subString != NULL && nargs < 1){
					
					args[nargs] = (char *)malloc(strlen(subString) + 1);
					strcpy(args[nargs], subString);
					nargs++;
					subString = strtok(NULL, s);
				}

				if (subString != NULL){
					printf("Too many arguments\n");
					return fail;
				} else if (nargs != 1){
					printf("Too few arguments\n");
				}

				join(args[0]);


			} else if (!strcmp(subString, "/leavesession")){
				if (!current_user.connected){
					printf("Login please\n");
					return fail;
				}
				if (current_user.connected_session != true){
					printf("Not currently in a session\n");
					return fail;
				}

				leave();

			} else if (!strcmp(subString, "/createsession")){
				if (!current_user.connected){
					printf("Login before creating a session\n");
					return fail;
				}
				// get the sub arguments
				subString = strtok(NULL, s);
				if (subString != NULL && nargs < 1){
					
					args[nargs] = (char *)malloc(strlen(subString) + 1);
					strcpy(args[nargs], subString);

					nargs++;
					subString = strtok(NULL, s);
				}

				if (subString != NULL){
					printf("Too many arguments\n");
					return fail;
				}

				create(args[0]);


			} else if (!strcmp(subString, "/list")){
				if (!current_user.connected){
					printf("Login please\n");
					return fail;
				}
				list();

			} else if (!strcmp(subString, "/quit")){
				quit();
			} else if (!strcmp(subString, "/invite")){
				if (!current_user.connected){
					printf("Login before inviting someone to a session\n");
					return fail;
				}

				if (current_user.connected_session == false){
					printf("Please join a session to invite users\n");
					return fail;
				}
				// get the sub arguments
				subString = strtok(NULL, s);
				if (subString != NULL && nargs < 1){
					
					args[nargs] = (char *)malloc(strlen(subString) + 1);
					strcpy(args[nargs], subString);
					nargs++;
					subString = strtok(NULL, s);
				}

				if (subString != NULL){
					printf("Too many arguments\n");
					return fail;
				} else if (nargs != 1){
					printf("Too few arguments\n");
				}

				invite(args[0]);


			} else {
				printf("Invalid command\n");
			}

			
		} else {
			// received a message for the chat
			send_message(input);
		}
	}

	return command;
}


void login(char *client_ID, char *password, char *server_IP, char* server_Port){
	int sockfd, numbytes;
	char buf[MAXDATASIZE];
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];
	char *data;
	char *message;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	 
	if ((rv = getaddrinfo(server_IP, server_Port, &hints, &servinfo)) != 0) {
	 	fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
	 	current_user.client_socket = -1;
	 	return;
	}

	 // loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
	 	if ((sockfd = socket(p->ai_family, p->ai_socktype,
	 		p->ai_protocol)) == -1) {

	 		perror("client: socket");
	 		continue;
	 	}
	 	if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
	 		close(sockfd);
	 		perror("client: connect");
	 		continue;
	 	}
	 	break;
	}

	if (p == NULL) {
	 	fprintf(stderr, "client: failed to connect\n");
	 	current_user.client_socket = -1;
	 	return;
	}

	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
	s, sizeof s);
	 
	printf("client: connecting to %s\n", s);
	 
	freeaddrinfo(servinfo); // all done with this structure
	int bytes_sent;

	data = malloc(strlen(client_ID) + strlen(password) + 2);
	strcpy(data, client_ID);
	strcat(data, " ");
	strcat(data, password);

	message = pack_message(LOGIN, strlen(data), client_ID, data);
	
	// send the login request
	bytes_sent = send(sockfd, message, strlen(message), 0);

	strcpy(current_user.client_ID, client_ID);
	strcpy(current_user.password, password);
	current_user.client_socket = sockfd;
	current_user.connected = true;

}


void join(char *session_ID){
	char *message;
	int bytes_sent;

	message = pack_message(JOIN, strlen(session_ID), current_user.client_ID, session_ID);
	strcpy(current_user.session_ID, session_ID);

	bytes_sent = send(current_user.client_socket, message, strlen(message), 0);

	current_user.connected_session = true;
}


void leave(){
	char *message;
	int bytes_sent;
	char text[5] = "leave";
	printf("trying to leave\n");

	current_user.connected_session = false;
	memset(current_user.session_ID, 0, sizeof(current_user.session_ID));

	message = pack_message(LEAVE_SESS, strlen(text), current_user.client_ID, text);

	bytes_sent = send(current_user.client_socket, message, strlen(message), 0);
}

void create(char *session_ID){
	char *message;
	int bytes_sent;

	message = pack_message(NEW_SESS, strlen(session_ID), current_user.client_ID, session_ID);
	strcpy(current_user.session_ID, session_ID);

	bytes_sent = send(current_user.client_socket, message, strlen(message), 0);

	current_user.connected_session = true;
}

void list(){
	char *message;
	int bytes_sent;
	char text[5] = "list";

	message = pack_message(QUERY, strlen(text), current_user.client_ID, text);

	bytes_sent = send(current_user.client_socket, message, strlen(message), 0);
}

void quit(){
	close(current_user.client_socket);
	current_user.client_socket = -1;
	current_user.connected = false;
	current_user.connected_session = false;
	printf("Goodbye\n");
	exit(1);
}

void invite(char *client_ID){
	char *message;
	char *invitation = malloc(sizeof(current_user.session_ID)
	 + sizeof(client_ID) + 2);
	strcpy(invitation, current_user.session_ID);
	strcat(invitation, ":");
	strcat(invitation, client_ID);
	int bytes_sent;

	message = pack_message(INVITE, strlen(invitation), current_user.client_ID, invitation);

	bytes_sent = send(current_user.client_socket, message, strlen(message), 0);
}

void send_message(char *text) {
	char *message = pack_message(MESSAGE, strlen(text), current_user.client_ID, text);

	int bytes_sent;
	// Check the client ID
	bytes_sent = send(current_user.client_socket, message, strlen(message), 0);
}

char* pack_message(unsigned int type, unsigned int size,
 char source[MAX_NAME], char data[MAX_DATA]){

	char temp[20];
	char *output = malloc(sizeof(char)*5000);
	
	sprintf(temp, "%u", type);
	strcpy(output, temp);
	strcat(output, ":");

	sprintf(temp, "%u", size);
	strcat(output, temp);
	strcat(output, ":");

	strcat(output, source);
	strcat(output, ":");

	strcat(output, data);

	return output;
}

void decode(char *message){
	int msg_Type;
	char *string;
	char colon[2] = ":";
	char space[2] = " ";
	int length;
	int count = 0;
	char *msgChunk;
	char *data;
	char *client_ID;
	char *password;
	char *invited_ID;
	char *session_ID;
	fd_set response;
	char input [50];

	struct timeval tv;
	tv.tv_sec = 2;
	tv.tv_usec = 500000;

	char *msg_cpy = malloc(strlen(message));
	strcpy(msg_cpy, message);

	string = strtok(msg_cpy, colon);
	
	while(string != NULL && count < 4){

		switch (count){
			case 0:
				msg_Type = atoi(string);
			break;
			case 1:
				length = atoi(string);
			break;
			case 2:
				client_ID = malloc(sizeof(string));
				strcpy(client_ID, string);
			break;
			case 3:
				data = malloc(sizeof(string));
				strcpy(data, string);
	
			break;
		}
		string = strtok(NULL, colon);
		count++;
	}
	int session = 0;
	switch(msg_Type){
		case LO_ACK:
			printf("Login successful!\n");
		break;
		case LO_NAK:
			printf("Login failed: %s\n", data);
			current_user.client_socket = -1;
			current_user.connected = false;
		break;
		case JN_ACK:
			printf("Joined session %s\n", data);
		break;
		case JN_NAK:
			printf("Join failed:  %s\n", data);
			current_user.connected_session = false;
			memset(current_user.session_ID, 0, sizeof(current_user.session_ID));
		break;
		case NS_ACK:
			printf("Created session %s\n", data);
		break;
		case QU_ACK:
			printf("Users and sessions:\n");
			if (length > 0) {
				count = 1;
				printf("   Active users:\n");
				printf("       %s\n", data);
				while (count < length){
					// string = (NULL, colon);
					printf("       %s\n", string);
					count++;
				}
				session++;
				string = strtok(NULL, colon);
			} else {
				printf("   session_ID: %s\n", data);
			}


			while(string != NULL){
				if ((session % 2) == 0){
					printf("       Users: %s\n", string);
					session++;
				} else {
					printf("   session_ID: %s\n", string);
					session++;
				}
				string = strtok(NULL, colon);
			}

		break;
		case MESSAGE:
			printf("%s: %s\n", client_ID, data);
		break;
		case INVITE:
			FD_ZERO(&response);
			FD_SET(STDIN, &response);

			session_ID = malloc(sizeof(data));
			strcpy(session_ID, data);

			invited_ID = malloc(sizeof(string));
			strcpy(invited_ID, string);

			printf("You've been invited to join session '%s' by %s\n",
				session_ID, client_ID);
			if (current_user.connected_session == true){
				printf("type '/accept' to leave current session and join and '/reject' to decline\n");
			} else {
				printf("type '/accept' to join and '/reject' to decline\n");
			}

			// collect data from stdin
			// don't care about writefds and exceptfds:
			select(STDIN+1, &response, NULL, NULL, NULL);

			if (FD_ISSET(STDIN, &response)) {
				fgets(input, sizeof(input), stdin);

				int len = strlen(input) - 1;
				if (input[len] == '\n'){
					input[len] = '\0';
				}
				
			} 

			if (strcmp(input, "/accept") == 0){
				if (current_user.connected_session){
					leave();
				}
				join(session_ID);
			} else if (strcmp(input, "/reject") == 0){
				printf("Resonse rejected\n");
				
			} else {
				printf("Invalid response, rejecting invitation\n");
			}
			

		break;

	}
}























