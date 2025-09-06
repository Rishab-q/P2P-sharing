// client.c - Corrected Version
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <pthread.h>
#include <libgen.h>

#define SERVER_PORT 8080
#define BUFFER_SIZE 4096

char SERVER_IP[INET_ADDRSTRLEN];
int MY_LISTEN_PORT;
char* FILES_DIRECTORY_PATH;

void* peer_server_thread(void* arg) {
    (void)arg; // Mark parameter as intentionally unused

    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("peer server socket failed");
        pthread_exit(NULL);
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("peer server setsockopt");
        pthread_exit(NULL);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(MY_LISTEN_PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("peer server bind failed");
        pthread_exit(NULL);
    }
    if (listen(server_fd, 5) < 0) {
        perror("peer server listen");
        pthread_exit(NULL);
    }

    printf("[Peer Server] Listening for downloads on port %d\n", MY_LISTEN_PORT);

    while(1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0) {
            perror("peer server accept");
            continue;
        }
        char buffer[BUFFER_SIZE] = {0};
        read(new_socket, buffer, BUFFER_SIZE);
        buffer[strcspn(buffer, "\n\r")] = 0;
        printf("[Peer Server] Received download request for: %s\n", buffer);

        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%s", FILES_DIRECTORY_PATH, buffer);

        FILE *fp = fopen(filepath, "rb");
        if (fp == NULL) {
            char *errmsg = "ERROR: File not found.";
            send(new_socket, errmsg, strlen(errmsg), 0);
        } else {
            char file_buffer[BUFFER_SIZE];
            size_t bytes_read;
            while ((bytes_read = fread(file_buffer, 1, BUFFER_SIZE, fp)) > 0) {
                if(send(new_socket, file_buffer, bytes_read, 0) < 0){
                    perror("send file failed");
                    break;
                }
            }
            fclose(fp);
            printf("[Peer Server] Finished sending file %s.\n", buffer);
        }
        close(new_socket);
    }
    pthread_exit(NULL);
}

void send_to_server(const char* message, char* response) {
    int sock;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return;
    }
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);

    if(inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("Invalid server address");
        close(sock);
        return;
    }
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed to Central Server");
        close(sock);
        return;
    }

    send(sock, message, strlen(message), 0);
    read(sock, buffer, BUFFER_SIZE - 1);
    buffer[BUFFER_SIZE - 1] = '\0';
    strcpy(response, buffer);
    close(sock);
}


int main(int argc, char *argv[]) {
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Usage: %s <listening-port> <path-to-files-directory> [server-ip]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    MY_LISTEN_PORT = atoi(argv[1]);
    FILES_DIRECTORY_PATH = argv[2];

     printf("DEBUG: Program is attempting to open directory: '%s'\n", FILES_DIRECTORY_PATH);

    DIR *d = opendir(FILES_DIRECTORY_PATH);
    if (!d) {
        perror("ERROR: Could not open files directory");
        exit(EXIT_FAILURE);
    }
    if (argc == 4) {
        strncpy(SERVER_IP, argv[3], INET_ADDRSTRLEN - 1);
        SERVER_IP[INET_ADDRSTRLEN - 1] = '\0';
    } else {
        strncpy(SERVER_IP, "127.0.0.1", INET_ADDRSTRLEN);
    }

    printf("Connecting to server at %s\n", SERVER_IP);
    fflush(stdout);  // Flush immediately

    pthread_t server_tid;
    if (pthread_create(&server_tid, NULL, peer_server_thread, NULL) != 0) {
        perror("Failed to create peer server thread");
        exit(EXIT_FAILURE);
    }
    pthread_detach(server_tid);
    printf("HI\n"); // Make it detached so it won't block main thread

    sleep(1); // Allow server thread to start

    // Gather list of local files
    char file_list[BUFFER_SIZE] = "";
    //DIR *d = opendir(FILES_DIRECTORY_PATH);
    if (!d) {
        perror("Could not open files directory. Please check the path");
        exit(EXIT_FAILURE);
    }
    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        if (dir->d_type == DT_REG) {
            strcat(file_list, dir->d_name);
            strcat(file_list, ",");
        }
    }
    closedir(d);
    if (strlen(file_list) > 0) file_list[strlen(file_list) - 1] = '\0';

    // Register with central server
    char register_msg[BUFFER_SIZE];
    snprintf(register_msg, sizeof(register_msg), "REGISTER %d %s", MY_LISTEN_PORT, file_list);
    char server_response[BUFFER_SIZE] = {0};
    send_to_server(register_msg, server_response);
    printf("Central Server Response: %s\n", server_response);
    fflush(stdout);

    // User input loop
    char user_input[100];
    while (1) {
        printf("\nEnter 'list' to see files, filename to download, or 'exit': ");
        fflush(stdout); // Important to flush prompt immediately

        if (fgets(user_input, sizeof(user_input), stdin) == NULL) break;
        user_input[strcspn(user_input, "\n\r")] = 0;

        if (strcmp(user_input, "exit") == 0) break;
        if (strlen(user_input) == 0) continue;

        if (strcmp(user_input, "list") == 0) {
            char list_response[BUFFER_SIZE] = {0};
            send_to_server("LIST", list_response);
            printf("\n--- Available Files ---\n%s\n-----------------------\n", list_response);
            fflush(stdout);
        } else {
            char query_msg[BUFFER_SIZE];
            snprintf(query_msg, sizeof(query_msg), "QUERY %s", user_input);
            send_to_server(query_msg, server_response);
            printf("Central Server Response: %s\n", server_response);
            fflush(stdout);

            if (strcmp(server_response, "NOT_FOUND") != 0) {
                char peer_ip[INET_ADDRSTRLEN];
                int peer_port;

                char response_copy[BUFFER_SIZE];
                strncpy(response_copy, server_response, BUFFER_SIZE);

                char *token = strtok(response_copy, ":");
                if (token) strcpy(peer_ip, token);
                token = strtok(NULL, ":");
                if (token) peer_port = atoi(token);
                else continue;

                printf("Connecting to peer at %s:%d...\n", peer_ip, peer_port);
                fflush(stdout);

                int peer_sock;
                struct sockaddr_in peer_addr;
                if ((peer_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) continue;

                peer_addr.sin_family = AF_INET;
                peer_addr.sin_port = htons(peer_port);
                inet_pton(AF_INET, peer_ip, &peer_addr.sin_addr);

                if (connect(peer_sock, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) < 0) {
                    perror("Connection to peer failed");
                    close(peer_sock);
                    continue;
                }

                send(peer_sock, user_input, strlen(user_input), 0);

                printf("Saving file to current directory...\n");
                fflush(stdout);

                FILE *fp = fopen(user_input, "wb");
                if (fp) {
                    char file_buffer[BUFFER_SIZE];
                    int bytes_received;
                    while ((bytes_received = recv(peer_sock, file_buffer, BUFFER_SIZE, 0)) > 0) {
                        fwrite(file_buffer, 1, bytes_received, fp);
                    }
                    fclose(fp);
                    printf("Download of %s completed!\n", user_input);
                } else {
                    perror("Failed to create file for download");
                }
                close(peer_sock);
            }
        }
    }

    char exit_msg[64];
    snprintf(exit_msg, sizeof(exit_msg), "EXIT %d", MY_LISTEN_PORT);
    send_to_server(exit_msg, server_response);
    printf("Server Response to EXIT: %s\n", server_response);
    fflush(stdout);

    return 0;
}