#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;

static void stop(int);
static void handleclient(int);
static int route(const char *, const char *, const char **);
static int safepath(const char *);


int
main(void)
{
	int server, yes, dirfd;
	int exitcode = EXIT_FAILURE;

	struct sockaddr_in addr = {0};

	struct sigaction sa = {0};

	sa.sa_handler = stop;

	if (sigemptyset(&sa.sa_mask) < 0 || 
		sigaction(SIGINT, &sa, NULL) < 0) {
		perror("signal setup");
		return EXIT_FAILURE;
	}

	dirfd = open("files", O_RDONLY | O_DIRECTORY);
	if (dirfd < 0) {
		perror("open files");
		return EXIT_FAILURE;
	}

	server = socket(AF_INET, SOCK_STREAM, 0);
	if (server < 0) {
		perror("socket");
		goto cleandir;
	}
	printf("Socket created: fd=%d\n", server);
	
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(8080);
	
	yes = 1;
	if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0 ) {
		perror("setsockopt");
		goto cleanserver;
	}

	if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		goto cleanserver;
	}

	if (listen(server, 10) < 0) {
		perror("listen");
		goto cleanserver;
	}
	printf("listening on :8080\n");

	while (running) {
		int client = accept(server, NULL, NULL);

		if (client < 0) {
			if (errno == EINTR)
				continue;

			perror("accept");
			goto cleanserver;
		}
		printf("client fd %d\n", client);
		
		handleclient(client);
		close(client);
	}

	printf("Server shutting down\n");
	exitcode = EXIT_SUCCESS;

cleanserver:
	close(server);
cleandir:
	close(dirfd);
	return exitcode;
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
	char *start, *finish;
	char method[8], path[256], version[16], request[4096];
	int status;
	ssize_t n;
	size_t used = 0;
	char *end = NULL;
	const char *file = NULL;

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

	start = strchr(request, ' ');
	if (!start)
		return;
	if ((size_t)(start - request) >= sizeof(method))
		return;
	
	finish = strchr(++start, ' ');
	if (!finish)
		return;
	if ((size_t)(finish - start) >= sizeof(path))
		return;

	if (sscanf(request, "%7s %255s %15s", method, path, version) != 3)
		return;

	if (strcmp(version, "HTTP/1.0") &&
		strcmp(version, "HTTP/1.1"))
		return;

	printf("%s %s \n", method, path);

	status = route(method, path, &file);

	if (status == 200)
		printf("Serving: %s\n",file);
	else
		printf("HTTP error: %d\n", status);

}

static int
route(const char *method, const char *path, const char **file)
{
	if (strcmp(method, "GET"))
		return 405;
	if (!safepath(path))
		return 403;
	if (!strcmp(path, "/"))
		*file = "homepage.html";
	else
		*file = path + 1;
	return 200;
}

static int
safepath(const char *path)
{
	const char *p;

	if (*path != '/')
		return 0;

	for (p = path + 1; *p; p++) {
		if (*p == '\\' || (unsigned char)*p < 32 || (unsigned char)*p == 127 )
			return 0;

		if (*p == '.' &&  p[-1] == '/')
			return 0;
	}
	return 1;
}
