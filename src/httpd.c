#include <sys/socket.h>
#include <sys/types.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>
#include <arpa/inet.h>
#include <time.h>

int sockfd, connfd;
struct sockaddr_in server, client;
char webpage[] =
"HTTP/1.1 200 OK\r\n"
"Content-Type: text/html; charset=utf-8\r\n"
"<!DOCTYPE html>\n<html>\n<head><meta charset=\"utf-8\">"
"<title>Test page</title>\n</head>\n"
"<body>Yeahboi</body>\n</html>";

void logInfo() {
	GString* logStr;
	time_t timeStamp = time(NULL);
	char portStr[5];
	sprintf(portStr, "%d", ntohs(client.sin_port));

	logStr = g_string_new(" : ");
	g_string_prepend(logStr, asctime(localtime(&timeStamp)));
	g_string_append(logStr, inet_ntoa(client.sin_addr));
	g_string_append(logStr, ":");
	g_string_append(logStr, portStr);
	g_string_append(logStr, "\n");

	FILE *logFile = fopen("../log.txt", "a");
	fputs(logStr->str, logFile);
	fclose(logFile);
}

int main(int argc, char *argv[])
{
	if(argc != 2) {
		printf("Usage: %s <port> \n", argv[0]);
		exit(1);
	}

	int myPort = atoi(argv[1]);
	char payload[2048];
	socklen_t cliLen = sizeof(client);

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

	while(1)
    {
		connfd = accept(sockfd, (struct sockaddr *) &client, &cliLen);

        if(connfd == -1) {
            perror("Accepting connection failed..\n");
            close(sockfd);
            continue;
        }

		printf("Got client connection..\n\n");
		logInfo();

        if(!fork()) {

            close(sockfd);
            memset(payload, 0, 2048);
            read(connfd, payload, 2047);
            //printf("%s\n", payload);

            write(connfd, webpage, sizeof(webpage) -1);
            close(connfd);
            printf("closing...\n");
            exit(0);
        }
        close(connfd);
    }

	return 0;
}
