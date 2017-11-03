#include <sys/socket.h>
#include <sys/types.h>
#include <sys/poll.h>
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
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#define maxSize 100
struct pollfd fds[maxSize];
int fdSize=1;

typedef struct clientRequest {
	int connfd;
	gchar* method;
	gchar* statusCode;
	gchar* page;
	gchar* hostInfo;
	gchar* httpVersion;
	gchar* ipAddr;
	gchar* port;
	bool closeConnection;
	GString* requestBody;
} clientRequest;

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
	g_string_append(logStr, cr->ipAddr);
	g_string_append(logStr, ":");
	g_string_append(logStr, cr->port);
	g_string_append(logStr, " ");
	g_string_append(logStr, cr->method);
	g_string_append(logStr, " ");
	g_string_append(logStr, cr->hostInfo);
	g_string_append(logStr, cr->page);
	g_string_append(logStr, " : ");
	g_string_append(logStr, cr->statusCode);
	g_string_append(logStr, "\n");

	// Write to log file.
	FILE *logFile = fopen("../log.txt", "a");
	fputs(logStr->str, logFile);
	fclose(logFile);

	g_string_free(logStr, TRUE);
}

bool checkVersion(clientRequest* cr) {
	if(g_strcmp0(cr->httpVersion, "HTTP/1.1") == 0) {
		return TRUE;
	}
	
	return FALSE;
}

GString* handleHeader(GString* payload, clientRequest* cr, gsize contentLen) {
	GString* pl = g_string_sized_new(payload->allocated_len);
	g_string_assign(pl, payload->str);

	// Construct a basic header string.
	GString* head = g_string_sized_new(payload->allocated_len);

	if(g_str_has_prefix(pl->str, "POST")) {
		g_string_assign(head, cr->httpVersion);
		g_string_append(head, " 201 CREATED\r\n");
		g_string_append_printf(head, "%s %lu\r\n", "Content-Length:", contentLen);
	}
	else if(g_str_has_prefix(pl->str, "GET")) {
		g_string_assign(head, cr->httpVersion);
		g_string_append(head, " 200 OK\r\n");
		g_string_append(head, "Content-Type: text/html; charset=UTF-8\r\n");

		// If this call is a GET request, we want to add the Content-Length header.
		// Content-Length is a payload field which means we dont need to send it in a HEAD request.
		// RFC 7231 (Section 3.3)
		char contSize[4];
		sprintf(contSize, "%zd", contentLen);
		GString* lenStr = g_string_new("\r\n");
		g_string_prepend(lenStr, contSize);

		g_string_append(head, "Content-Length: ");
		g_string_append(head, lenStr->str);
		g_string_free(lenStr, TRUE);
	}
	else if(g_str_has_prefix(pl->str, "HEAD")) {
		g_string_assign(head, cr->httpVersion);
		g_string_append(head, " 200 OK\r\n");
		g_string_append(head, "Content-Type: text/html; charset=UTF-8\r\n");
	}
	else {
		g_string_assign(head, cr->httpVersion);
		g_string_append(head, " 501 Not implemented\r\n");
		g_string_append(head, "Content-Length: 0\r\n");
	}

	// Split payload into lines.
	gchar** lines = g_strsplit(pl->str, "\r\n", 0);


	bool addedClose = FALSE;
	bool addedKeep = FALSE;
	// Strip lines of whitespaces and add them to the header string.
	for(unsigned int i = 0; i < g_strv_length(lines); i++) {
		// Check if request contains the 'Connection: close' header.
		if(checkVersion(cr)) {
			if(g_strcmp0(lines[i], "Connection: close") == 0) {
				cr->closeConnection = TRUE;
				g_strstrip(lines[i]);
				g_string_append_printf(head, "%s\r\n", lines[i]);
			}
			else {
				if(addedKeep == FALSE) {
					cr->closeConnection = FALSE;
					g_string_append_printf(head, "%s\r\n", "Connection: keep-alive");
					addedKeep = TRUE;
				}
			}
		}
		// Check if requests contains 'Connection: keep-alive' header;
		else {
			if(g_strcmp0(lines[i], "Connection: keep-alive") == 0) {
				cr->closeConnection = FALSE;
				g_strstrip(lines[i]);
				g_string_append_printf(head, "%s\r\n", lines[i]);
			}
			else {
				if(addedClose == FALSE) {
					cr->closeConnection = TRUE;
					g_string_append_printf(head, "%s\r\n", "Connection: close");
					addedClose = TRUE;
				}
			}
		}
		
		// Get content-type header from request headers.
		if(g_str_has_prefix(pl->str, "POST")) {
			if(g_str_has_prefix(lines[i], "Content-Type:")) {
				g_strstrip(lines[i]);
				g_string_append_printf(head, "%s\r\n", lines[i]);
			}
		}
	}

	g_string_append(head, "\r\n");

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
	// + request body if this is a POST request.
	GString* ret = g_string_new("<!DOCTYPE html>\r\n<html><head><meta charset=\"utf-8\"><title>HTTP-Server</title></head><body>");
	g_string_append(ret, hostUrl->str);
	g_string_append(ret, " ");
	g_string_append(ret, cr->ipAddr);
	g_string_append(ret, ":");
	g_string_append(ret, cr->port);
	g_string_append(ret, cr->requestBody->str);
	g_string_append(ret, "</body></html>\r\n");
	g_string_free(hostUrl, TRUE);

	return ret;
}

