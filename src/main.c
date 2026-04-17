#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>

#include "support.h"
#include "commands.h"

#define MAX_CLIENTS 1024
#define BUF_SIZE 4096

int main() {
	//Disable output buffering
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	
	int server_fd, client_addr_len;
	struct sockaddr_in client_addr;
	
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}
	
	// Since the tester restarts your program quite often, setting SO_REUSEADDR
	// ensures that we don't run into 'Address already in use' errors
	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		printf("SO_REUSEADDR failed: %s \n", strerror(errno));
		return 1;
	}
	
	struct sockaddr_in serv_addr = { .sin_family = AF_INET ,
									 .sin_port = htons(6379),
									 .sin_addr = { htonl(INADDR_ANY) },
									};
	
	if (bind(server_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
		printf("Bind failed: %s \n", strerror(errno));
		return 1;
	}
	
	int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0) {
		printf("Listen failed: %s \n", strerror(errno));
		return 1;
	}
	
	printf("Waiting for a client to connect...\n");
	client_addr_len = sizeof(client_addr);

	char buf[BUF_SIZE];
	
	struct pollfd fds[MAX_CLIENTS];
	int nfds = 0;

	fds[0].fd = server_fd;
	fds[0].events = POLLIN;
	nfds = 1;

	int server_cycle_ms = 10;
	long long last_check = 0;

	while(1) {
		long long now = current_time_ms();
		if (now - last_check >= server_cycle_ms) {
			process_timeouts(now);
			last_check = now;
		}

		int ready = poll(fds, nfds, server_cycle_ms);

		if (ready == -1) {
			printf("Poll failed: %s\n", strerror(errno));

		} else if (ready > 0) {

			if (fds[0].revents & POLLIN) {
				int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);
				
				if (client_fd == -1) {
					printf("Accept error: %s\n", strerror(errno));
				} else if (client_fd >= 0) {
					fds[nfds].fd = client_fd;
					fds[nfds].events = POLLIN;
					nfds++;
					printf("Client connected, id: %d\n", client_fd);
				}
			}

			for (int i = 1; i < nfds; i++) {
				if (fds[i].revents & POLLIN) {

					int n = read(fds[i].fd, buf, BUF_SIZE - 1);
					
					if (n == -1) {
						printf("Read error: %s \n", strerror(errno));
					} else if (n > 0) {
						buf[n] = '\0';
						char **client_request = parse_message(buf);
						if (client_request == NULL) {
							continue;
						} 

						handle_command(fds[i].fd, client_request);
						free(client_request);
					} else if (n == 0) {
						close(fds[i].fd);
						fds[i] = fds[nfds - 1];
						nfds--;
						i--;
						continue;
					}
				}
			}
		}
	}
	
	close(server_fd);
	return 0;
}
