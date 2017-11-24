/*
** server.c -- a datagram socket listener
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
#include <stdbool.h>

#define MAXBUFFERLEN 5000 // the max length of the buffer
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
#define LOGOUT		15


void *get_in_addr(struct sockaddr *sa){
	if (sa->sa_family == AF_INET){
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int unpack_message(char *message,  int sockfd, int listener);
void login(char *client_ID, char *password, int sockfd);
void join(char *client_ID, char *session_ID, int sockfd);
struct client* get_client(char *client_ID);
struct session* get_session(char *session_ID);
void create(char *client_ID, char *session_ID, int sockfd);
void broadcast(char *message, int fdmax, int listener, fd_set master, int sockfd);
void list(int sockfd);
void leave(char *client_ID, char *session_ID);
void invite(char *client_ID, char *session_ID, char *invited_ID);
void logout(char *client_ID);

struct lab3message {
	unsigned int type;
	unsigned int size;
	unsigned char source[MAX_NAME];
	unsigned char data[MAX_DATA];
};

struct client{
	char client_ID[MAX_NAME];
	char password[MAX_NAME];
	bool connected;
	char session_ID[MAX_NAME];
	bool connected_session;
	int client_socket; 
};

struct session{
	char session_ID[MAX_NAME];
	struct client clientList[10];
	int num_connected;
	fd_set socket_list;
	int max_socket;
};

struct client clientList[10];
struct session sessionList[20];
int activeSessions = 0;

int main(int argc, char *argv[]){

	// set up the client list
	FILE *file;
	char *access_file = "clientList.txt";
	file = fopen(access_file, "r");
	char line[256];
	char *s = " ";
	char *subString;
	int length = 0;

	for (int i = 0; i < 10; i++){
		fgets(line, sizeof(line), file);
		subString = strtok(line, s);

		strcpy(clientList[i].client_ID, subString);

		subString = strtok(NULL, s);
		strcpy(clientList[i].password, subString);

		length = strlen(clientList[i].password);
	if (clientList[i].password[length-1] == '\n'){
		clientList[i].password[length-1] = '\0';
	}

		clientList[i].connected = false;
		clientList[i].connected_session = false;
		memset(clientList[i].session_ID, 0, sizeof(clientList[i].session_ID));
		clientList[i].client_socket = -1;
	}

	fclose(file);




	fd_set master; // master file descriptor list
	fd_set read_fds; // temp file descriptor list for select()
	int fdmax; // maximum file descriptor 

	int listener; // listening socket descriptor
	int newfd; // newly accept()ed socket descriptor
	struct sockaddr_storage remoteaddr; // client address
	socklen_t addrlen;

	char buf[256]; // buffer for client data
	int nbytes;

	char remoteIP[INET6_ADDRSTRLEN];

	int yes=1; // for setsockopt() SO_REUSEADDR, below
	int i, j, rv;

	struct addrinfo hints, *ai, *p;

	FD_ZERO(&master); // clear the master and temp sets
	FD_ZERO(&read_fds);

	 // get us a socket and bind it
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if ((rv = getaddrinfo(NULL, argv[1], &hints, &ai)) != 0) {
	 	fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
	 	exit(1);
	}

	for(p = ai; p != NULL; p = p->ai_next) {
	 	listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
	 	if (listener < 0) {
	 		continue;
	 	}

		 // lose the pesky "address already in use" error message
		//setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
		if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
		 	close(listener);
			continue;
		}

		break;
	}

	// if we got here, it means we didn't get bound
	if (p == NULL) {
		fprintf(stderr, "selectserver: failed to bind\n");
		exit(2);
	}

	freeaddrinfo(ai); // all done with this
	
	// listen
	if (listen(listener, 10) == -1) {
		perror("listen");
		exit(3);
	}

	// add the listener to the master set
	FD_SET(listener, &master);
	
	// keep track of the biggest file descriptor
	fdmax = listener; // so far, it's this one
	struct timeval tv;
	tv.tv_sec = 2;
 	tv.tv_usec = 500000;

 	int msg_Type = -1;
	// main loop
	for(;;) {
		read_fds = master; // copy it
		if (select(fdmax+1, &read_fds, NULL, NULL, &tv) == -1) {
			perror("select");
			exit(4);
		}

		// run through the existing connections looking for data to read
		for(i = 0; i <= fdmax; i++) {
			if (FD_ISSET(i, &read_fds)) { // we got one!!

				if (i == listener) {
					// handle new connections
					addrlen = sizeof remoteaddr;
					newfd = accept(listener,
						(struct sockaddr *)&remoteaddr,
						&addrlen);
					if (newfd == -1) {
						perror("accept");
					} else {
						FD_SET(newfd, &master); // add to master set
						if (newfd > fdmax) { // keep track of the max
							fdmax = newfd;
						}
						printf("selectserver: new connection from %s on "
							"socket %d\n",
							inet_ntop(remoteaddr.ss_family,
								get_in_addr((struct sockaddr*)&remoteaddr),
								remoteIP, INET6_ADDRSTRLEN),
							newfd);
					}
				} else {
					// handle data from a client
					if ((nbytes = recv(i, buf, sizeof(buf), 0)) <= 0) {
						// got error or connection closed by client
						if (nbytes == 0) {
							// connection closed
							for (int j = 0; j < 10; j++){
								if (clientList[j].client_socket == i){
									logout(clientList[j].client_ID);
								}
							}
							printf("selectserver: socket %d hung up\n", i);
						} else {
							perror("recv");
						}
						close(i); // bye!
						FD_CLR(i, &master); // remove from master set
					} else {
						buf[nbytes] = '\0';
						// printf("Hey we got %s\n", buf);

						msg_Type = unpack_message(buf, i, listener);
						// we got some data from a client
						
					}
				} // END handle data from client
			} // END got new incoming connection
		} // END looping through file descriptors
	} // END for(;;)--and you thought it would never end!
	close(listener);
	for(i = 0; i <= fdmax; i++){
		close(i);
	}
	return 0;
}


int unpack_message(char *message, int sockfd, int listener){
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
	char *session_ID;
	char *invited_ID;

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
	//printf("got message of type: %d\n", msg_Type);
	//string = strtok(NULL, colon);
	//printf("client_ID:%s\n", client_ID);

	struct session *current_session;
	struct client *current_client;

	switch(msg_Type){
		case LOGIN:

			msgChunk = strtok(data, space);
			strcpy(client_ID, msgChunk);

			msgChunk = strtok(NULL, space);
			password = malloc(sizeof(msgChunk));
			strcpy(password, msgChunk);
			login(client_ID, password, sockfd);

		break;
		case JOIN:

			join(client_ID, data, sockfd);
		break;
		case LEAVE_SESS:

			current_client = get_client(client_ID);

			if (current_client->connected_session == false){
				printf("You are not connected to a session\n");
				break;
			}

			leave(client_ID, current_client->session_ID);
		break;
		case NEW_SESS:
			current_client = get_client(client_ID);

			if (current_client->connected_session == true){
				printf("Please exit a session to create a new one\n");
				break;
			}

			create(client_ID, data, sockfd);
		break;
		case MESSAGE:

			current_client = get_client(client_ID);

			if (current_client->connected_session == false){
				printf("Please join a session to send messages\n");
				break;
			}
			current_session = get_session(current_client->session_ID);

			if (current_session == NULL){
				printf("You is null\n");
				break;
			}

			broadcast(message, current_session->max_socket, listener, 
				current_session->socket_list, sockfd);
		break;
		case QUERY:
			list(sockfd);
		break;
		case INVITE:

			session_ID = malloc(sizeof(data));
			strcpy(session_ID, data);

			invited_ID = malloc(sizeof(string));
			strcpy(invited_ID, string);

			invite(client_ID, session_ID, invited_ID);
		break;
		case LOGOUT:
			logout(client_ID);
		break;
		default:
			printf("Message error, type not recognized\n");
		break;
	}

	return msg_Type;
}

void login(char *client_ID, char *password, int sockfd){
	char *ack = "2";
	char nack[100] = "3:0: :";

	struct client *current = get_client(client_ID);

	if (current == NULL){
		strcat(nack, "user not found");
		send(sockfd, nack, strlen(nack), 0);
		return;
	}

	if (strcmp(password, current->password) == 0){
		if (current->connected){
			strcat(nack, "user already logged in");
			send(sockfd, nack, strlen(nack), 0);
		} else {
		current->connected = true;
		current->client_socket = sockfd;
		printf("Login success\n");
		send(sockfd, ack, strlen(ack), 0);
		return;
		}
	} else {
		strcat(nack, "wrong password");
		send(sockfd, nack, strlen(nack), 0);
		return;
	}
	
}

void join(char *client_ID, char *session_ID, int sockfd){
	int i = 0;
	int j = 0;
	int num = 0;

	char ack[500] = "6:0: :";
	char nack[100] = "7:0: :";

	if (activeSessions == 0){
		char *error = "no active sessions";
		printf("No active sessions to join\n");
		strcat(nack, error);
		send(sockfd, nack, strlen(nack), 0);
		return;
	}
	
	struct client *current = get_client(client_ID);
	struct session *current_session = get_session(session_ID);

	if (current_session == NULL){
		return;
	}

	current->connected_session = true;
	strcpy(current->session_ID, session_ID);

	num = current_session->num_connected;
	
	strcpy(current_session->clientList[num].client_ID, current->client_ID);
	strcpy(current_session->clientList[num].password, current->password);
	strcpy(current_session->clientList[num].session_ID, current->session_ID);
	current_session->clientList[num].connected = true;
	current_session->clientList[num].connected_session = true;
	current_session->clientList[num].client_socket = current->client_socket;

	FD_SET(sockfd, &current_session->socket_list);
	if (current_session->max_socket < sockfd){
		current_session->max_socket = sockfd;
	}

	current_session->num_connected++;

	printf("client %s joined %s session\n", current_session->clientList[num].client_ID, sessionList[i].session_ID);

	strcat(ack, session_ID);
	send(sockfd, ack, strlen(ack), 0);

}

struct client* get_client(char *client_ID){
	for (int i = 0; i < 10; i++){
		if (strcmp(client_ID, clientList[i].client_ID) == 0){
			return &clientList[i];
		}
	}

	return NULL;
}

struct session* get_session(char *session_ID){
	int i = 0;

	while((strlen(sessionList[i].session_ID) != 0) && i < activeSessions){
		if (strcmp(sessionList[i].session_ID, session_ID) == 0){

			return &sessionList[i];
		}
		i++;
	}

	return NULL;
}

void create(char *client_ID, char *session_ID, int sockfd){
	//printf("entered create\n");
	struct client *current = get_client(client_ID);
	struct session new_session;

	strcpy(current->session_ID, session_ID);
	current->connected_session = true;

	//sessionList[activeSessions] = new_session;
	strcpy(sessionList[activeSessions].session_ID, session_ID);
	strcpy(sessionList[activeSessions].clientList[0].client_ID, current->client_ID);
	strcpy(sessionList[activeSessions].clientList[0].password, current->password);
	strcpy(sessionList[activeSessions].clientList[0].session_ID, session_ID);
	sessionList[activeSessions].clientList[0].connected = true;
	sessionList[activeSessions].clientList[0].connected_session = true;
	sessionList[activeSessions].clientList[0].client_socket = current->client_socket;

	sessionList[activeSessions].num_connected = 1;

	FD_ZERO(&sessionList[activeSessions].socket_list);
	FD_SET(sockfd, &sessionList[activeSessions].socket_list);
	// printf("activeSessions: %d\n", activeSessions);
	sessionList[activeSessions].max_socket = sockfd;
	//printf("session ID: %s\n", sessionList[activeSessions].session_ID);

	activeSessions++;


	char ack[1000] = "10:0: :";
	strcat(ack, session_ID);
	// printf("sending NS_ACK %s\n", ack);
	send(sockfd, ack, strlen(ack), 0);
}

void broadcast(char *buf, int fdmax, int listener, fd_set set, int sockfd){
	//if (msg_Type == MESSAGE){

	for(int j = 0; j <= fdmax; j++) {
		// send to everyone!
		if (FD_ISSET(j, &set)) {
			// except the listener and ourselves
			if (j != listener && j != sockfd) {

				if (send(j, buf, strlen(buf), 0) == -1) {
					perror("send");
				}
			}
		}
	}
//}	
}

void list(int sockfd){
	char ack[10000] = "13:";
	char temp[10];
	char activeUsers[1000] = "";
	int count = 0;
	char userSession[1000];

	for (int i = 0; i < 10; i++){
		if (clientList[i].connected){
			strcat(activeUsers, clientList[i].client_ID);
			strcat(activeUsers, ":");
			count++;
		}
	}
	sprintf(temp, "%u", count);
	strcat(ack, temp);
	strcat(ack, ": :");
	strcat(ack, activeUsers);

	for (int i = 0; i < activeSessions; i++){

			strcpy(userSession, sessionList[i].session_ID);
			strcat(userSession, ":");

			for (int j = 0; j < 10; j++){
				if (strlen(sessionList[i].clientList[j].client_ID) != 0){
					strcat(userSession, sessionList[i].clientList[j].client_ID);
					// printf("    %s\n", sessionList[i].clientList[j].client_ID);
					strcat(userSession, " ");
				}
			}
			strcat(userSession, " :");
			strcat(ack, userSession);
		
	}
	// printf("sending %s\n", ack);
	send(sockfd, ack, strlen(ack), 0);
}

void leave(char *client_ID, char *session_ID){
	struct client *current = get_client(client_ID);
	struct session *current_session = get_session(session_ID);

	for(int i = 0; i < current_session->num_connected; i++){
		if (strcmp(client_ID, current_session->clientList[i].client_ID) == 0){
			// clear ID
			memset(current_session->clientList[i].client_ID, 0,
			 sizeof(current_session->clientList[i].client_ID));
			// clear session 
			memset(current_session->clientList[i].session_ID, 0,
			 sizeof(current_session->clientList[i].session_ID));
			// clear password
			memset(current_session->clientList[i].password, 0,
			 sizeof(current_session->clientList[i].password));
			current_session->clientList[i].connected = false;
			current_session->clientList[i].connected_session = false;
			current_session->clientList[i].client_socket = -1;

			current->connected_session = false;
			
			FD_CLR(current->client_socket, &current_session->socket_list);
			break;
		}
	}


}

// get a message in the format INVITE:len:ID:sesionID:inviteeID
void invite(char *client_ID, char *session_ID, char *invited_ID){

	struct client *invited = get_client(invited_ID);

	char invitation[100] = "14:0:";
	strcat(invitation, client_ID);
	strcat(invitation, ":");
	strcat(invitation, session_ID);
	strcat(invitation, ":");
	strcat(invitation, invited_ID);

	send(invited->client_socket, invitation, strlen(invitation), 0);
}

void logout(char *client_ID){
	// printf("client ID is %s\n", client_ID);

	struct client *current = get_client(client_ID);
	if (current == NULL){
		printf("client not found!\n");
	} 
	
	if (current->connected_session){
		leave(client_ID, current->session_ID);
	}
	current->connected = false;

	printf("%s logged out\n", client_ID);
}














