#include <sys/socket.h>
#include <sys/types.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>
#include <arpa/inet.h>

int sockfd, connfd;
struct sockaddr_in server, client;
char webpage[] =
"HTTP/1.1 200 OK\r\n"
"Content-Type: text/html; charset=utf-8\r\n"
"<!DOCTYPE html>\n<html>\n<head><meta charset=\"utf-8\">"
"<title>Test page</title>\n</head>\n"
"<body>Yeahboi</body>\n</html>";

/*void closeConnection() {
	shutdown(connfd, SHUT_RDWR);
	close(connfd);
}

void loop() {
	
}*/

int main(int argc, char *argv[])
{
	if(argc != 2) {
		printf("Usage: %s <port> \n", argv[0]);
		exit(1);
	}

	int myPort = atoi(argv[1]);
	char payload[2048];
	socklen_t sin_len = sizeof(client);

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
	listen(sockfd, 1);

	/*while(1) {
		printf("Loopyloop");
		socklen_t len = (socklen_t) sizeof(client);
		connfd = accept(sockfd, (struct sockaddr *) &client, &len);

		if(connfd < 0) {
			perror("Accepting connection failed.");
			exit(1);
		}

		// TODO add timestamp
		printf("timestamp : %s:%d", inet_ntoa(client.sin_addr), ntohs(client.sin_port));

	}*/

	while(1)
    {
        connfd = accept(sockfd, (struct sockaddr *) &client, &sin_len);

        if(connfd == -1) {
            perror("Connection failed...\n");
            close(sockfd);
            continue;
        }

        printf("Got client connection.....\n");
        if(!fork()) {

            close(sockfd);
            memset(payload, 0, 2048);
            read(connfd, payload, 2047);
            printf("%s\n", payload);

            write(connfd, webpage, sizeof(webpage) -1);
            close(connfd);
            printf("closing...\n");
            exit(0);
        }
        close(connfd);
    }

	//closeConnection();

	return 0;
}
