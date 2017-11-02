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
	bool keepAlive;
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

GString* handleHeader(GString* payload, clientRequest* cr, gsize contentLen) {
	GString* pl = g_string_sized_new(payload->allocated_len);
	g_string_assign(pl, payload->str);

	// Construct a basic header string.
	GString* head = g_string_sized_new(payload->allocated_len);

	if(g_str_has_prefix(pl->str, "POST")) {
		g_string_assign(head, cr->httpVersion);
		g_string_append(head, " 201 CREATED\r\n");
	}
	else if(g_str_has_prefix(pl->str, "GET") || g_str_has_prefix(pl->str, "HEAD")){
		g_string_assign(head, cr->httpVersion);
		g_string_append(head, " 200 OK\r\n");
		g_string_append(head, "Content-Type: text/html\r\n");
	}
	else {
		g_string_assign(head, cr->httpVersion);
		g_string_append(head, " 501 Not implemented\r\n");
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
			g_string_append_printf(head, "%s %lu\r\n", "Content-Length:", contentLen);
			continue;
		}

		// Check if request contains the 'Connection: close' header.
		if(g_strcmp0(lines[i], "Connection: close") == 0) {
			cr->closeConnection = TRUE;
		}
		else if(g_strcmp0(lines[i], "Connection: keep-alive") == 0) {
			cr->keepAlive = TRUE;
		}
		
		g_strstrip(lines[i]);
		g_string_append_printf(head, "%s\r\n", lines[i]);
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
	GString* ret = g_string_new("<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>HTTP-Server</title></head><body>");
	g_string_append(ret, hostUrl->str);
	g_string_append(ret, " ");
	g_string_append(ret, cr->ipAddr);
	g_string_append(ret, ":");
	g_string_append(ret, cr->port);
	g_string_append(ret, cr->requestBody->str);

	g_string_append(ret, "</body></html>\n");

	g_string_free(hostUrl, TRUE);

	return ret;
}

