// server.c - Multi-threaded with LIST function
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8081
#define MAX_PEERS 10
#define MAX_FILES 10
#define MAX_FILENAME_LEN 50
#define BUFFER_SIZE 4096 // Increased buffer for larger file lists


typedef struct {
    char ip_addr[INET_ADDRSTRLEN];
    int port;
    char files[MAX_FILES][MAX_FILENAME_LEN];
    int file_count;
    int is_active;
} PeerInfo;


PeerInfo peer_list[MAX_PEERS];
int peer_count = 0;
pthread_mutex_t peer_list_mutex = PTHREAD_MUTEX_INITIALIZER;

void register_peer(char* ip, int port, char* file_list_str) {
    pthread_mutex_lock(&peer_list_mutex);
    if (peer_count >= MAX_PEERS) {
        printf("Peer list is full.\n");
        pthread_mutex_unlock(&peer_list_mutex);
        return;
    }

    strcpy(peer_list[peer_count].ip_addr, ip);
    peer_list[peer_count].port = port;
    peer_list[peer_count].is_active = 1;
    peer_list[peer_count].file_count = 0;

    char* files_copy = strdup(file_list_str);
    char* token = strtok(files_copy, ",");
    while (token != NULL && peer_list[peer_count].file_count < MAX_FILES) {
        strncpy(peer_list[peer_count].files[peer_list[peer_count].file_count], token, MAX_FILENAME_LEN - 1);
        peer_list[peer_count].files[peer_list[peer_count].file_count][MAX_FILENAME_LEN - 1] = '\0';
        peer_list[peer_count].file_count++;
        token = strtok(NULL, ",");
    }
    free(files_copy);

    printf("Registered Peer: %s:%d with %d files.\n", ip, port, peer_list[peer_count].file_count);
    peer_count++;
    pthread_mutex_unlock(&peer_list_mutex);
}

char* find_file_owner(const char* filename) {
    static char owner_info[100];
    pthread_mutex_lock(&peer_list_mutex);
    for (int i = 0; i < peer_count; i++) {
        if (peer_list[i].is_active) {
            for (int j = 0; j < peer_list[i].file_count; j++) {
                if (strcmp(peer_list[i].files[j], filename) == 0) {
                    snprintf(owner_info, sizeof(owner_info), "%s:%d", peer_list[i].ip_addr, peer_list[i].port);
                    pthread_mutex_unlock(&peer_list_mutex);
                    return owner_info;
                }
            }
        }
    }
    pthread_mutex_unlock(&peer_list_mutex);
    return "NOT_FOUND";
}

void unregister_peer(const char* ip, int port) {
    pthread_mutex_lock(&peer_list_mutex);
    for (int i = 0; i < peer_count; i++) {
        if (peer_list[i].is_active && strcmp(peer_list[i].ip_addr, ip) == 0 && peer_list[i].port == port) {
            peer_list[i].is_active = 0; // Mark as inactive
            printf("Unregistered Peer: %s:%d\n", ip, port);
            break;
        }
    }
    pthread_mutex_unlock(&peer_list_mutex);
}

void* handle_client(void* socket_desc) {
    int new_socket = *(int*)socket_desc;
    free(socket_desc);

    char buffer[BUFFER_SIZE] = {0};
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    getpeername(new_socket, (struct sockaddr*)&address, &addrlen);
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &address.sin_addr, client_ip, sizeof(client_ip));

    if (read(new_socket, buffer, BUFFER_SIZE) < 0) {
        perror("read failed");
        close(new_socket);
        return NULL;
    }

    printf("Received from client %s: %s\n", client_ip, buffer);
    char* buffer_copy = strdup(buffer);
    char *command = strtok(buffer_copy, " ");

    if (command == NULL) {
        // Invalid command
    } else if (strcmp(command, "REGISTER") == 0) {
        int port = atoi(strtok(NULL, " "));
        char *files = strtok(NULL, "");
        if (files) {
            register_peer(client_ip, port, files);
            send(new_socket, "REGISTERED", strlen("REGISTERED"), 0);
        }
    } else if (strcmp(command, "QUERY") == 0) {
        char* filename = strtok(NULL, "");
        if (filename) {
            filename[strcspn(filename, "\n\r")] = 0;
            char* owner_info = find_file_owner(filename);
            send(new_socket, owner_info, strlen(owner_info), 0);
        }
    } else if (strcmp(command, "EXIT") == 0) {
        int port = atoi(strtok(NULL, " "));
        unregister_peer(client_ip, port);
        send(new_socket, "GOODBYE", strlen("GOODBYE"), 0);
    } 
    // ***** NEW CODE BLOCK STARTS HERE *****
    else if (strcmp(command, "LIST") == 0) {
        char file_list_response[BUFFER_SIZE] = {0};
        char temp_buffer[256];

        pthread_mutex_lock(&peer_list_mutex);
        for (int i = 0; i < peer_count; i++) {
            if (peer_list[i].is_active) {
                for (int j = 0; j < peer_list[i].file_count; j++) {
                    snprintf(temp_buffer, sizeof(temp_buffer), "%s [hosted by %s:%d]\n", 
                             peer_list[i].files[j], peer_list[i].ip_addr, peer_list[i].port);
                    if (strlen(file_list_response) + strlen(temp_buffer) < sizeof(file_list_response)) {
                        strcat(file_list_response, temp_buffer);
                    }
                }
            }
        }
        pthread_mutex_unlock(&peer_list_mutex);

        if (strlen(file_list_response) > 0) {
            file_list_response[strlen(file_list_response) - 1] = '\0';
        }

        if (strlen(file_list_response) == 0) {
            send(new_socket, "NO_FILES_AVAILABLE", strlen("NO_FILES_AVAILABLE"), 0);
        } else {
            send(new_socket, file_list_response, strlen(file_list_response), 0);
        }
    }
    // ***** NEW CODE BLOCK ENDS HERE *****

    free(buffer_copy);
    close(new_socket);
    return NULL;
}

int main() {

    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, MAX_PEERS) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", PORT);

    while(1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int* new_socket = malloc(sizeof(int));
        if ((*new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_len)) < 0) {
            perror("accept");
            free(new_socket);
            continue;
        }

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, (void*)new_socket) != 0) {
            perror("pthread_create failed");
            free(new_socket);
        }
        pthread_detach(tid);
    }

    close(server_fd);

    return 0;
}