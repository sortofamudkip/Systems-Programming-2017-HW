#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/file.h>

#define ERR_EXIT(a) { perror(a); exit(1); }

typedef struct {
    char hostname[512];  // server's hostname
    unsigned short port;  // port to listen
    int listen_fd;  // fd to wait for a new connection
} server;

typedef struct {
    char host[512];  // client's host
    int conn_fd;  // fd to talk with client
    char buf[512];  // data sent by/to client
    size_t buf_len;  // bytes used by buf
    // you don't need to change this.
    char* filename;  // filename set in header, end with '\0'.
    int header_done;  // used by handle_read to know if the header is read or not.
    int associative_fd; //the file it's opened to
} request;

server svr;  // server
request* requestP = NULL;  // point to a list of requests
int maxfd;  // size of open file descriptor table, size of request list

const char* accept_header = "ACCEPT\n";
const char* reject_header = "REJECT\n";

// Forwards

static void init_server(unsigned short port);
// initailize a server, exit for error

static void init_request(request* reqP);
// initailize a request instance

static void free_request(request* reqP);
// free resources used by a request instance

static int handle_read(request* reqP);
// return 0: socket ended, request done.
// return 1: success, message (without header) got this time is in reqP->buf with reqP->buf_len bytes. read more until got <= 0.
// It's guaranteed that the header would be correctly set after the first read.
// error code:
// -1: client connection error

