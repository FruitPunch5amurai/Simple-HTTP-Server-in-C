#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <unistd.h>

#define BUFFER_SIZE 1024

void *http_handler(void *args);

typedef struct
{
	int client_sock;
	const char *directory;
} ThreadArgs;

int main(int argc, char *argv[])
{
	const char *directory = "";
	for (int i = 1; i < argc - 1; i++)
	{
		if (strcmp(argv[i], "--directory") == 0)
		{
			directory = argv[i + 1];
			printf("Directory root: %s\n", directory);
			break;
		}
	}

	char cwd[1024]; // Buffer to store the path string

	// getcwd takes the buffer and its maximum size as arguments
	if (getcwd(cwd, sizeof(cwd)) != NULL)
	{
		printf("Current working directory: %s\n", cwd);
	}
	else
	{
		perror("getcwd() error");
	}

	// Disable output buffering
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	int server_sock, client_addr_len;
	struct sockaddr_in client_addr;
	pthread_t tid;

	server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sock == -1)
	{
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}

	// Since the tester restarts your program quite often, setting SO_REUSEADDR
	// ensures that we don't run into 'Address already in use' errors
	int reuse = 1;
	if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) <
		0)
	{
		printf("SO_REUSEADDR failed: %s \n", strerror(errno));
		return 1;
	}

	struct sockaddr_in serv_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(4221),
		.sin_addr = {htonl(INADDR_ANY)},
	};

	if (bind(server_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0)
	{
		printf("Bind failed: %s \n", strerror(errno));
		return 1;
	}

	int connection_backlog = 5;
	if (listen(server_sock, connection_backlog) != 0)
	{
		printf("Listen failed: %s \n", strerror(errno));
		return 1;
	}

	printf("Waiting for a client to connect...\n");

	// Handle Connections
	while (1)
	{
		client_addr_len = sizeof(client_addr);
		const int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);
		if (client_sock < 0)
			break;

		ThreadArgs *args = malloc(sizeof(*args));
		if (args == NULL)
		{
			close(client_sock);
			continue;
		}
		args->client_sock = client_sock;
		args->directory = directory;

		pthread_create(&tid, NULL, http_handler, args);
		pthread_detach(tid);
		printf("Client connected\n");
	}

	close(server_sock);

	return 0;
}

void *http_handler(void *args)
{
	ThreadArgs *data = (ThreadArgs *)args;
	const int client_sock = data->client_sock;
	const char *directory = data->directory;
	free(data);

	char request_buf[BUFFER_SIZE];

	ssize_t bytes_read = read(client_sock, request_buf, BUFFER_SIZE - 1);
	if (bytes_read < 0)
	{
		printf("Read failed: %s   \n", strerror(errno));
		close(client_sock);
		return NULL;
	}
	request_buf[bytes_read] = '\0';

	char method[8];
	char path[BUFFER_SIZE];
	char *format = NULL;

	sscanf(request_buf, "%s %s", method, path);

	char response[BUFFER_SIZE];

	if (strncmp(path, "/echo/", 6) == 0)
	{
		char *content = path + 6;
		format = "HTTP/1.1 200 OK\r\n"
				 "Content-Type: text/plain\r\n"
				 "Content-Length: %zu\r\n\r\n%s";

		sprintf(response, format, strlen(content), content);
		printf("response data : \n%s", response);
	}
	else if (strncmp(path, "/user-agent", 11) == 0)
	{
		char *user_agent = strstr(request_buf, "User-Agent: ");
		if (user_agent)
		{
			user_agent += strlen("User-Agent: ");

			// Remove \r\n so we can get an accurate length
			char *end = strstr(user_agent, "\r\n");
			if (end)
				*end = '\0';

			format = "HTTP/1.1 200 OK\r\n"
					 "Content-Type: text/plain\r\n"
					 "Content-Length: %lu\r\n\r\n%s";

			sprintf(response, format, strlen(user_agent), user_agent);
			printf("response data : \n%s", response);
		}
	}
	else if (strncmp(path, "/files/", 7) == 0)
	{
		const char *filename = path + 7;
		char full_path[BUFFER_SIZE];
		sprintf(full_path, "%s%s", directory, filename);

		if (strncmp(method, "POST", 4) == 0)
		{
			char *body = strstr(request_buf, "\r\n\r\n");
			printf("%s\n", body);
			if (body)
			{
				body += 4;
				FILE *fp = fopen(full_path, "wb");
				if (fp)
				{
					fwrite(body, 1, strlen(body), fp);
					fclose(fp);
					strcpy(response, "HTTP/1.1 201 Created\r\n\r\n");
				}
			}
		}
		else
		{
			FILE *f = fopen(full_path, "rb");
			if (f)
			{
				fseek(f, 0, SEEK_END);
				int length = ftell(f);
				fseek(f, 0, SEEK_SET);
				char *buffer = malloc(length);
				fread(buffer, 1, length, f);
				fclose(f);
				sprintf(response,
						"HTTP/1.1 200 OK\r\nContent-Type: "
						"application/octet-stream\r\nContent-Length: "
						"%ld\r\n\r\n%s",
						length, buffer);
			}
			else
			{
				strcpy(response, "HTTP/1.1 404 Not Found\r\nContent-Type: "
								 "text/plain\r\n\r\n");
			}
		}
	}
	else if (strcmp(path, "/") == 0)
	{
		strcpy(response, "HTTP/1.1 200 OK\r\n\r\n");
	}
	else
	{
		strcpy(response, "HTTP/1.1 404 Not Found\r\n\r\n");
	}

	send(client_sock, response, strlen(response), 0);

	close(client_sock);
	return NULL;
}