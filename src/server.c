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
#include <pthread.h>

#define PORT "8080"
#define BACKLOG 10
#define THREAD_POOL_SIZE 4
#define QUEUE_MAX_SIZE 20

int work_queue[QUEUE_MAX_SIZE];
int queue_count = 0;
int queue_head = 0;
int queue_tail = 0;

pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

void handle_client(int client_fd) {
    char buffer[2048];
    memset(buffer, 0, sizeof buffer);

    int bytes_recieved = recv(client_fd, buffer, sizeof(buffer) -1, 0);
    if (bytes_recieved <= 0) {
        if (bytes_recieved == -1) perror("recv failed");
        close(client_fd);
        return;
    }

    printf("Request recieved:\n%s\n", buffer);

    char method[16];
    char path[256];

    int tokens_parsed = sscanf(buffer, "%15s %255s", method, path);
    if (tokens_parsed < 2) {
        fprintf(stderr, "Malformed HTTP request line\n");
        close(client_fd);
        return;
    }

    if (strcmp(method, "GET") != 0) {
        char *bad_method_response = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n";
        send(client_fd, bad_method_response, strlen(bad_method_response), 0);
        close(client_fd);
        return;
    }

    if (strcmp(path, "/") == 0) {
        strcpy(path, "/index.html");
    }

    printf("Client requested file: %s\n", path);
    
    char full_path[270];
    snprintf(full_path, sizeof(full_path), ".%s", path);

    FILE *file = fopen(full_path, "rb");
    if (file == NULL) {
        char *not_found_header = 
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: 48\r\n"
            "\r\n"
            "<html><body><h1>404 File Not Found</h1></body></html>";

        send(client_fd, not_found_header, strlen(not_found_header), 0);
        close(client_fd);
        return;
    }

    fseek(file, 0 , SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char header_buffer[512];
    int header_len = snprintf(header_buffer, sizeof(header_buffer),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %ld\r\n"
        "Connection: clse\r\n"
        "\r\n",
        file_size);

    send(client_fd, header_buffer, header_len, 0);

    char file_chunk[1024];
    size_t bytes_read;
    while ((bytes_read = fread(file_chunk, 1, sizeof(file_chunk), file)) > 0) {
        send(client_fd, file_chunk, bytes_read, 0);
    }

    fclose(file);
    close(client_fd);
    printf("Successfully served %s (%ld bytes) to client.\n", full_path, file_size);
}

void queue_push(int client_fd) {
    pthread_mutex_lock(&queue_mutex);

    if (queue_count >= QUEUE_MAX_SIZE) {
        fprintf(stderr, "Work queue full! Dropping connection request.\n");
        close(client_fd);
        pthread_mutex_unlock(&queue_mutex);
        return;
    }

    work_queue[queue_tail] = client_fd;
    queue_tail = (queue_tail + 1) % QUEUE_MAX_SIZE;
    queue_count++;

    pthread_cond_signal(&queue_cond);

    pthread_mutex_unlock(&queue_mutex);
}

int queue_pop(void) {
    pthread_mutex_lock(&queue_mutex);

    while (queue_count == 0) {
        pthread_cond_wait(&queue_cond, &queue_mutex);
    }

    int client_fd = work_queue[queue_head];
    queue_head = (queue_head + 1) % QUEUE_MAX_SIZE;
    queue_count--;

    pthread_mutex_unlock(&queue_mutex);
    return client_fd;
}

void* worker_thread_cycle(void* arg) {
    (void)arg;

    while(1) {
        int client_fd = queue_pop();
        handle_client(client_fd);
    }

    return NULL;
}

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

    pthread_t thread_pool[THREAD_POOL_SIZE];
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        if (pthread_create(&thread_pool[i], NULL, worker_thread_cycle, NULL) != 0) {
            perror("Failed to create worker thread");
            return 1;
        }
    }

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

        queue_push(client_fd);
    }

    close(server_fd);
    return 0;
}