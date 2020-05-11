#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/select.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define TIMEOUT_SEC 5		// timeout in seconds for wait for a connection 
#define MAXBUFSIZE  1024	// timeout in seconds for wait for a connection 
#define NO_USE      0		// status of a http request
#define ERROR	    -1	
#define READING     1		
#define WRITING     2		
#define ERR_EXIT(a) { perror(a); exit(1); }

#define CHILD_EXIT_ERROR 255
#define LOGFILE_REQUEST -555
#define FNAME_NOT_SPECIFIED -789

typedef struct {
    char hostname[512];		// hostname
    unsigned short port;	// port to listen
    int listen_fd;		// fd to wait for a new connection
} http_server;

typedef struct {
    int conn_fd;		// fd to talk with client
    int status;			// not used, error, reading (from client)
                                // writing (to client)
    char file[MAXBUFSIZE];	// requested file
    char query[MAXBUFSIZE];	// requested query
    char host[MAXBUFSIZE];	// client host
    char* buf;			// data sent by/to client
    size_t buf_len;		// bytes used by buf
    size_t buf_size; 		// bytes allocated for buf
    size_t buf_idx; 		// offset for reading and writing
} http_request;

typedef struct {
    char c_time_string[1000];
} TimeInfo;

static char* logfilenameP;	// log file name

// Forwards
//

static void add_to_buf( http_request *reqP, char* str, size_t len );

static void init_http_server( http_server *svrP,  unsigned short port );
// initailize a http_request instance, exit for error

static void init_request( http_request* reqP );
// initailize a http_request instance

static void free_request( http_request* reqP );
// free resources used by a http_request instance

static int read_header_and_file( http_request* reqP, int *errP );
// return 0: success, file is buffered in retP->buf with retP->buf_len bytes
// return -1: error, check error code (*errP)
// return 1: continue to it until return -1 or 0
// error code: 
// 1: client connection error 
// 2: bad request, cannot parse request
// 3: method not implemented 
// 4: illegal filename
// 5: illegal query
// 6: file not found
// 7: file is protected

static void set_ndelay( int fd );
// Set NDELAY mode on a socket.

#define INVALID_NAME 0
#define BAD_CGI_NAME -2

int check_file_validity(char *CGI_prog_name, char *filename) {
    fprintf(stderr, "CGI name: |%s|; filename: |%s|\n", CGI_prog_name, filename);
    //contains things that aren't {0-9} U {upper/lower alphabet} U  {_}
    for (int i = 0; CGI_prog_name[i]; ++i) {
        if (!(isalnum(CGI_prog_name[i]) || CGI_prog_name[i] == '_')) {fprintf(stderr, "Bad CGI name: |%s|\n", CGI_prog_name); return INVALID_NAME;}
    }
    for (int i = 0; filename[i]; ++i) {
        if (!(isalnum(filename[i]) || filename[i] == '_')) {fprintf(stderr, "Bad filename: |%s|\n", filename); return INVALID_NAME;}
    }
    return 1;
}

#define MAX_FILE_SIZE 800000 
char global_buf[MAX_FILE_SIZE];
int pid[1024];
int openpids[1024] = {};
int dead_processes = 0;

int get_open_files() {
    int count = 0;
    for (int i = 0; i < 1024; i++) if (pid[i] != -1) openpids[count++] = pid[i];
    return count;
}

TimeInfo* map_pos;
http_request* global_pointer;

void handler(int signal) {
    fprintf(stderr, "signal recieved!\n");
    if (!global_pointer) ERR_EXIT("bad request pointer in handler");
    char header_str[200], info_message[1000];
    int buflen = snprintf(header_str, sizeof(header_str), "HTTP/1.1 200 OK\015\012\015\012" );
    write(global_pointer->conn_fd, header_str, buflen);
    sprintf(info_message, "%d processes died previously.\n", ++dead_processes);
    write(global_pointer->conn_fd, info_message, strlen(info_message)); 
    int open_count = get_open_files();
    write(global_pointer->conn_fd, "PIDs of runing processes: ", strlen("PIDs of runing processes: ")); 
    for (int i = 0; i < open_count; i++) {
        sprintf(info_message, "%d ", openpids[i]);
        write(global_pointer->conn_fd, info_message, strlen(info_message)); 
    } write(global_pointer->conn_fd, "\n", strlen("\n"));
    write(global_pointer->conn_fd, "Previous CGI Exit: ", strlen("Previous CGI Exit: "));
    write(global_pointer->conn_fd, map_pos->c_time_string, strlen(map_pos->c_time_string));
    fsync(global_pointer->conn_fd);
}

