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
#include <zlib.h>
#include <stdint.h>

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
		printf("\nClient connected\n");
	}

	close(server_sock);

	return 0;
}

unsigned char *zlibCompress(const unsigned char *src, size_t src_len, size_t *out_len)
{
	if (src == NULL || src_len == 0 || out_len == NULL)
	{
		return NULL;
	}

	uLong dest_len = compressBound((uLong)src_len) * 2;
	unsigned char *dest = (unsigned char *)malloc(dest_len);
	if (dest == NULL)
	{
		return NULL;
	}

	z_stream zs;
	zs.zalloc = Z_NULL;
	zs.zfree = Z_NULL;
	zs.opaque = Z_NULL;
	zs.avail_in = (uInt)src_len;
	zs.next_in = (Bytef *)src;
	zs.avail_out = (uInt)(dest_len);
	zs.next_out = (Bytef *)dest;
	deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 | 16, 8,
				 Z_DEFAULT_STRATEGY);
	deflate(&zs, Z_FINISH);
	deflateEnd(&zs);
	*out_len = zs.total_out;

	return dest;
}

static int send_all(int client_sock, const void *buffer, size_t length)
{
	const unsigned char *data = buffer;
	while (length > 0)
	{
		ssize_t sent = send(client_sock, data, length, 0);
		if (sent < 0)
		{
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (sent == 0)
			return -1;

		data += sent;
		length -= (size_t)sent;
	}
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
		char *accept_encoding = strstr(request_buf, "Accept-Encoding: ");
		char *content = path + 6;
		int supportsCompression = 0;
		size_t content_len = strlen(content);

		if (accept_encoding)
		{
			accept_encoding += strlen("Accept-Encoding: ");

			// Remove \r\n
			char *end = strstr(accept_encoding, "\r\n");
			if (end)
				*end = '\0';

			// Comma Seperated List
			char *token = strtok(accept_encoding, ",");
			while (token != NULL)
			{
				if (strncmp(token, "gzip", 4) == 0)
				{
					supportsCompression = 1;
					break;
				}
				accept_encoding += strlen(token) + 2;
				token = strtok(NULL, ", ");
			}
		}

		if (supportsCompression)
		{
			size_t compressed_len = 0;
			unsigned char *compressed_data = zlibCompress((const unsigned char *)content, content_len, &compressed_len);
			if (compressed_data == NULL)
			{
				snprintf(response, sizeof(response), "HTTP/1.1 500 Internal Server Error\r\n\r\n");
			}
			else
			{
				int header_len = snprintf(response, sizeof(response),
								  "HTTP/1.1 200 OK\r\n"
								  "Content-Type: text/plain\r\n"
								  "Content-Encoding: gzip\r\n"
								  "Content-Length: %zu\r\n\r\n",
								  compressed_len);
				if (header_len < 0 || (size_t)header_len >= sizeof(response) ||
					send_all(client_sock, response, (size_t)header_len) != 0 ||
					send_all(client_sock, compressed_data, compressed_len) != 0)
				{
					free(compressed_data);
					close(client_sock);
					return NULL;
				}
				free(compressed_data);
				close(client_sock);
				return NULL;
			}
		}
		else
		{
			format = "HTTP/1.1 200 OK\r\n"
					 "Content-Type: text/plain\r\n"
					 "Content-Length: %zu\r\n\r\n%s";
			sprintf(response, format, content_len, content);
		}
		printf("\nresponse data : \n%s", response);
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
			printf("\nresponse data : \n%s", response);
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