int main(int argc, char** argv) {
    int i, ret;

    struct sockaddr_in cliaddr;  // used by accept()
    int clilen;

    int conn_fd;  // fd for a new connection with client
    int file_fd;  // fd for file that we open for reading
    char buf[512];
    int buf_len;

    // Parse args.
    if (argc != 2) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }

    // Initialize server
    init_server((unsigned short) atoi(argv[1]));

    // Get file descripter table size and initize request table
    maxfd = getdtablesize();
    requestP = (request*) malloc(sizeof(request) * maxfd);
    if (requestP == NULL) {
        ERR_EXIT("out of memory allocating all requests");
    }
    for (i = 0; i < maxfd; i++) {
        init_request(&requestP[i]);
    }
    requestP[svr.listen_fd].conn_fd = svr.listen_fd;
    strcpy(requestP[svr.listen_fd].host, svr.hostname);

    // Loop for handling connections
    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);

    //> add in master set for multiplexing
    fd_set master_set;
    FD_ZERO(&master_set);
    FD_SET(svr.listen_fd, &master_set);
    maxfd = svr.listen_fd;

    //> add in timeout for some reason
    struct timeval timeout;
    timeout.tv_sec = 0; //arbitrary
    timeout.tv_usec = 10;

    //> add in thingy to check if we've opened it before
    fd_set has_been_opened;
    FD_ZERO(&has_been_opened);

    while (1) {
        fd_set read_set;

        read_set = master_set;

        int fd_ready = select(maxfd + 1, &read_set, NULL, NULL, &timeout);


// fprintf(stderr, "files that are ready for reading:\n");
// for (int i = 0; i <= maxfd; i++) if (FD_ISSET(i, &read_set)) printf("%d ", i);
// puts("");

        if(FD_ISSET(svr.listen_fd, &read_set)) {
            fd_ready--;
            // Check new connection
            clilen = sizeof(cliaddr);
            conn_fd = accept(svr.listen_fd, (struct sockaddr*)&cliaddr, (socklen_t*)&clilen);
            if (conn_fd < 0) {
                if (errno == EINTR || errno == EAGAIN) continue;  // try again
                if (errno == ENFILE) {
                    (void) fprintf(stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd);
                    continue;
                }
                ERR_EXIT("accept");
            }
            requestP[conn_fd].conn_fd = conn_fd;
            strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
            fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host);
            maxfd = (conn_fd > maxfd) ? conn_fd : maxfd;
            //> also turn on conn_fd's bit in the MASTER fd_set
            FD_SET(conn_fd, &master_set);            
        }
        

        for (int conn_fd = svr.listen_fd+1; conn_fd <= maxfd && fd_ready > 0; ++conn_fd) { //for each request
            if (FD_ISSET(conn_fd, &read_set)) {
                fd_ready--;
                ret = handle_read(&requestP[conn_fd]); //do handle read
                if (ret < 0) {
                    fprintf(stderr, "bad request from %s\n", requestP[conn_fd].host);
                    continue;
                }
                if (!FD_ISSET(conn_fd, &has_been_opened)) { //if it hasn't been opened before

                    int temp_fd = open(requestP[conn_fd].filename, O_RDWR);
                    printf("opening the file %s\n", requestP[conn_fd].filename);

                    printf("\ttemp_fd = %d\n", temp_fd);

                    struct flock lock;
                    lock.l_type = F_WRLCK;
                    lock.l_whence = SEEK_SET;
                    lock.l_start = 0;
                    lock.l_len = 0; //lock to EOF
                    if (temp_fd != -1) {
                        fprintf(stderr, "Opening file [%s] (already exists)\n", requestP[conn_fd].filename);
                        // TODO: Add lock
                        // TODO: check if the request should be rejected.
                        printf("\tchecking for locks:\n");
                        fcntl(temp_fd, F_GETLK, &lock);
                        int repeatedFname = 0;
#ifndef READ_SERVER 
                        for (int i = svr.listen_fd; i <= maxfd; ++i) {
                            if (i != conn_fd && requestP[i].filename != NULL && !strcmp(requestP[i].filename, requestP[conn_fd].filename)) {
                                repeatedFname = 1; break;
                            }
                        }
#endif
                        if (lock.l_type != F_UNLCK || repeatedFname) { //can't lock
                        // if (lock.l_type != F_UNLCK) { //if any lock exists
                            //> print REJECT
                            printf("\tlock exists, rejecting request\n");
                            write(requestP[conn_fd].conn_fd, reject_header, sizeof(reject_header));
                            //> get rid of this process
                            close(temp_fd);               
                            FD_CLR(conn_fd, &master_set);
                            FD_CLR(conn_fd, &has_been_opened);
                            close(requestP[conn_fd].conn_fd);
                            free_request(&requestP[conn_fd]); 
                            continue;                        
                        }
                        else printf("\tno locks, accepting request\n");
                    }
                    //no lock/file DNE: accept
                    
                    FD_SET(conn_fd, &has_been_opened);
                    close(temp_fd);
                    write(requestP[conn_fd].conn_fd, accept_header, sizeof(accept_header));
#ifdef READ_SERVER
                    file_fd = open(requestP[conn_fd].filename, O_RDONLY);
                    lock.l_type = F_RDLCK;
#endif
#ifndef READ_SERVER 
                    printf("\treopen file, now with a write lock\n");
                    file_fd = open(requestP[conn_fd].filename, O_WRONLY | O_CREAT | O_TRUNC,
                            S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
                    requestP[conn_fd].associative_fd = file_fd;
                    lock.l_type = F_WRLCK;
#endif
                    printf("\tsuccess, file_fd is now %d\n", file_fd);

                    //> lock this file
                    lock.l_whence = SEEK_SET;
                    lock.l_start = 0;
                    lock.l_len = 0; //lock to EOF
                    fcntl(file_fd, F_SETLK, &lock); //put down a lock
                    // fcntl(file_fd, F_GETLK, &lock);
                    // if (lock.l_type == F_UNLCK) puts("can ignore???");
                    // if (lock.l_type == F_RDLCK) printf("read lock was implemented?\n");
                    // if (lock.l_type == F_WRLCK) printf("write lock was implemented\n");
                    // if (lock.l_type == F_UNLCK) printf("unlock was implemented??\n");

                } //end of "first open"

#ifdef READ_SERVER
                while (1) {
                    ret = read(file_fd, buf, sizeof(buf));
                    if (ret < 0) {
                        fprintf(stderr, "Error when reading file %s\n", requestP[conn_fd].filename);
                        break;
                    } else if (ret == 0) break;
                    write(requestP[conn_fd].conn_fd, buf, ret);
                }
                if (ret == 0) { //EOF
                    if (file_fd >= 0) close(file_fd);
                    FD_CLR(conn_fd, &master_set);
                    close(requestP[conn_fd].conn_fd);
                    free_request(&requestP[conn_fd]);
                    FD_CLR(conn_fd, &has_been_opened);
                    fprintf(stderr, "Done reading file [%s]\n", requestP[conn_fd].filename);
                }            
#endif


#ifndef READ_SERVER
                if (ret == 0) {
                    puts("CLOSED FILE");
                    close(requestP[conn_fd].associative_fd);
                    FD_CLR(conn_fd, &master_set);
                    FD_CLR(conn_fd, &has_been_opened);
                    close(requestP[conn_fd].conn_fd);
                    free_request(&requestP[conn_fd]);                    
                    fprintf(stderr, "Done writing file [%s]\n", requestP[conn_fd].filename);
                }
                else {
                    write(requestP[conn_fd].associative_fd, requestP[conn_fd].buf, requestP[conn_fd].buf_len);
                    // printf("%s", requestP[conn_fd].buf);
                }
#endif
                
            } //end of "is read/write set"
        } //end of "for each request" loop
    } //end of while (1)

    free(requestP);
    return 0;
}