int main(int argc, char** argv) {
//> DO NOT TOUCH THIS
    http_server server;		// http server
    http_request* requestP = NULL;// pointer to http requests from client
    int maxfd;                  // size of open file descriptor table
    struct sockaddr_in cliaddr; // used by accept()
    int clilen;
    int conn_fd;		// fd for a new connection with client
    int err;			// used by read_header_and_file()
    int i, ret, nwritten;
    // Parse args. 
    if ( argc != 3 ) {
        fprintf( stderr, "usage:  %s port# logfile\n", argv[0] );
        exit(1);
    }
    logfilenameP = argv[2];
    // Initialize http server
    init_http_server( &server, (unsigned short) atoi( argv[1] ) );
    maxfd = getdtablesize();
    requestP = ( http_request* ) malloc( sizeof( http_request ) * maxfd );
    if ( requestP == (http_request*) 0 ) {
		fprintf(stderr, "out of memory allocating all http requests\n");
		exit(1);
    }
    for ( i = 0; i < maxfd; i++) init_request(&requestP[i]);
    requestP[ server.listen_fd ].conn_fd = server.listen_fd;
    requestP[ server.listen_fd ].status = READING;
    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d, logfile %s...\n", server.hostname, server.port, server.listen_fd, maxfd, logfilenameP );
//> END OF DO NOT TOUCH THIS
    //> Signal stuff
    struct sigaction act;
    act.sa_handler = handler; act.sa_flags = 0 | SA_INTERRUPT;
    if (sigaction(SIGUSR1, &act, NULL) < 0) ERR_EXIT("sigaction error");
    //> End of signal stuff
    //> pipes and processes 
    for (int i =0; i < 1024; i++) pid[i] = -1;
    int pipe1[1024][2], pipe2[1024][2];
    //> end of pipes and processes

    //> Multuplexing stuff
    // add in master set for multiplexing
    fd_set master_bation_fd; FD_ZERO(&master_bation_fd); FD_SET(server.listen_fd, &master_bation_fd);
    fd_set master_set; FD_ZERO(&master_set); 
    maxfd = server.listen_fd + 5; 
    struct timeval timeout; //timeout, for select
    //> end of multiplexing stuff

    //> mmap stuff! god help us.
    
    int mmap_fd = open(logfilenameP, O_RDWR | O_TRUNC | O_CREAT, 0777); 
    lseek(mmap_fd, 500, SEEK_CUR); write(mmap_fd, "", 1);
    map_pos = (TimeInfo*) mmap(NULL, sizeof(TimeInfo), PROT_READ|PROT_WRITE, MAP_SHARED, mmap_fd, 0);
    close(mmap_fd);
    //> end of mmap stuff. Ew.

    while (1) { //this will: check if new connection exists AND process existing running
        timeout.tv_sec = 1;
        timeout.tv_usec = 200000;
        fd_set read_bation_fd = master_bation_fd;
        int accept_new_connection = select(server.listen_fd+1, &read_bation_fd, NULL, NULL, &timeout);
        if (FD_ISSET(server.listen_fd, &read_bation_fd)) { 
            // Get a connection
            clilen = sizeof(cliaddr);
            conn_fd = accept( server.listen_fd, (struct sockaddr *) &cliaddr, (socklen_t *) &clilen );
            if (conn_fd < 0) {
                if ( errno == EINTR || errno == EAGAIN ) continue; // try again 
                if ( errno == ENFILE ) {
                    (void) fprintf( stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd );
                    continue;
                }   
                ERR_EXIT("accept");
            }
            requestP[conn_fd].conn_fd = conn_fd;
            requestP[conn_fd].status = READING;     
            strcpy( requestP[conn_fd].host, inet_ntoa( cliaddr.sin_addr ) );
            set_ndelay( conn_fd );
            fprintf( stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host );
            maxfd = (maxfd > conn_fd) ? maxfd : conn_fd;
            ret = 88888;
            int valid_file_name = 1, CGI_exists = 1;
            while (ret != 0) {
                ret = read_header_and_file(&requestP[conn_fd], &err);
                if (ret > 0) continue;
                if (ret == LOGFILE_REQUEST) { //fork a child and send a signal to... the server. Wat.
                    int pid = getpid(), child;
                    global_pointer = &requestP[conn_fd];
                    if ((child = fork()) == 0) {
                        kill(pid, SIGUSR1);
                        exit(0);
                    }
                    else {
                        waitpid(child, NULL, 0);
                        close(requestP[conn_fd].conn_fd);
                        free_request( &requestP[conn_fd] );
                        break;
                    }
                }
                // fprintf(stderr, "CGI program name: %s, requested filename: %s\n", requestP[conn_fd].file, strchr(requestP[conn_fd].query, '=') + 1);
                // check the validity of the file inside this function
                // note that the CGI prog name is stored in requestP[conn_fd].file
                //     and that the file name is in strchr(requestP[conn_fd].query, '=') + 1
                if (ret != FNAME_NOT_SPECIFIED) {
	                valid_file_name = check_file_validity(requestP[conn_fd].file, strchr(requestP[conn_fd].query, '=') + 1);
	                CGI_exists = access(requestP[conn_fd].file, F_OK) != -1 ? 1 : 0;
	                if (CGI_exists) fprintf(stderr, "CGI Exists: |%s|\n", requestP[conn_fd].file);
	                else fprintf(stderr, "CGI doesn't exist: |%s|\n", requestP[conn_fd].file);
                }
                // error codes and actions performed depending on the following:
                if (ret < 0 || !valid_file_name || !CGI_exists) { // error for reading http header or requested file
                    char err_msg[500];
                    if (!valid_file_name) {
                        fprintf(stderr, "400 Bad Request: Invalid file name");
                        int buflen = snprintf(err_msg, sizeof(err_msg), "HTTP/1.1 400 Bad Request\015\012\015\012");
                        write(requestP[conn_fd].conn_fd, err_msg, buflen);
                        write(requestP[conn_fd].conn_fd, "400 Bad Request", strlen("400 Bad Request"));
                    }
                    else if (!CGI_exists) {
                        fprintf(stderr, "404 Not Found: CGI doesn't exist\n");
                        int buflen = snprintf(err_msg, sizeof(err_msg), "HTTP/1.1 404 Not Found\015\012\015\012");
                        write(requestP[conn_fd].conn_fd, err_msg, buflen);
                        write(requestP[conn_fd].conn_fd, "404 Not Found", strlen("404 Not Found"));
                    }
                    else if (ret == FNAME_NOT_SPECIFIED) {
                    	fprintf(stderr, "404 Not Found: filename not specified\n");
                        int buflen = snprintf(err_msg, sizeof(err_msg), "HTTP/1.1 404 Not Found\015\012\015\012");
                        write(requestP[conn_fd].conn_fd, err_msg, buflen);
                        write(requestP[conn_fd].conn_fd, "404 Not Found: filename not specified", strlen("404 Not Found: filename not specified"));
                    }
                    fprintf(stderr, "error on fd %d, code %d\n", requestP[conn_fd].conn_fd, err );
                    fsync(requestP[conn_fd].conn_fd);
                    requestP[conn_fd].status = ERROR;
                    close(requestP[conn_fd].conn_fd);
                    free_request( &requestP[conn_fd] );
                    break;
                }
            }
            if (ret == 0 && valid_file_name && CGI_exists) { 
                char *query_name = strchr(requestP[conn_fd].query, '=') + 1;
                pipe(pipe1[conn_fd]); pipe(pipe2[conn_fd]);
                if ((pid[conn_fd] = fork()) == 0) {
                    //redirection
                    close(pipe1[conn_fd][1]);                   close(pipe2[conn_fd][0]);
                    dup2(pipe1[conn_fd][0], STDIN_FILENO);      dup2(pipe2[conn_fd][1], STDOUT_FILENO);
                    close(pipe1[conn_fd][0]);                   close(pipe2[conn_fd][1]);
                    //exec
                    execl(requestP[conn_fd].file, requestP[conn_fd].file, logfilenameP, (char *)0);
                }
                else { 
                    usleep(560); //so the child does its thing first?
                    close(pipe1[conn_fd][0]);                      close(pipe2[conn_fd][1]);
                    //write the file name to the child
                    write(pipe1[conn_fd][1], query_name, strlen(query_name));
                    fsync(pipe1[conn_fd][1]);
                    FD_SET(pipe2[conn_fd][0], &master_set); //save this into read
                    maxfd = (maxfd > pipe2[conn_fd][0]) ? maxfd : pipe2[conn_fd][0];
                }
            }
        }
	timeout.tv_sec = 0;
        timeout.tv_usec = 10;
        fd_set read_set = master_set;
        int fd_ready = select(maxfd + 1, &read_set, NULL, NULL, &timeout);
        for (int conn_fd = 2; conn_fd <= maxfd && fd_ready > 0; ++conn_fd) { //for each connection: check if its pipe exists
            if (FD_ISSET(pipe2[conn_fd][0], &read_set)) { //this one is ready to handle requests
                char *query_name = strchr(requestP[conn_fd].query, '=') + 1;
                fd_ready--;
                char buffer[1024] = {}; int n, status; nwritten = 0;
                while ((n = read(pipe2[conn_fd][0], buffer, 1024)) > 0) {
                    if (nwritten >= MAX_FILE_SIZE) {
                        write(requestP[conn_fd].conn_fd, "Max file size exceeded...", strlen("Max file size exceeded..."));
                        kill(pid[conn_fd], SIGINT);
                        break;
                    }
                    // write(STDERR_FILENO, buffer, n);
                    strncpy(&global_buf[nwritten], buffer, n);
                    nwritten += n;
                }
                waitpid(pid[conn_fd], &status, 0);
                //> start of record dead time & file name
                time_t current_time = time(NULL); char c_time_string[100], final_string[100];
                strcpy(c_time_string, ctime(&current_time));
                sprintf(final_string, "\n\t%s\tFilename: %s\n", c_time_string, query_name);
                memcpy(map_pos->c_time_string, &final_string , sizeof(final_string));
                //> end of record dead time etc

                dead_processes++;
                fprintf(stderr, "exit status: %d\n", WEXITSTATUS(status));
                char header_str[1000]; struct stat sb; char timebuf[100]; int buflen;
                if (WEXITSTATUS(status) == CHILD_EXIT_ERROR) { //if return value was 255, it died abnormally
                    fprintf(stderr, "404 Not Found: file |%s| doesn't exist\n", strchr(requestP[conn_fd].query, '=') + 1);
                    //write header to client
                    buflen = snprintf(header_str, sizeof(header_str), "HTTP/1.1 404 Not Found\015\012" );
                    write(requestP[conn_fd].conn_fd, header_str, buflen);
                    fsync(requestP[conn_fd].conn_fd);
                }
                else {
                    buflen = snprintf(header_str, sizeof(header_str), "HTTP/1.1 200 OK\015\012" );
                    write(requestP[conn_fd].conn_fd, header_str, buflen);
                    fsync(requestP[conn_fd].conn_fd);
                    lstat(query_name, &sb);
                    buflen = snprintf(header_str, sizeof(header_str), "Content-Length: %ld\015\012", (int64_t) sb.st_size );
                }
                time_t now = time( (time_t*) 0 );
                strftime( timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime( &now ) );
                buflen = snprintf( header_str, sizeof(header_str), "Date: %s\015\012", timebuf );
                write(requestP[conn_fd].conn_fd, header_str, buflen);
                fsync(requestP[conn_fd].conn_fd);
                buflen = snprintf( header_str, sizeof(header_str), "Connection: close\015\012" );
                write(requestP[conn_fd].conn_fd, header_str, buflen);
                fsync(requestP[conn_fd].conn_fd);
                //write to client
                fsync(requestP[conn_fd].conn_fd);
                write(requestP[conn_fd].conn_fd, "\015\012", 2);
                fsync(requestP[conn_fd].conn_fd);
                write(requestP[conn_fd].conn_fd, global_buf, nwritten);
                fsync(requestP[conn_fd].conn_fd);
                fsync(requestP[conn_fd].conn_fd);
                fprintf(stderr, "complete writing %d bytes on fd %d\n", nwritten, requestP[conn_fd].conn_fd);

                //done here, clean up
                FD_CLR(pipe2[conn_fd][0], &master_set);
                close(pipe2[conn_fd][0]); close(pipe1[conn_fd][1]); 
                close(requestP[conn_fd].conn_fd); //do we need this
                free_request(&requestP[conn_fd]);
                pid[conn_fd] = -1;
            }
        }
    }
    free(requestP);
    return 0;
}


