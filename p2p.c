#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/stat.h>
#include <time.h>
#include <stdbool.h>

#define PORT "43069"
#define MAX_PEERS 10
#define BUFFER_SIZE 1024

time_t current_modified_time = 0;
time_t saved_modified_time = 0;
bool update_in_progress = false;
pthread_mutex_t blockchain_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t update_cond = PTHREAD_COND_INITIALIZER;

int peer_sockets[MAX_PEERS];
int peer_count = 0;

time_t get_time() {//gets blockchain modified time.
    struct stat st;
    if (stat("blockchain.txt", &st) == 0) {
        return st.st_mtime;
    } else {
        perror("stat_g_t");  // Prints an error if the stat call fails
        return -1;  // Return an invalid time in case of error
    }
}

void add_peer(int socket) { // function to add peers or more connections.

    if (peer_count < MAX_PEERS) {
        peer_sockets[peer_count++] = socket; //save their sock_fd
    }
}

void remove_peer(int socket) {
    for (int i = 0; i < peer_count; i++) {
        if (peer_sockets[i] == socket) {//Replaces the socket_fd of the peer to be removed with the last element in the peer_sockets array
            peer_sockets[i] = peer_sockets[--peer_count];
            break;
        }
    }
}
void broadcast_blockchain(int sender_socket, bool single) {/*************************************************************************************************** */
    //broadcast should only broadcast to everyone except the sender. it should broadcast to everyone if the changes were made locally(not from sender).
    //if the bool single is true then we are sending the buffer to one specific socket. (for the listen peer function)

    FILE *file = fopen("blockchain.txt", "rb");
    if (file == NULL) {
        perror("Error opening blockchain.txt");
        return;
    }
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buffer = malloc(file_size + 1);
    if (buffer == NULL) {
        perror("Memory allocation failed");
        fclose(file);
        return;
    }
    fread(buffer, 1, file_size, file);
    buffer[file_size] = '\0';
    fclose(file);

    if(single){ //if its true then I send the file to a single person. this is specifically for listen peer function.
        send(sender_socket, buffer, file_size + 1, 0);
    } else {//otherwise broadcast to everyone except sender.
    for (int i = 0; i < peer_count; i++) {
        if (peer_sockets[i] != sender_socket) {//if its -1, it'll send it to everyone.
            send(peer_sockets[i], buffer, file_size + 1, 0);
        }
    }
    }
    free(buffer);
}

void update_blockchain(const char *data, int sock_fd) {
    pthread_mutex_lock(&blockchain_mutex);
    update_in_progress = true;

    FILE *file = fopen("blockchain.txt", "w");
    if (file != NULL) {
        time_t now = time(NULL);
        //fprintf(file, "%s", ctime(&now));
        fprintf(file, "%s\n", data);
        fclose(file);
    }

    current_modified_time = get_time();
    saved_modified_time = current_modified_time;
    update_in_progress = false;

    pthread_cond_signal(&update_cond);
    pthread_mutex_unlock(&blockchain_mutex);

    if(sock_fd != -1){
        broadcast_blockchain(sock_fd, false);
    }
}

void *monitor_blockchain(void *arg) {/************************************************************************************************************************************ */
    //monitor blockchain should monitor the local blockchain at all times(every 5 seconds) for changes. if changes then broadcast else do nothing(or am i supposed to update time?idk)
    struct timespec ts; //use this in conjuntion with pthread cond timed wait for mutual exclusion purposes.
    while (1) {
        pthread_mutex_lock(&blockchain_mutex);
        while (update_in_progress) {
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1; // Wait for up to 1 second
            pthread_cond_timedwait(&update_cond, &blockchain_mutex, &ts);
        }

        time_t new_time = get_time();
        if (new_time > saved_modified_time) {
            printf("Blockchain file changed.\n");
            broadcast_blockchain(-1, false);
            saved_modified_time = new_time;
        }
        pthread_mutex_unlock(&blockchain_mutex);
        sleep(5); // Sleep for 5 seconds before next check
    }
    return NULL;
}

