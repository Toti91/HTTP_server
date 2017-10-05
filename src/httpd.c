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
#include <stdbool.h>
#include <unistd.h>

int sockfd, connfd;
struct sockaddr_in server, client;

void logInfo(GString* payload, gchar* method) {
	GString* logStr;
	GString* pl = g_string_sized_new(payload->allocated_len);
	g_string_assign(pl, payload->str);

	gchar* statusCode;
	gchar* host;

	// Send correct status code corresponding to request method.. TODO: this is bad code
	if(g_strcmp0(method, "POST") == 0) {
		statusCode = "201";
	}
	else if(g_strcmp0(method, "INVALID") == 0) {
		statusCode = "501";
	}
	else {
		statusCode = "200";
	}

	// Split payload into lines and extract host from second line.
	gchar** lines = g_strsplit(pl->str, "\r\n", 0);
	gchar** secondLine = g_strsplit(lines[1], " ", 0);

	host = secondLine[1];

	// Retrieve client port.
	char portStr[sizeof(ntohs(client.sin_port))];
	sprintf(portStr, "%d", ntohs(client.sin_port));

	// ISO 8601 datetime
	time_t timeStamp = time(NULL);
	char formattedTime[] = "1991-12-11T00:00:00+TZ"; // My birthday!
	struct tm* thisTime = gmtime(&timeStamp);
	strftime(formattedTime, sizeof(formattedTime), "%FT%T%Z", thisTime);

	// Construct the log string.
	logStr = g_string_new(" : ");
	g_string_prepend(logStr, formattedTime);
	g_string_append(logStr, inet_ntoa(client.sin_addr));
	g_string_append(logStr, ":");
	g_string_append(logStr, portStr);
	g_string_append(logStr, " ");
	g_string_append(logStr, method);
	g_string_append(logStr, " ");
	g_string_append(logStr, host);
	g_string_append(logStr, " : ");
	g_string_append(logStr, statusCode);
	g_string_append(logStr, "\n");

	// Write to log file.
	FILE *logFile = fopen("../log.txt", "a");
	fputs(logStr->str, logFile);
	fclose(logFile);

	g_string_free(logStr, TRUE);
	g_strfreev(lines);
	g_strfreev(secondLine);
}

GString* handleHeader(GString* payload, bool headRequest, gsize contentLen) {
	// Copy payload into a fresh GString.
	GString* pl = g_string_sized_new(payload->allocated_len);
	g_string_assign(pl, payload->str);

	// Split payload on newlines.
	gchar** lines = g_strsplit(pl->str, "\r\n", 0);
	
	// Construct a basic header string.
	GString* head = g_string_sized_new(pl->allocated_len);

	if(g_str_has_prefix(pl->str, "POST")) {
		g_string_assign(head, "HTTP/1.1 201 CREATED\n");
	}
	else {
		g_string_assign(head, "HTTP/1.1 200 OK\n");
		g_string_append(head, "Content-Type: text/html\n");
	}

	// If this call is a GET request, we want to add the Content-Length header.
	// Content-Length is a payload field which means we dont need to send it in a HEAD request.
	// RFC 7231 (Section 3.3)
	if(g_str_has_prefix(pl->str, "GET")) {
		// The Content-Length STRING shouldn't need more than 4 bytes. (max 9999 ?)
		char contSize[4];
		sprintf(contSize, "%zd", contentLen);
		GString* lenStr = g_string_new("\n");
		g_string_prepend(lenStr, contSize);

		g_string_append(head, "Content-length: ");
		g_string_append(head, lenStr->str);
		g_string_free(lenStr, TRUE);
	}

	// Strip lines of whitespaces and add them to the header string.
	for(unsigned int i = 0; i < g_strv_length(lines); i++) {
		g_strstrip(lines[i]);
		g_string_append_printf(head, "%s\n", lines[i]);
	}

	// If this call is a HEAD request, then just send the header to the client connection.
	if(headRequest) {
		send(connfd, head->str, head->len, 0);
		g_string_free(head, TRUE);
		g_string_free(pl, TRUE);
	}

	return head;
}