// ======================================================================================================
// You don't need to know how the following codes are working

#include <fcntl.h>
#include <ctype.h>

static void strdecode( char* to, char* from );
static int hexit( char c );
static char* get_request_line( http_request *reqP );
static void* e_malloc( size_t size );
static void* e_realloc( void* optr, size_t size );

static void init_request( http_request* reqP ) {
    reqP->conn_fd = -1;
    reqP->status = 0;		// not used
    reqP->file[0] = (char) 0;
    reqP->query[0] = (char) 0;
    reqP->host[0] = (char) 0;
    reqP->buf = NULL;
    reqP->buf_size = 0;
    reqP->buf_len = 0;
    reqP->buf_idx = 0;
}

static void free_request( http_request* reqP ) {
    if ( reqP->buf != NULL ) {
    	free( reqP->buf );
    	reqP->buf = NULL;
    }
    init_request( reqP );
}


#define ERR_RET( error ) { *errP = error; return -1; }

// return 0: success, file is buffered in retP->buf with retP->buf_len bytes
// return -1: error, check error code (*errP)
// return 1: read more, continue until return -1 or 0
// error code: 
// 1: client connection error 
// 2: bad request, cannot parse request
// 3: method not implemented 
// 4: illegal filename
// 5: illegal query
// 6: file not found
// 7: file is protected
//
static int read_header_and_file( http_request* reqP, int *errP ) {
    // Request variables
    char* file = (char *) 0;
    char* path = (char *) 0;
    char* query = (char *) 0;
    char* protocol = (char *) 0;
    char* method_str = (char *) 0;
    int r, fd;
    struct stat sb;
    char timebuf[100];
    int buflen;
    char buf[10000];
    time_t now;
    void *ptr;

    // Read in request from client
    while (1) {
		r = read( reqP->conn_fd, buf, sizeof(buf) );
		if ( r < 0 && ( errno == EINTR || errno == EAGAIN ) ) {return 1;}
		if ( r <= 0 ) ERR_RET( 1 )
		add_to_buf( reqP, buf, r );
		if ( strstr( reqP->buf, "\015\012\015\012" ) != (char*) 0 ||
		     strstr( reqP->buf, "\012\012" ) != (char*) 0 ) break;
    }
    // Parse the first line of the request.
    method_str = get_request_line( reqP );
    if ( method_str == (char*) 0 ) ERR_RET( 2 )
    path = strpbrk( method_str, " \t\012\015" ); //checks if there are any \t \n \r in string
    if ( path == (char*) 0 ) ERR_RET( 2 )
    *path++ = '\0';
    path += strspn( path, " \t\012\015" );
    protocol = strpbrk( path, " \t\012\015" );
    if ( protocol == (char*) 0 ) ERR_RET( 2 )
    *protocol++ = '\0';
    protocol += strspn( protocol, " \t\012\015" );
    query = strchr( path, '?' );
    if ( query == (char*) 0 ) {

        if (!strcmp(path+1, "info")) return LOGFILE_REQUEST;
        else {return FNAME_NOT_SPECIFIED;}
        query = "";
    }
    else *query++ = '\0';

    if ( strcasecmp( method_str, "GET" ) != 0 ) ERR_RET( 3 )
    else {
        strdecode( path, path );
        if ( path[0] != '/' ) ERR_RET( 4 )
	    else file = &(path[1]);
    }

    if ( strlen( file ) >= MAXBUFSIZE-1 ) ERR_RET( 4 )
    if ( strlen( query ) >= MAXBUFSIZE-1 ) ERR_RET( 5 )
	  
    strcpy( reqP->file, file );
    strcpy( reqP->query, query );
    return 0;
}


