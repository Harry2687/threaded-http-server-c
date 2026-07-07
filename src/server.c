#define _XOPEN_SOURCE 700
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define PORT "8080"
#define BACKLOG 10

int main(void) {
    int status;
    int server_fd, client_fd;
    struct addrinfo hints, *res;
    struct sockaddr_storage client_addr;
    socklen_t sin_size;
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((status = getaddrinfo(NULL, PORT, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        return 1;
    }

    server_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (server_fd == -1) {
        perror("server: socket allocation failed");
        freeaddrinfo(res);
        return 1;
    }

    if (bind(server_fd, res->ai_addr, res->ai_addrlen) == -1) {
        perror("server: bind failed");
        close(server_fd);
        freeaddrinfo(res);
        return 1;
    }

    freeaddrinfo(res);

    if (listen(server_fd, BACKLOG) == -1) {
        perror("listen failed");
        close(server_fd);
        return 1;
    }

    printf("Server listening cleanly on port %s...\n", PORT);

    while(1) {
        sin_size = sizeof client_addr;
        
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &sin_size);
        if (client_fd == -1) {
            perror("accept failed");
            continue;
        }

        printf("Server: Got connection request!\n");

        char buffer[1024];
        memset(buffer, 0, sizeof buffer);

        int bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received > 0) {
            printf("Received: %s\n", buffer);
            
            send(client_fd, buffer, bytes_received, 0);
        } else if (bytes_received == -1) {
            perror("recv failed");
        }

        close(client_fd);
    }

    close(server_fd);
    return 0;
}