void handleGetRequest(GString* payload) {
	// Create a new GString to copy payload into.
	GString* pl = g_string_sized_new(payload->allocated_len);
	g_string_assign(pl, payload->str);

	// Split the header and retrieve the host url.
	gchar** lines = g_strsplit(pl->str, "\r\n", 0);
	gchar** firstLine = g_strsplit(lines[0], " ", 0);
	gchar** secondLine = g_strsplit(lines[1], " ", 0);
	gchar* page = firstLine[1];
	gchar* host = secondLine[1];

	// Create a new string that will hold basic HTML page.
	GString* html = g_string_new("<!DOCTYPE html>\n<html><head><title>Test</title></head><body>");

	// Retrieve client port.
	char cliPort[sizeof(ntohs(client.sin_port))];
	sprintf(cliPort, "%d", ntohs(client.sin_port));

	// Construct a string that contains the host url.
	GString* hostUrl = g_string_new(host);
	g_string_append(hostUrl, page);
	g_strchug(hostUrl->str);
	g_string_prepend(hostUrl, "http://");

	// Insert host url and client information into HTML.
	g_string_append(html, hostUrl->str);
	g_string_append(html, " ");
	g_string_append(html, inet_ntoa(client.sin_addr));
	g_string_append(html, ":");
	g_string_append(html, cliPort);
	g_string_append(html, "</body></html>");
	g_string_free(hostUrl, TRUE);

	// Construct a string with header and html page.
	GString* header = handleHeader(payload, FALSE, html->len);
	GString* response = g_string_sized_new(header->allocated_len);
	g_string_append(response, header->str);
	g_string_append(response, html->str);

	// Send the page to the client connection.
	send(connfd, response->str, response->len, 0);

	// Free memory
	g_string_free(header, TRUE);
	g_string_free(html, TRUE);
	g_string_free(response, TRUE);
}

void handlePostRequest(GString* payload) {
	// Copy payload into fresh GString
	GString* pl = g_string_sized_new(payload->allocated_len);
	g_string_assign(pl, payload->str);

	// Get correct header.
	GString* response = handleHeader(pl, FALSE, pl->len);
	g_string_free(pl, TRUE);

	// Send response.
	send(connfd, response->str, response->len, 0);
	g_string_free(response, TRUE);
}

void handleInvalid() {
	// If the request is not GET, POST or HEAD, then send Not implemented..
	GString* response = g_string_new("HTTP/1.1 501 Not implemented\n\n");

	send(connfd, response->str, response->len, 0);
	g_string_free(response, TRUE);
}

bool handleRequest(GString* payload) {
	if(g_str_has_prefix(payload->str, "GET")) { // GET request
		handleGetRequest(payload);
		logInfo(payload, "GET");
		printf("GET..\n");

		return true;
	}
	else if(g_str_has_prefix(payload->str, "HEAD")) { // HEAD request
		handleHeader(payload, TRUE, 0);
		logInfo(payload, "HEAD");
		printf("HEAD..\n");

		return true;
	}
	else if(g_str_has_prefix(payload->str, "POST")) { // POST request
		handlePostRequest(payload);
		logInfo(payload, "POST");
		printf("POST..\n");

		return true;
	}
	else { // INVALID request
		handleInvalid();
		logInfo(payload, "INVALID");
		printf("Invalid request, closing connection..\n");

		return false;
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

	// Set up socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	server.sin_port = htons(myPort);

	int optionStatus = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

	if(optionStatus < 0) {
		perror("Setting socket options failed");
	}

	int bindStatus = bind(sockfd, (struct sockaddr *) &server, (socklen_t) sizeof(server));

	if(bindStatus < 0) {
		perror("Socket binding failed.");
		return EXIT_FAILURE;
	}

	printf("Listening on port %d \n\n", myPort);
	listen(sockfd, 1);

	while(1)
	{
		// Accept incoming client connection.
		connfd = accept(sockfd, (struct sockaddr *) &client, &cliLen);

		if(connfd == -1) {
			perror("Accepting connection failed..\n");
			close(sockfd);
			continue;
		}

		printf("Got client connection..\n");
		
		memset(buff, 0, 2048);
		read(connfd, buff, 2047);
		
		GString* payload = g_string_new(buff);

		if(handleRequest(payload)) {
			printf("Done! Closing connection..\n\n");
		}

		close(connfd);
		g_string_free(payload, TRUE);
	}

	return 0;
}