static void add_to_buf( http_request *reqP, char* str, size_t len ) { 
    char** bufP = &(reqP->buf);
    size_t* bufsizeP = &(reqP->buf_size);
    size_t* buflenP = &(reqP->buf_len);

    if ( *bufsizeP == 0 ) {
    *bufsizeP = len + 500;
    *buflenP = 0;
    *bufP = (char*) e_malloc( *bufsizeP );
    } else if ( *buflenP + len >= *bufsizeP ) {
    *bufsizeP = *buflenP + len + 500;
    *bufP = (char*) e_realloc( (void*) *bufP, *bufsizeP );
    }
    (void) memmove( &((*bufP)[*buflenP]), str, len );
    *buflenP += len;
    (*bufP)[*buflenP] = '\0';
}

static char* get_request_line( http_request *reqP ) { 
    int begin;
    char c;

    char *bufP = reqP->buf;
    int buf_len = reqP->buf_len;

    for ( begin = reqP->buf_idx ; reqP->buf_idx < buf_len; ++reqP->buf_idx ) {
		c = bufP[ reqP->buf_idx ];
		if ( c == '\012' || c == '\015' ) {
		    bufP[reqP->buf_idx] = '\0';
		    ++reqP->buf_idx;
		    if ( c == '\015' && reqP->buf_idx < buf_len && bufP[reqP->buf_idx] == '\012' ) {
				bufP[reqP->buf_idx] = '\0';
				++reqP->buf_idx;
		    }
		    return &(bufP[begin]);
		}
    }
    fprintf( stderr, "http request format error\n" );
    exit( 1 );
}