GString* constructColorHtml(gchar* col) {
	GString* ret = g_string_new("<!DOCTYPE html>\r\n<html><head><meta charset=\"utf-8\"><title>HTTP-Server</title></head><body ");
	g_string_append(ret, "style='background-color:");
	g_string_append(ret, g_strsplit(col, "=", -1)[1]);
	g_string_append(ret, "'></body></html>\r\n");

	return ret;
}

void sendGetResponse(GString* payload, clientRequest* cr) {
	// Construct the HTML5 page.

	GString* html = g_string_new("");

	if(g_str_has_prefix(cr->page, "/color?")) {
		gchar* col = g_strdup(cr->page);
		html = constructColorHtml(g_strsplit(col, "?", -1)[1]);
	}
	else {
		html = constructHtml(cr);
	}

	// Construct a response string with header and html page.
	GString* header = handleHeader(payload, cr, html->len);
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
	// Construct html and header
	GString* html = constructHtml(cr);
	GString* pl = handleHeader(payload, cr, html->len);

	GString* response = g_string_sized_new(pl->allocated_len);
	g_string_append(response, pl->str);
	g_string_append(response, html->str);

	// Send response.
	send(cr->connfd, response->str, response->len, 0);

	g_string_free(html, TRUE);
	g_string_free(pl, TRUE);
	g_string_free(response, TRUE);
}

void sendInvalidResponse(GString* payload, clientRequest* cr) {
	// If the request is not GET, POST or HEAD, then send Not implemented..
	GString* response = handleHeader(payload, cr, 0);

	send(cr->connfd, response->str, response->len, 0);
	g_string_free(response, TRUE);
}

void handleRequest(GString* payload, clientRequest* cr) {
	if(g_strcmp0(cr->method, "GET") == 0) { 				// GET request
		sendGetResponse(payload, cr);
		printf("GET..\n");
	}
	else if(g_strcmp0(cr->method, "HEAD") == 0) { 			// HEAD request
		handleHeader(payload, cr, constructHtml(cr)->len);
		printf("HEAD..\n");
	}
	else if(g_strcmp0(cr->method, "POST") == 0) { 			// POST request
		sendPostResponse(payload, cr);
		printf("POST..\n");
	}
	else {													// INVALID request
		sendInvalidResponse(payload, cr);
		printf("Invalid request: %s\n", cr->method);
	}
}

// Creates a new client request that holds information like the request method etc.
clientRequest* newClientRequest(int cfd, GString* payload, struct sockaddr_in* sock) {
	// Copy payload into fresh GString
	GString* pl = g_string_sized_new(payload->allocated_len);
	g_string_assign(pl, payload->str);
	clientRequest* cr = malloc(sizeof(clientRequest));
	
	// Split payload header into lines, then extract first line for the information we need.
	gchar** lines = g_strsplit(pl->str, "\r\n", 0);
	gchar** firstLine = g_strsplit(lines[0], " ", 0);

	// Retrieve the client port from socket.
	char portStr[sizeof(ntohs(sock->sin_port))];
	sprintf(portStr, "%d", ntohs(sock->sin_port));

	cr->connfd = cfd;
	cr->method = g_strstrip(firstLine[0]);
	cr->page = firstLine[1];
	cr->httpVersion = firstLine[2];
	cr->ipAddr = inet_ntoa(sock->sin_addr);
	cr->port = portStr;
	cr->closeConnection = FALSE;
	cr->requestBody = g_string_new(strstr(payload->str, "\r\n\r\n"));

	// Loop through header and retrieve host address.
	for (unsigned int i = 0; i < g_strv_length(lines); i++) {
		if (g_str_has_prefix(lines[i], "Host: ")) {
			cr->hostInfo = &lines[i][6];
		}
	}

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
	g_string_free(pl, TRUE);

	return cr;
}

