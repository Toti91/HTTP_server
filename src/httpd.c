#include <sys/socket.h>
#include <sys/types.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>

int sockfd;
struct sockaddr_in server, client;

void loop() {

	while(true) {

	}
}

int main(int argc, char *argv[])
{
	if(argc != 2) {
		printf("Usage: %s <port> \n", argv[0]);
		return 0;
	}

	int myPort = atoi(argv[1]);

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	server.sin_port = htons(myPort);

	int optionStatus = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

	if(optionStatus < 0) {
		perror("setting socket options failed");
	}

	int bindStatus = bind(sockfd, (struct sockaddr *) &server, (socklen_t) sizeof(server));

	if(bindStatus < 0) {
		perror("socket binding failed.");
		return EXIT_FAILURE;
	}

	printf("Listening on port %d \n", myPort);
	listen(sockfd, 10);

	loop();

	return 0;
}