// ======================================================================================================
// You don't need to know how the following codes are working
#include <fcntl.h>

static void* e_malloc(size_t size);


static void init_request(request* reqP) {
    reqP->conn_fd = reqP->associative_fd = -1;
    reqP->buf_len = 0;
    reqP->filename = NULL;
    reqP->header_done = 0;
}

static void free_request(request* reqP) {
    if (reqP->filename != NULL) {
        free(reqP->filename);
        reqP->filename = NULL;
    }
    init_request(reqP);
}

// return 0: socket ended, request done.
// return 1: success, message (without header) got this time is in reqP->buf with reqP->buf_len bytes. read more until got <= 0.
// It's guaranteed that the header would be correctly set after the first read.
// error code:
// -1: client connection error
static int handle_read(request* reqP) {
    int r;
    char buf[512];

    // Read in request from client
    r = read(reqP->conn_fd, buf, sizeof(buf));
    if (r < 0) return -1;
    if (r == 0) return 0;
    if (reqP->header_done == 0) {
        char* p1 = strstr(buf, "\015\012");
        int newline_len = 2;
        // be careful that in Windows, line ends with \015\012
        if (p1 == NULL) {
            p1 = strstr(buf, "\012");
            newline_len = 1;
            if (p1 == NULL) {
                // This would not happen in testing, but you can fix this if you want.
                ERR_EXIT("header not complete in first read...");
            }
        }
        size_t len = p1 - buf + 1;
        reqP->filename = (char*)e_malloc(len);
        memmove(reqP->filename, buf, len);
        reqP->filename[len - 1] = '\0';
        p1 += newline_len;
        reqP->buf_len = r - (p1 - buf);
        memmove(reqP->buf, p1, reqP->buf_len);
        reqP->header_done = 1;
    } else {
        reqP->buf_len = r;
        memmove(reqP->buf, buf, r);
    }
    return 1;
}

static void init_server(unsigned short port) {
    struct sockaddr_in servaddr;
    int tmp;

    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;

    svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr.listen_fd < 0) ERR_EXIT("socket");
    fcntl(svr.listen_fd, F_SETFL, O_NONBLOCK);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    tmp = 1;
    if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&tmp, sizeof(tmp)) < 0) {
        ERR_EXIT("setsockopt");
    }
    if (bind(svr.listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        ERR_EXIT("bind");
    }
    if (listen(svr.listen_fd, 1024) < 0) {
        ERR_EXIT("listen");
    }
}

static void* e_malloc(size_t size) {
    void* ptr;

    ptr = malloc(size);
    if (ptr == NULL) ERR_EXIT("out of memory");
    return ptr;
}

