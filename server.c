#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;

static void stop(int);
static void handleclient(int);
static const char * route(const char *, const char *);


int
main(void)
{
	int server, yes;
	struct sockaddr_in addr = {0};

	struct sigaction sa = {0};

	sa.sa_handler = stop;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);

	server = socket(AF_INET, SOCK_STREAM, 0);
	if (server < 0) {
		perror("socket");
		return EXIT_FAILURE;
	}
	printf("Socket created: fd=%d\n", server);
	
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(8080);
	
	yes = 1;
	if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0 ) {
		perror("setsockopt");
		close(server);
		return EXIT_FAILURE;
	}

	if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		close(server);
		return EXIT_FAILURE;
	}

	if (listen(server, 10) < 0) {
		perror("listen");
		close(server);
		return EXIT_FAILURE;
	}
	printf("listening on :8080\n");

	while (running) {
		int client = accept(server, NULL, NULL);

		if (client < 0) {
			if (errno == EINTR)
				continue;

			perror("accept");
			continue;
		}
		printf("client fd %d\n", client);
		
		handleclient(client);
		close(client);
	}

	printf("Server shutting down\n");
	close(server);
	return EXIT_SUCCESS;
}

static void
stop(int signo)
{
	(void)signo;
	running = 0;
}

static void
handleclient(int client)
{
	const char *response;
	char *end = NULL;
	char method[8], path[256], request[4096];
	ssize_t n;
	size_t used = 0;

	while (used < sizeof(request) - 1) {
		n = read(client, request + used,
			sizeof(request) - 1 - used);
		if (n < 0) {
			if (errno == EINTR) {
				if (!running)
					return;
				continue;
			}
			perror("read");
			return;
		}
		if (n == 0)
			return;
		used += (size_t)n;
		request[used] = '\0';

		end = strstr(request, "\r\n");
		if (end)
			break;
	
	}
	if (!end)
		return;
	*end = '\0';

	if (sscanf(request, "%7s %255s", method, path) != 2)
		return;

	printf("%s %s \n", method, path);

	response = route(method, path);

	if (write(client, response, strlen(response)) < 0)
		perror("write");

}

static const char *
route(const char *method, const char *path)
{
	if (strcmp(method, "GET") != 0) {
		return "HTTP/1.0 405 Method Not Allowed\r\n"
		"Content-Type: text/html\r\n"
		"Allow: GET\r\n"
		"Connection: close\r\n"
		"\r\n"
		"<h1>405 Method Not Allowed</h1>\n";
	}
	if (strcmp(path, "/") == 0) { 
		return "HTTP/1.0 200 OK\r\n"
		"Content-Type: text/html\r\n"
		"Connection: close\r\n"
		"\r\n"
		"<h1>Hello from FreeBSD!</h1>\n";
	}
	return "HTTP/1.0 404 Not Found\r\n"
		"Content-Type: text/html\r\n"
		"Connection: close\r\n"
		"\r\n"
		"<h1>404 Not Found</h1>\n";
}
