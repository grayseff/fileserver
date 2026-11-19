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

#ifdef __FreeBSD__
#include <sys/capsicum.h>
#endif

static volatile sig_atomic_t running = 1;

static void stop(int);
static void handleclient(int, int);
static int route(char *, const char **);
static int safepath(const char *);
static int sendresponse(int, int, const char *, int);

int
main(void)
{
	int server, yes, dirfd;
	int exitcode = EXIT_FAILURE;

	struct sockaddr_in6 addr = {0};

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

	server = socket(AF_INET6, SOCK_STREAM, 0);
	if (server < 0) {
		perror("socket");
		goto cleandir;
	}
	printf("Socket created: fd=%d\n", server);
	
	addr.sin6_family = AF_INET6;
	addr.sin6_addr= in6addr_loopback;
	addr.sin6_port = htons(8080);
	
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
	printf("listening on [::1]:8080\n");

#ifdef __FreeBSD__
	cap_rights_t rights;

	cap_rights_init(&rights, CAP_LOOKUP, CAP_READ);
	if (cap_rights_limit(dirfd, &rights) < 0) {
		perror("cap rights limit");
		goto cleanserver;
	}

	if (cap_enter() < 0) {
		perror("cap_enter");
		goto cleanserver;
	}
	printf("Capsicum engaged: Hot peppers, cold permissions\n");
#endif

	while (running) {
		int client = accept(server, NULL, NULL);

		if (client < 0) {
			if (errno == EINTR)
				continue;

			perror("accept");
			goto cleanserver;
		}
		printf("client fd %d\n", client);
		
		handleclient(client, dirfd);
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
handleclient(int client, int dirfd)
{
	char request[4096];
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

	status = route(request, &file);
	if (sendresponse(client, dirfd, file, status) < 0)
		fprintf(stderr, "Serving failed\n");
	return;
}

static int
route(char *request, const char **file)
{
	char *start, *finish;
	char *method, *path, *version;

	method = request;
	start = strchr(request, ' ');
	if (!start) {
		*file = "400.html";
		return 400;
	}
	*start = '\0';
	path = start + 1;
	finish = strchr(path, ' ');
	if (!finish) {
		*file = "400.html";
		return 400;
	}
	*finish = '\0';
	version = finish +1;

	if ((size_t)(finish - path) > 255) {
		*file = "414.html";
		return 414;
	}

	if (strcmp(version, "HTTP/1.0") &&
		strcmp(version, "HTTP/1.1")) {
		*file = "400.html";
		return 400;
	}

	printf("%s %s \n", method, path);

	if (strcmp(method, "GET")) {
		*file = "405.html";
		return 405;
	}
	if (!safepath(path)) {
		*file = "403.html";
		return 403;
	}
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

static int
sendresponse(int client, int dirfd, const char *file, int status)
{
	int fd, len;
	ssize_t n;
	size_t sent = 0;
	const char *reason;
	char header[256],buffer[4096];

	
	fd = openat(dirfd, file, O_RDONLY);
	if (fd < 0)
		status = 404;

	switch (status) {
	case 200:
		reason = "OK";
		break;
	case 400:
		reason = "Bad Request";
		break;
	case 403:
		reason = "Forbidden";
		break;
	case 404:
		reason = "Not Found";
		break;
	case 405:
		reason = "Method Not Allowed";
		break;
	case 414:
		reason = "URI Too Long";
		break;
	default:
		reason = "Internal Server Error";
		status = 500;
		break;
	}
	len = snprintf(header, sizeof(header),
		"HTTP/1.1 %d %s\r\n"
		"Content-Type: text/html\r\n"
		"Connection: close\r\n"
		"\r\n",
		status, reason);
	if (len < 0 || (size_t)len >= sizeof(header)) {
		if (fd >= 0)
			close(fd);
		return -1;
	}	

	while (sent < (size_t)len) {
		n = write(client, header + sent, (size_t)len-sent);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			perror("write");
			close(fd);
			return -1;
		}
		if (n==0) {
			close(fd);
			return -1;
		}
		sent += (size_t)n;
	}

	if (status == 404) {
		static const char msg[] =
			"<h1>404: These aren't the pages you're looking for</h1>\n";
		write(client, msg, sizeof(msg) -1);
		return 0;
	}
	printf("Reading %s (fd = %d)\n", file, fd);
	while (1) {
		n = read(fd, buffer, sizeof(buffer));
		if (n < 0) {
			if (errno == EINTR)
				continue;
			perror("read file");
			close(fd);
			return -1;
		}
		if (n == 0)
			break;
		/* write bytes */
		len = (size_t)n;
		sent = 0;
		while (sent < (size_t)len) {
			n = write(client, buffer+sent, (size_t)len - sent);
			if (n < 0) {
				if (errno == EINTR)
					continue;
				perror("write");
				close(fd);
				return -1;
			}
			if (n==0) {
				close(fd);
				return -1;
			}
			sent += (size_t)n;
		}
	}	
	close(fd);
	return 0;

}