static void init_http_server( http_server *svrP, unsigned short port ) {
    struct sockaddr_in servaddr;
    int tmp;

    gethostname( svrP->hostname, sizeof( svrP->hostname) );
    svrP->port = port;
   
    svrP->listen_fd = socket( AF_INET, SOCK_STREAM, 0 );
    if ( svrP->listen_fd < 0 ) ERR_EXIT( "socket" )

    bzero( &servaddr, sizeof(servaddr) );
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl( INADDR_ANY );
    servaddr.sin_port = htons( port );
    tmp = 1;
    if ( setsockopt( svrP->listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*) &tmp, sizeof(tmp) ) < 0 ) 
	ERR_EXIT ( "setsockopt " )
    if ( bind( svrP->listen_fd, (struct sockaddr *) &servaddr, sizeof(servaddr) ) < 0 ) ERR_EXIT( "bind" )

    if ( listen( svrP->listen_fd, 1024 ) < 0 ) ERR_EXIT( "listen" )
}

// Set NDELAY mode on a socket.
static void set_ndelay( int fd ) {
    int flags, newflags;

    flags = fcntl( fd, F_GETFL, 0 );
    if ( flags != -1 ) {
	newflags = flags | (int) O_NDELAY; // nonblocking mode
	if ( newflags != flags )
	    (void) fcntl( fd, F_SETFL, newflags );
    }
}   

static void strdecode( char* to, char* from ) {
    for ( ; *from != '\0'; ++to, ++from ) {
	if ( from[0] == '%' && isxdigit( from[1] ) && isxdigit( from[2] ) ) {
	    *to = hexit( from[1] ) * 16 + hexit( from[2] );
	    from += 2;
	} else {
	    *to = *from;
        }
    }
    *to = '\0';
}


static int hexit( char c ) {
    if ( c >= '0' && c <= '9' )
	return c - '0';
    if ( c >= 'a' && c <= 'f' )
	return c - 'a' + 10;
    if ( c >= 'A' && c <= 'F' )
	return c - 'A' + 10;
    return 0;           // shouldn't happen
}


static void* e_malloc( size_t size ) {
    void* ptr;

    ptr = malloc( size );
    if ( ptr == (void*) 0 ) {
	(void) fprintf( stderr, "out of memory\n" );
	exit( 1 );
    }
    return ptr;
}


static void* e_realloc( void* optr, size_t size ) {
    void* ptr;

    ptr = realloc( optr, size );
    if ( ptr == (void*) 0 ) {
	(void) fprintf( stderr, "out of memory\n" );
	exit( 1 );
    }
    return ptr;
}
