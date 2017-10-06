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

typedef struct clientRequest {
	int connfd;
	gchar* method;
	gchar* statusCode;
	gchar* page;
	gchar* hostInfo;
	gchar* httpVersion;
	gchar* port;
	GString* requestBody;
} clientRequest;

struct sockaddr_in server, client;

void logInfo(clientRequest* cr) {
	GString* logStr;

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
	g_string_append(logStr, cr->port);
	g_string_append(logStr, " ");
	g_string_append(logStr, cr->method);
	g_string_append(logStr, " ");
	g_string_append(logStr, cr->hostInfo);
	g_string_append(logStr, " : ");
	g_string_append(logStr, cr->statusCode);
	g_string_append(logStr, "\n");

	// Write to log file.
	FILE *logFile = fopen("../log.txt", "a");
	fputs(logStr->str, logFile);
	fclose(logFile);

	g_string_free(logStr, TRUE);
}

GString* handleHeader(GString* payload, clientRequest* cr, gsize contentLen) {
	GString* pl = g_string_sized_new(payload->allocated_len);
	g_string_assign(pl, payload->str);

	// Construct a basic header string.
	GString* head = g_string_sized_new(payload->allocated_len);

	if(g_str_has_prefix(pl->str, "POST")) {
		g_string_assign(head, cr->httpVersion);
		g_string_append(head, " 201 CREATED\n");
	}
	else {
		g_string_assign(head, cr->httpVersion);
		g_string_append(head, " 200 OK\n");
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

	// Split payload into lines.
	gchar** lines = g_strsplit(pl->str, "\r\n", 0);

	// Strip lines of whitespaces and add them to the header string.
	for(unsigned int i = 0; i < g_strv_length(lines); i++) {
		// If this is a POST request we need to inject a new Content-Length header 
		// with the correct length of the added HTML.
		if(g_str_has_prefix(pl->str, "POST") && g_str_has_prefix(lines[i], "Content-Length:")) {
			g_string_append_printf(head, "%s %lu\n", "Content-Length:", contentLen);
			continue;
		}
		
		g_strstrip(lines[i]);
		g_string_append_printf(head, "%s\n", lines[i]);
	}

	// If this call is a HEAD request, then just send the header to the client connection.
	if(g_strcmp0(cr->method, "HEAD") == 0) {
		send(cr->connfd, head->str, head->len, 0);
		g_string_free(head, TRUE);
	}

	g_free(lines);
	g_string_free(pl, TRUE);

	return head;
}

GString* constructHtml(clientRequest* cr) {
	// Initialize a new string with the host information.
	GString* hostUrl = g_string_new(cr->hostInfo);
	g_string_append(hostUrl, cr->page);
	g_strchug(hostUrl->str);
	g_string_prepend(hostUrl, "http://");

	// Construct a HTML page and inject host and client information..
	GString* ret = g_string_new("<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>Test</title></head><body>");
	g_string_append(ret, hostUrl->str);
	g_string_append(ret, " ");
	g_string_append(ret, inet_ntoa(client.sin_addr));
	g_string_append(ret, ":");
	g_string_append(ret, cr->port);
	g_string_append(ret, cr->requestBody->str);
	g_string_append(ret, "</body></html>");

	g_string_free(hostUrl, TRUE);

	return ret;
}

void sendGetResponse(GString* payload, clientRequest* cr) {
	// Construct the HTML5 page.
	GString* html = constructHtml(cr);

	// Construct a response string with header and html page.
	GString* header = handleHeader(payload, cr, html->len + 1);
	GString* response = g_string_sized_new(header->allocated_len);
	g_string_append(response, header->str);
	g_string_append(response, html->str);

	// Send the page to the client connection.
	send(cr->connfd, response->str, response->len, 0);

	// Free memory
	g_string_free(header, TRUE);
	g_string_free(html, TRUE);
	g_string_free(response, TRUE);
}

void sendPostResponse(GString* payload, clientRequest* cr) {
	// This is the correct content-length.
	gsize contentLen = (constructHtml(cr)->len);
	GString* pl = handleHeader(payload, cr, contentLen);

	// Split the payload on header/body and insert new body.
	gchar** split = g_strsplit(pl->str, "\n\n", 0);
	GString* response = g_string_new(split[0]);

	g_string_append(response, "\n\n");
	g_string_append(response, constructHtml(cr)->str);

	// Send response.
	send(cr->connfd, response->str, response->len, 0);

	g_free(split);
	g_string_free(pl, TRUE);
	g_string_free(response, TRUE);
}

void sendInvalidResponse(clientRequest* cr) {
	// If the request is not GET, POST or HEAD, then send Not implemented..
	GString* response = g_string_new(cr->httpVersion);
	g_string_append(response, " 501 Not implemented\n\n");

	send(cr->connfd, response->str, response->len, 0);
	g_string_free(response, TRUE);
}

bool handleRequest(GString* payload, clientRequest* cr) {
	if(g_strcmp0(cr->method, "GET") == 0) { // GET request
		sendGetResponse(payload, cr);
		printf("GET..\n");

		return true;
	}
	else if(g_strcmp0(cr->method, "HEAD") == 0) { // HEAD request
		handleHeader(payload, cr, 0);
		printf("HEAD..\n");

		return true;
	}
	else if(g_strcmp0(cr->method, "POST") == 0) { // POST request
		sendPostResponse(payload, cr);
		printf("POST..\n");

		return true;
	}
	else { // INVALID request
		sendInvalidResponse(cr);
		printf("Invalid request: %s\n", cr->method);
		printf("Closing connection..\n\n");

		return false;
	}
}

// Creates a new client request that holds information like the request method etc.
clientRequest* newClientRequest(int cfd, GString* payload) {
	// Copy payload into fresh GString
	GString* pl = g_string_sized_new(payload->allocated_len);
	g_string_assign(pl, payload->str);
	clientRequest* cr = malloc(sizeof(clientRequest));
	
	// Split payload header into lines, then extract first and second lines for the information we need.
	gchar** lines = g_strsplit(pl->str, "\r\n", 0);
	gchar** firstLine = g_strsplit(lines[0], " ", 0);
	gchar** secondLine = g_strsplit(lines[1], " ", 0);

	// Retrieve the client port from socket.
	char portStr[sizeof(ntohs(client.sin_port))];
	sprintf(portStr, "%d", ntohs(client.sin_port));

	cr->connfd = cfd;
	cr->method = g_strstrip(firstLine[0]);
	cr->page = firstLine[1];
	cr->hostInfo = secondLine[1];
	cr->httpVersion = firstLine[2];
	cr->port = portStr;
	cr->requestBody = g_string_new(strstr(payload->str, "\r\n\r\n"));

	// Set the correct HTTP status code corresponding to method.
	if(g_strcmp0(cr->method, "POST") == 0) {
		cr->statusCode = "201";
	}
	else if(g_strcmp0(cr->method, "GET") == 0 || g_strcmp0(cr->method, "HEAD") == 0) {
		cr->statusCode = "200";
	}
	else {
		cr->statusCode = "501";
	}

	g_free(lines);
	g_free(firstLine);
	g_free(secondLine);
	g_string_free(pl, TRUE);

	return cr;
}

/*void destroyClientRequest(clientRequest* cr) {
	close(cr->connfd);
	g_free(cr->method);
	g_free(cr->statusCode);
	g_free(cr->page);
	g_free(cr->hostInfo);
	g_free(cr->httpVersion);
	g_free(cr->port);
	g_string_free(cr->requestBody, TRUE);
	g_free(cr);
}*/

int main(int argc, char *argv[])
{
	if(argc != 2) {
		printf("Usage: %s <port> \n", argv[0]);
		exit(1);
	}

	int sockfd, status;
	int myPort = atoi(argv[1]);
	char buff[2048];
	socklen_t cliLen = sizeof(client);

	// Set up socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	server.sin_port = htons(myPort);

	status = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

	if(status < 0) {
		perror("Setting socket options failed");
		close(sockfd);
		return EXIT_FAILURE;
	}

	/*status = ioctl(sockfd, FIONBIO, &(int){1});

	if(status < 0) {
		perror("setsockopt() failed");
		close(sockfd);
		return EXIT_FAILURE;
	}*/

	status = bind(sockfd, (struct sockaddr *) &server, (socklen_t) sizeof(server));

	if(status < 0) {
		perror("Socket binding failed.");
		return EXIT_FAILURE;
	}

	printf("Listening on port %d \n\n", myPort);
	listen(sockfd, 1);

	while(1)
	{
		// Accept incoming client connection.
		int connfd = accept(sockfd, (struct sockaddr *) &client, &cliLen);

		if(connfd == -1) {
			perror("Accepting connection failed..\n");
			close(sockfd);
			continue;
		}

		printf("Got client connection..\n");
		
		memset(buff, 0, 2048);
		read(connfd, buff, 2047);
		
		GString* payload = g_string_new(buff);
		clientRequest* cr = newClientRequest(connfd, payload);
		logInfo(cr);

		if(handleRequest(payload, cr)) {
			printf("Done! Closing connection..\n\n");
		}

		close(connfd);
		//destroyClientRequest(cr);
		g_string_free(payload, TRUE);
	}

	return 0;
}