void *handle_peer(void *socket_ptr) {//*****************************************************************************************************************************
    //handle peer needs to constantly receive data from peer and if recieved add it to the blockchain. also announces disconnects.
    int sock = *(int*)socket_ptr;
    char buffer[BUFFER_SIZE] = {0};
    int valread;

    while (1) {
        valread = recv(sock, buffer, BUFFER_SIZE - 1, 0);//stays on this line till something is received.
        if (valread <= 0) {
            printf("Peer disconnected\n");
            break;
        }
        buffer[valread] = '\0';// Null-terminate the received data for safety
        update_blockchain(buffer, sock);// true indicates it's from the network
        printf("Received: %s\n", buffer);

        memset(buffer, 0, BUFFER_SIZE);
    }

    remove_peer(sock);
    close(sock);
    free(socket_ptr);
    return NULL;
}//************************************************************************************************************ */

void *listen_for_peers(void *arg) {
    int sock_fd;
    struct addrinfo hints, *servinfo, *p;
    int yes = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT, &hints, &servinfo) != 0) {
        perror("getaddrinfo");
        return NULL;
    }

    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            continue;
        }
        if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            close(sock_fd);
            continue;
        }
        if (bind(sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sock_fd);
            continue;
        }
        break;
    }

    freeaddrinfo(servinfo);

    if (p == NULL) {
        fprintf(stderr, "Failed to bind\n");
        return NULL;
    }
    if (listen(sock_fd, 10) == -1) {
        perror("listen");
        return NULL;
    }
    printf("Listening for peers...\n");

    while(1) {
        struct sockaddr_storage their_addr;
        socklen_t addr_size = sizeof their_addr;
        int *new_sock = malloc(sizeof(int));
        
        *new_sock = accept(sock_fd, (struct sockaddr *)&their_addr, &addr_size);
        
        if (*new_sock == -1) {
            perror("accept");
            free(new_sock);
            continue;
        }
        char peer_ip[INET6_ADDRSTRLEN];
        inet_ntop(their_addr.ss_family, 
            &(((struct sockaddr_in*)&their_addr)->sin_addr),
            peer_ip, sizeof peer_ip);
        printf("New peer connection from %s\n", peer_ip);

        add_peer(*new_sock);

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_peer, (void*)new_sock) < 0) {//assigns new handle peer thread to all new connections
            perror("Could not create thread");
            remove_peer(*new_sock);
            close(*new_sock);
            free(new_sock);
            continue;
        }
        broadcast_blockchain(*new_sock, true);//broadcast blockchain as soon as a new client connects(only sends to whoever just connected)
        saved_modified_time = current_modified_time;
    }
}

void connect_to_peer(const char *ip_address) { //****************************************************************************************starts thread to handle peer
    int sockfd;  
    struct addrinfo hints, *servinfo, *p;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(ip_address, PORT, &hints, &servinfo) != 0) {
        perror("getaddrinfo");
        return;
    }
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            continue;
        }
        break;
    }
    if (p == NULL) {
        fprintf(stderr, "Failed to connect\n");
        return;
    }
    freeaddrinfo(servinfo);
    printf("Connected to peer %s\n", ip_address);
    add_peer(sockfd);

    int *new_sock = malloc(sizeof(int));
    *new_sock = sockfd;
    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, handle_peer, (void*)new_sock) < 0) { 
        perror("Could not create thread");
        remove_peer(sockfd);
        close(sockfd);
        free(new_sock);
        return;
    }
}

int main(int argc, char *argv[]) {/***************************************************************************************** */
    pthread_t listen_thread, monitor_thread;
    
    pthread_mutex_lock(&blockchain_mutex);
    saved_modified_time = get_time();
    pthread_mutex_unlock(&blockchain_mutex);
    
    if (pthread_create(&listen_thread, NULL, listen_for_peers, NULL) < 0) {//need listenForPeers active for every instance of peer
        perror("Could not create listening thread");
        return 1;
    }

    if (pthread_create(&monitor_thread, NULL, monitor_blockchain, NULL) < 0) {//Also need monitorBlkChain active for ecery instance of peer.
        perror("Could not create monitor_Blockchain thread");
        return 1;
    }

    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            connect_to_peer(argv[i]);
        }
    } else {
        // Use local copy of blockchain.txt
        FILE *file = fopen("blockchain.txt", "r");
        if (file == NULL) {
            file = fopen("blockchain.txt", "w");// Create an empty blockchain file if it doesn't exist
            if (file == NULL) {
                perror("Error creating blockchain.txt");
                return 1;
            }
        }
        fclose(file);
    }
    // need to start a pthread to monitor the blockchain. if changes, broadcast
    //listen_for_peers(void);// then we also start lsitening for incoming
    while(1) {//RUN FOREVER
        ;
    }//could add user input part in the while loop to update the blockchain through cmd line which would be better in terms of code safety and ease of use.
    return 0;
}