void destroyConnection(int i) {
	// Close the connection and shrink the FD-array.
	close(fds[i].fd);
	fds[i].fd = -1;

	for (int k = 0; k < fdSize; k++) {
		if (fds[k].fd == -1) {
			for(int j = k; j < fdSize; j++) {
				fds[j].fd = fds[j + 1].fd;
			}
			k--;
			fdSize--;
		}
	}
}

int main(int argc, char *argv[])
{
	if(argc != 2) {
		printf("Usage: %s <port> \n", argv[0]);
		exit(1);
	}

	int sockfd, status, pollStatus, connfd=-1, currSize=0;
	struct sockaddr_in server, client;
	int myPort = atoi(argv[1]);
	char buff[2048];
	socklen_t cliLen = sizeof(client);
	clientRequest* cr;

	const int timeout = (30 * 1000); // 30 second timeout

	// Set up socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	server.sin_port = htons(myPort);
	status = bind(sockfd, (struct sockaddr *) &server, (socklen_t) sizeof(server));

	if(status < 0) {
		perror("Socket binding failed.");
		return EXIT_FAILURE;
	}

	printf("Listening on port %d \n\n", myPort);
	listen(sockfd, 32);
	memset(fds, 0, sizeof(fds));

	// Initialize array of file descriptors with socket fd.
	fds[0].fd = sockfd;
	fds[0].events = POLLIN;

	// POLLING: We received help with consideration to this tutorial: 
	// https://www.ibm.com/support/knowledgecenter/ssw_ibm_i_71/rzab6/poll.htm
	while(TRUE) {
		pollStatus = poll(fds, fdSize, timeout);

		if(pollStatus < 0) {
			perror("poll() failed");
			return EXIT_FAILURE;
		}

		currSize = fdSize;

		// Time out
		if(pollStatus == 0) {
			// Loop through file descriptors and check if they timed out.
			for(int i = 0; i < currSize; i++) {
				if(fds[i].revents != POLLIN && fds[i].fd != sockfd) {
					printf("DESTROYINGG KEEP-ALIVE\n");
					destroyConnection(i);
				}
			}
			continue;
		}
		
		for(int i = 0; i < currSize; i++) {
			if(fds[i].revents == 0) {
				continue;
			}

			if(fds[i].revents != POLLIN) {
				break;
			}

			if(fds[i].fd == sockfd) {
				// If this iteration is the socket fd then
				// accept incoming connections and add their file descriptors to array.
				connfd = accept(sockfd, (struct sockaddr *) &client, &cliLen);

				if(connfd < 0) {
					// If errno is EWOULDBLOCK then we should break out of the loop.
					if(errno != EWOULDBLOCK) {
						perror("accept() failed");
						return EXIT_FAILURE;
					}
					break;
				}

				// Add new file descriptor to array.
				fds[fdSize].fd = connfd;
				fds[fdSize].events = POLLIN;
				fdSize++;
			}
			else {
				// Memset buffer for incoming data.
				memset(buff, 0, 2048);

				if(fds[i].revents & POLLIN) {
					status = recv(fds[i].fd, buff, sizeof(buff), 0);
				}

				if(status < 0) {
					if(errno != EWOULDBLOCK) {
						perror("read() failed");
						return EXIT_FAILURE;
					}
					break;
				}

				// Done reading, no need to send data.
				if(status == 0) {
					continue;
				}

				// Create a payload from buffer and initialize a new client request,
				// Then parse the request and send response.
				GString* payload = g_string_new(buff);
				cr = newClientRequest(fds[i].fd, payload, &client);
				logInfo(cr);
				handleRequest(payload, cr);

				g_string_free(payload, TRUE);
				free(cr);

				// If this request contains 'Connection: close' header, then destroy it immediatly.
				if(cr->closeConnection) {
					printf("DESTROYINGG CONNECTION CLOSE\n");
					destroyConnection(i);
				}
			}
		}
	}

	// Clean up connections.
	for(int i = 0; i < fdSize; i++) {
		if(fds[i].fd >= 0) {
			close(fds[i].fd);
		}
	}

	return 0;
}