GString* constructColorHtml(gchar* col) {
	GString* ret = g_string_new("<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>HTTP-Server</title></head><body ");
	g_string_append(ret, "style='background-color:");
	g_string_append(ret, g_strsplit(col, "=", -1)[1]);
	g_string_append(ret, "'></body></html>\n");

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

void sendInvalidResponse(GString* payload, clientRequest* cr) {
	// If the request is not GET, POST or HEAD, then send Not implemented..
	GString* response = handleHeader(payload, cr, 0);

	send(cr->connfd, response->str, response->len, 0);
	g_string_free(response, TRUE);
}

void handleRequest(GString* payload, clientRequest* cr) {
	if(g_strcmp0(cr->method, "GET") == 0) { // GET request
		sendGetResponse(payload, cr);
		printf("GET..\n");
	}
	else if(g_strcmp0(cr->method, "HEAD") == 0) { // HEAD request
		handleHeader(payload, cr, 0);
		printf("HEAD..\n");
	}
	else if(g_strcmp0(cr->method, "POST") == 0) { // POST request
		sendPostResponse(payload, cr);
		printf("POST..\n");
	}
	else { // INVALID request
		sendInvalidResponse(payload, cr);
		printf("Invalid request: %s\n", cr->method);
	}
}

bool checkVersion(clientRequest* cr) {
	if(g_strcmp0(cr->httpVersion, "HTTP/1.1") == 0) {
		return TRUE;
	}
	
	return FALSE;
}

// Creates a new client request that holds information like the request method etc.
clientRequest* newClientRequest(int cfd, GString* payload, struct sockaddr_in* sock) {
	// Copy payload into fresh GString
	GString* pl = g_string_sized_new(payload->allocated_len);
	g_string_assign(pl, payload->str);
	clientRequest* cr = malloc(sizeof(clientRequest));
	
	// Split payload header into lines, then extract first and second lines for the information we need.
	gchar** lines = g_strsplit(pl->str, "\r\n", 0);
	gchar** firstLine = g_strsplit(lines[0], " ", 0);
	gchar** secondLine = g_strsplit(lines[1], " ", 0);

	// Retrieve the client port from socket.
	char portStr[sizeof(ntohs(sock->sin_port))];
	sprintf(portStr, "%d", ntohs(sock->sin_port));

	cr->connfd = cfd;
	cr->method = g_strstrip(firstLine[0]);
	cr->page = firstLine[1];
	cr->hostInfo = secondLine[1];
	cr->httpVersion = firstLine[2];
	cr->ipAddr = inet_ntoa(sock->sin_addr);
	cr->port = portStr;
	cr->closeConnection = FALSE;
	cr->keepAlive = FALSE;
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

int main(int argc, char *argv[])
{
	if(argc != 2) {
		printf("Usage: %s <port> \n", argv[0]);
		exit(1);
	}

	int sockfd, status, timeout, k, j, opt=1, fdSize=1, connfd=-1, currSize=0;
	struct sockaddr_in server, client;
	struct pollfd fds[100];
	int myPort = atoi(argv[1]);
	char buff[2048];
	socklen_t cliLen = sizeof(client);
	bool closeConnection=FALSE, compressArr=FALSE;
	clientRequest* cr;

	timeout = (30 * 1000); // 30 second timeout

	// Set up socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	server.sin_port = htons(myPort);

	status = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));

	if(status < 0) {
		perror("Setting socket options failed");
		close(sockfd);
		return EXIT_FAILURE;
	}

	status = ioctl(sockfd, FIONBIO, (char *)&opt);

	if(status < 0) {
		perror("ioctl() failed");
		close(sockfd);
		return EXIT_FAILURE;
	}

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
	while(1) {	
		status = poll(fds, fdSize, timeout);

		if(status < 0) {
			perror("poll() failed");
			return EXIT_FAILURE;
		}

		if(status == 0) {
			// If poll status is 0 then connection timed out. 
			continue;
		}

		currSize = fdSize;
		for(int i = 0; i < currSize; i++) {
	
			if(fds[i].revents == 0) {
				continue;
			}

			if(fds[i].revents != POLLIN) {
				break;
			}

			if(fds[i].fd == sockfd) {
				// Accept incoming connections and add their file descriptors to array.
				do {
					connfd = accept(sockfd, (struct sockaddr *) &client, &cliLen);

					if(connfd < 0) {
						if(errno != EWOULDBLOCK) {
							perror("accept() failed");
							return EXIT_FAILURE;
						}
						break;
					}

					fds[fdSize].fd = connfd;
					fds[fdSize].events = POLLIN;
					fdSize++;
				} while(connfd != -1);
			}
			else {
				closeConnection = FALSE;

				while(TRUE) {
					// Read data from client connection into buffer.
					memset(buff, 0, 2048);
					status = recv(fds[i].fd, buff, sizeof(buff), 0);

					if(status < 0) {
						if(errno != EWOULDBLOCK) {
							perror("read() failed");
							closeConnection = TRUE;
						}
						break;
					}

					if(status == 0) {
						closeConnection = TRUE;
						break;
					}

					// Create a payload from buffer and initialize a new client request.
					GString* payload = g_string_new(buff);
					cr = newClientRequest(fds[i].fd, payload, &client);
					logInfo(cr);
					handleRequest(payload, cr);

					if(cr->keepAlive) {
						continue;
					}
					else {
						closeConnection = TRUE;
						break;
					}

					// If this request version is HTTP/1.0, or contains the 'Connection: close' header,
					// then close the connection immediately
					/*if((!checkVersion(cr) && !cr->keepAlive) || (checkVersion(cr) && cr->closeConnection)) {
						closeConnection = TRUE;
						break;
					}*/
				}

				if(closeConnection) {
					// Close the connection and shrink the FD-array.
					printf("CLOSING CONNECTION NO: %d\n", fds[i].fd);
					close(fds[i].fd);
					fds[i].fd = -1;
					compressArr = TRUE;
				}
			}
		}

		if(compressArr) {
			compressArr = FALSE;

			for (k = 0; k < fdSize; k++) {
				if (fds[k].fd == -1) {
					for(j = k; j < fdSize; j++) {
						fds[j].fd = fds[j + 1].fd;
					}
					k--;
					fdSize--;
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

	free(cr);

	return 0;
}
