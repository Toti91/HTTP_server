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

void logInfo() {
	GString* logStr;
	char portStr[sizeof(ntohs(client.sin_port))];
	sprintf(portStr, "%d", ntohs(client.sin_port));
	time_t timeStamp = time(NULL);

	char formattedTime[] = "1991-12-11T00:00:00+TZ";
	struct tm* thisTime = gmtime(&timeStamp);
	strftime(formattedTime, sizeof(formattedTime), "%FT%T%Z", thisTime);


	logStr = g_string_new(" : ");
	g_string_prepend(logStr, formattedTime);
	g_string_append(logStr, inet_ntoa(client.sin_addr));
	g_string_append(logStr, ":");
	g_string_append(logStr, portStr);
	g_string_append(logStr, "\n");

	FILE *logFile = fopen("../log.txt", "a");
	fputs(logStr->str, logFile);
	fclose(logFile);

	g_string_free(logStr, TRUE);
}

void sendWebPage(GString* payload) {
	// Create a new GString to copy payload into.
	GString* pl = g_string_sized_new(payload->allocated_len);
	g_string_assign(pl, payload->str);

	// Split the header and retrieve the host url.
	gchar** lines = g_strsplit(pl->str, "\r\n", 0);
	gchar** firstLine = g_strsplit(lines[0], " ", 0);
	gchar** secondLine = g_strsplit(lines[1], " ", 0);
	gchar* page = firstLine[1];
	gchar* host = secondLine[1];

	GString* html = g_string_new("<!DOCTYPE html>\n<html><head><title>Test</title></head><body>");
	char cliPort[sizeof(ntohs(client.sin_port))];
	sprintf(cliPort, "%d", ntohs(client.sin_port));
	//sprintf(servPort, "%d", ntohs(server.sin_port));

	GString* hostUrl = g_string_new(host);
	g_string_append(hostUrl, page);
	g_strchug(hostUrl->str);
	g_string_prepend(hostUrl, "http://");


	g_string_append(html, hostUrl->str);
	g_string_append(html, " ");
	g_string_append(html, inet_ntoa(client.sin_addr));
	g_string_append(html, ":");
	g_string_append(html, cliPort);
	g_string_append(html, "</body></html>");

	g_string_free(hostUrl, TRUE);

	char contSize[3];
	sprintf(contSize, "%zd", html->len);

	GString* contentLen = g_string_new("\n");
	g_string_prepend(contentLen, contSize);

	GString* webPage = g_string_new("HTTP/1.1 200 OK\n");
	g_string_append(webPage, "Content-length: ");
	g_string_append(webPage, contentLen->str);
	g_string_append(webPage, "Content-Type: text/html\n\n");
	g_string_append(webPage, html->str);

	send(connfd, webPage->str, webPage->len, 0);

	// Free memory
	g_string_free(contentLen, TRUE);
	g_string_free(html, TRUE);
	g_string_free(webPage, TRUE);
}

void handleRequest(GString* pl) {
	if(g_str_has_prefix(pl->str, "GET")) { // GET request
		sendWebPage(pl);
	}
	else if(g_str_has_prefix(pl->str, "HEAD")) { // HEAD request
		// Do stuff
		printf("HEAD");
	}
	else if(g_str_has_prefix(pl->str, "POST")) { // POST request
		// Do stuff
		printf("POST");
	}
}

int main(int argc, char *argv[])
{
	if(argc != 2) {
		printf("Usage: %s <port> \n", argv[0]);
		exit(1);
	}

	int myPort = atoi(argv[1]);
	char buff[2048];
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

	printf("Listening on port %d \n\n", myPort);
	listen(sockfd, 1);

	while(1)
    {
		connfd = accept(sockfd, (struct sockaddr *) &client, &cliLen);

        if(connfd == -1) {
            perror("Accepting connection failed..\n");
            close(sockfd);
            continue;
        }

		printf("Got client connection..\n");
		logInfo();

        memset(buff, 0, 2048);
        read(connfd, buff, 2047);
		//printf("%s\n", buff);

		GString* payload = g_string_new(buff);
		handleRequest(payload);

        printf("Done! Closing connection..\n\n");
		close(connfd);
		g_string_free(payload, TRUE);
    }

	return 0;
}
