const char *usage = 
"myhttpd-server:                \n"
"myhttpd [-f|-t|-p] <port>      \n";

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/wait.h>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <dlfcn.h>
#include <arpa/inet.h>

typedef void (*httprunfunc)(int ssock, const char*);
struct targs {int sock; char *clientIP;};
struct myFile {char fullpath[256]; char name[128];};

const char *password = "dXNlcjpIZWxsb1dvcmxk\0";
int QueueLength = 5;
int port;
int flag;
pthread_mutex_t mutex;
struct timespec start;
int numRequest;
double minTime;
char minUrl[256];
double maxTime;
char maxUrl[256];

// Processes time request
void processTimeRequest( int socket, char *clientIP);
int endsWith(char *str, char *suffix);
void processRequestThread(targs *args);
void poolSlave(int socket);
void execCGIBin(char *docpath, char *query, int fd);
void sendDir(char *realpath, char *dir, char *query, int fd);
void sendStats(int fd);
void sendLog(int fd);
int nSort(const void *a, const void *b);
int naSort(const void *a, const void *b);  
int lSort(const void *a, const void *b);
int laSort(const void *a, const void *b);  
int sSort(const void *a, const void *b);
int saSort(const void *a, const void *b);

extern "C" void killzombie(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

int main(int argc, char **argv) {
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    numRequest = 0;
    minTime = 100;
    maxTime = 0;

    // Print usage if not enough arguments
    // Set defaults if no arguments
    if (argc == 1) {
        port = 11064;
        flag = 0;
    
    // Parse 1 argument call and confirm that the format is correct
    } else if (argc == 2) {
        int temp = atoi(argv[1]);
        if (temp > 1024 && temp < 65536) {
            port = temp;
            flag = 0;
        } else {
            port = 11064;
            if (!strcmp(argv[1], "-f")) {
                flag = 1;
            } else if (!strcmp(argv[1], "-t")) {
                flag = 2;
            } else if (!strcmp(argv[1], "-p")) {
                flag = 3;
            } else {
                fprintf(stderr, "%s", usage);
                exit(-1);
            }
        }

    // Parse a 2 argument call and confirm format is correct
    } else if (argc == 3) {
        port = atoi(argv[2]);
        if (port > 1024 && port < 65536) {
            if (!strcmp(argv[1], "-f")) {
                flag = 1;
            } else if (!strcmp(argv[1], "-t")) {
                flag = 2;
            } else if (!strcmp(argv[1], "-p")) {
                flag = 3;
            } else {
                fprintf(stderr, "%s", usage);
                exit(-1);
            }
        } else {
            fprintf(stderr, "%s", usage);
            exit(-1);
        }

    // If more than 2 args print usage
    } else {
        fprintf( stderr, "%s", usage );
        exit(-1);
    }
         
    printf("port: %d\nflag: %d\n", port, flag);

    // Kill Zombies
    struct sigaction sa;
    sa.sa_handler = killzombie;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);

    // Get the port from the arguments
    //int port = atoi( argv[1] );
              
    // Set the IP address and port for this server
    struct sockaddr_in serverIPAddress; 
    memset( &serverIPAddress, 0, sizeof(serverIPAddress) );
    serverIPAddress.sin_family = AF_INET;
    serverIPAddress.sin_addr.s_addr = INADDR_ANY;
    serverIPAddress.sin_port = htons((u_short) port);
                        
    // Allocate a socket
    int masterSocket =  socket(PF_INET, SOCK_STREAM, 0); 
    if ( masterSocket < 0) {
        perror("socket");
        exit( -1 );
    }

    // Set socket options to reuse port. Otherwise we will
    // have to wait about 2 minutes before reusing the sae port number
    int optval = 1;  
    int err = setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR, 
                    (char *) &optval, sizeof( int ) );
                                 
    // Bind the socket to the IP address and port
    int error = bind( masterSocket, 
                    (struct sockaddr *)&serverIPAddress, sizeof(serverIPAddress) );
    if ( error ) { 
        perror("bind");
        exit( -1 );
    }
                                    
    // Put socket in listening mode and set the 
    // size of the queue of unprocessed connections
    error = listen( masterSocket, QueueLength);
    if ( error ) { 
        perror("listen");
        exit( -1 );
    }

    if (flag == 0) {
        while (true) {
            struct sockaddr_in clientIPAddress;
            int alen = sizeof( clientIPAddress );
            int slaveSocket = accept( masterSocket, (struct sockaddr *)&clientIPAddress, 
                        (socklen_t*)&alen);
   
            if ( slaveSocket < 0 ) { 
                perror( "accept" );
            }   
   
            // Process request and close
            processTimeRequest(slaveSocket, inet_ntoa(clientIPAddress.sin_addr));
            close(slaveSocket);
        }
    }

    if (flag == 1) {
        while (true) {
            struct sockaddr_in clientIPAddress;
            int alen = sizeof( clientIPAddress );
            int slaveSocket = accept( masterSocket, (struct sockaddr *)&clientIPAddress, 
                        (socklen_t*)&alen);
    
            if ( slaveSocket < 0 ) { 
                perror( "accept" );
            }   
   
            int pid = fork();
            if (pid == 0) {
                processTimeRequest(slaveSocket, inet_ntoa(clientIPAddress.sin_addr));
                close(slaveSocket);
                exit(0);
            }
            close(slaveSocket);
        }
    }

    if (flag == 2) {
        while (true) {
            struct sockaddr_in clientIPAddress;
            int alen = sizeof( clientIPAddress );
            int slaveSocket = accept(masterSocket, (struct sockaddr *)&clientIPAddress, 
                        (socklen_t*)&alen);
            
            if ( slaveSocket < 0 ) { 
                perror( "accept" );
            } 
            
            struct targs *args = (struct targs*)malloc(sizeof(struct targs));
            args->sock = slaveSocket;
            char tmpIP[10];
            strcpy(tmpIP, inet_ntoa(clientIPAddress.sin_addr));
            args->clientIP = tmpIP;
            
            pthread_t t;
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate(&attr, PTHREAD_SCOPE_SYSTEM);
            pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
            pthread_create(&t, &attr, (void *(*)(void*))processRequestThread, (void *)args);
        }
    }
    
    if (flag == 3) {
        pthread_t tid[5];
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
        pthread_mutex_init(&mutex, NULL);
        for (int i=0; i<5; i++) {
            pthread_create(&tid[i], &attr, (void *(*)(void *))poolSlave, (void *)masterSocket);
        }
        pthread_join(tid[0], NULL);
    }

    return 0;
}

void poolSlave(int socket) {
    while (true) {
        struct sockaddr_in clientIPAddress;
        int alen = sizeof(clientIPAddress);
        
        pthread_mutex_lock(&mutex);
        int slaveSocket = accept(socket, (struct sockaddr*)&clientIPAddress, (socklen_t*)&alen);
        pthread_mutex_unlock(&mutex);

        if (slaveSocket < 0) {
            perror("accept");
        }

        processTimeRequest(slaveSocket, inet_ntoa(clientIPAddress.sin_addr));
        close(slaveSocket);
    }
}

void processRequestThread(targs *args) {
    int socket = args->sock;
    char *clientIP  = args->clientIP;
    free(args);
    processTimeRequest(socket, clientIP);
    close(socket);
}

void sendLog(int fd) {
    int file = open("log.txt", O_RDONLY);

    // Send Header
    write(fd, "HTTP/1.0 200 OK\r\n", 17);
    write(fd, "Server: CS 252 lab5\r\n", 21);
    write(fd, "Content-type: text/plain\r\n\r\n", 28);
 
    // Send Document
    char data[256];
    int count;
    while(count = read(file, data, 256)){
        if(write(fd, data, count) != count){
            perror("write");
            bzero(data, 256);
            break;
        }
    }
    close(file);
}

void sendStats(int fd) {
    struct timespec curr;
    clock_gettime(CLOCK_MONOTONIC_RAW, &curr);
    char buff[16];

    int elapsed = curr.tv_sec - start.tv_sec;
    write(fd, "HTTP/1.1 200 Document follows\r\nServer: CS 252 lab5\r\n"\
            "Content-type: text/html\r\n\r\n"\
            "Author: Joseph Klug<br>The server has been up for ", 129);
    sprintf(buff, "%d", elapsed);
    write(fd, buff, strlen(buff));
    write(fd, " seconds<br>Requests: ", 22);
    sprintf(buff, "%d", numRequest);
    write(fd, buff, strlen(buff));
    write(fd, "<br>Minimum Request Time: ", 26);
    sprintf(buff, "%.6f", minTime);
    write(fd, buff, strlen(buff));
    write(fd, "<br>Minimum Request URL: ", 25);
    write(fd, minUrl, strlen(minUrl));
    write(fd, "<br>Maximum Request Time: ", 26);
    sprintf(buff, "%.6f", maxTime);
    write(fd, buff, strlen(buff));
    write(fd, "<br>Maximum Request URL: ", 25);
    write(fd, maxUrl, strlen(maxUrl));
}

void execCGIBin(char *realpath, char *query, int fd) {
    //if (query) {}
    char *args[] = {realpath, query, NULL};


    if (access(realpath, F_OK) == -1) {
        perror("access");
        return;
    }

    write(fd, "HTTP/1.1 200 Document follows\r\nServer: CS 252 lab5\r\n", 44);

    if (endsWith(realpath, ".so")) {
        
        void * lib = dlopen( realpath, RTLD_LAZY );

        if ( lib == NULL ) { 
            perror( "dlopen");
            return;
        }

        httprunfunc httprun;

        httprun = (httprunfunc) dlsym( lib, "httprun");
        if ( httprun == NULL ) {
            perror( "dlsym: httprun not found:");
            return;
        }

        // Call the function
        httprun( fd, query);

    } else {
        int tmpout = dup(1);
        dup2(fd, 1);
        close(fd);

        int p = fork();
        if (p == 0) {
            setenv("REQUEST_METHOD", "GET", 1);
            setenv("QUERY_STRING", query, 1);
            execvp(realpath, args);
            exit(2);
        }

        dup2(tmpout, 1);
        close(tmpout);
    }
}

void sendDir(char *realpath, char *dir, char *query, int fd) {
    std::string realPath(realpath);
    if (!endsWith(realpath, "/"))
        realPath = realPath + "/";
    std::string realDir(dir);
    if (!endsWith(dir, "/"))
        realDir = realDir + "/";
    size_t found;
    if ((found = realDir.find_last_of('?')) != std::string::npos)
        realDir = realDir.substr(0, found);

    char nLink[] = "?C=N;O=A";
    char lLink[] = "?C=L;O=A";
    char sLink[] = "?C=S;O=A";
    char dLink[] = "?C=D;O=A";
    char g = 'N';
    char m = 'A';

    if (strlen(query)) {
        g = query[2];
        m = query[6];
    }

    if (strlen(query) != 0 && m == 'A') {
        if (g == 'N') {
            nLink[7] = 'D';
        } else if (g == 'L') {
            lLink[7] = 'D';
        } else if (g == 'S') {
            sLink[7] = 'D';
        } else if (g == 'D') {
            dLink[7] = 'D';
        }
    }

    printf("%s\n", nLink);

    std::string parent (dir);
    parent = parent.substr(0, parent.size() - 3);
    parent = parent.substr(0, parent.find_last_of("/") + 1);

    printf("Directory: %s\n", realPath.c_str());
    printf("Parent Dir: %s\n", parent.c_str());

    write(fd, "HTTP/1.0 200 OK\r\n", 17);
    write(fd, "Server: CS 252 lab5\r\n", 21);
    write(fd, "Content-type: text/html\r\n\r\n", 27);
    write(fd, "<html><head><title>Index of ", 28);
    write(fd, realpath, strlen(realpath));
    write(fd, "</title></head><body><h1>Index of ", 34);
    write(fd, realpath, strlen(realpath));
    write(fd, "</h1><table><tbody><tr><th valign=\"top\"><img src=\"/icons/blank.gif\">"\
            "</th><th><a href=\"", 86);
    write(fd, nLink, strlen(nLink));
    write(fd, "\">Name</a></th><th><a href=\"", 28);
    write(fd, lLink, strlen(lLink));
    write(fd, "\">Last modified</a></th><th><a href=\"", 37);
    write(fd, sLink, strlen(sLink));
    write(fd, "\">Size</a></th><th><a href=\"", 28);
    write(fd, dLink, strlen(dLink));
    write(fd, "\">Description</a></th></tr><tr><th colspan=\"5\"><hr></th></tr><tr>"\
            "<td valign=\"top\"><img src=\"/icons/back.gif\"></td><td><a href=\"", 127);
    write(fd, parent.c_str(), parent.length());
    write(fd, "\">Parent Directory</a></td><td>&nbsp</td><td align=\"right\"> - "\
            "</td><td> <td></tr>", 81);


    struct dirent **entList;
    int fileCount = scandir(realpath, &entList, NULL, alphasort);

    int realCount = 0;
    struct myFile files[fileCount];
    for (int i=0; i<fileCount; i++) {
        if (entList[i]->d_name[0] != '.') {
            std::string filename(entList[i]->d_name);
            printf("%s\n", filename.c_str());
            std::string fullName = realPath + std::string(filename);
            printf("%s\n", fullName.c_str());

            strcpy(files[realCount].fullpath, fullName.c_str());
            strcpy(files[realCount].name, filename.c_str());
            realCount++;
        }
        free(entList[i]);
    }

    if (g == 'L' && m == 'D') {
        qsort(files, realCount, sizeof(myFile), lSort);
    } else if (g == 'S' && m == 'D') {
        qsort(files, realCount, sizeof(myFile), sSort);
    } else if (g == 'L' && m == 'A') {
        qsort(files, realCount, sizeof(myFile), laSort);
    } else if (g == 'S' && m == 'A') {
        qsort(files, realCount, sizeof(myFile), saSort);
    } else if (g == 'N' && m == 'A') {
        qsort(files, realCount, sizeof(myFile), naSort);
    }


    for (int i=0; i < realCount; i++) {
        if (1) {
            char *filename = files[i].name;
            std::string fullName = files[i].fullpath;
            //std::string 

            FILE* file = fopen(fullName.c_str(), "r");
            fseek(file, 0L, SEEK_END);
            long size = ftell(file);
            fclose(file);
            char fileSize[32];
            sprintf(fileSize, "%ld", size);

            char type[18];
            DIR *d = opendir(fullName.c_str());
            if (d) {
                closedir(d);
                strcpy(type, "/icons/folder.gif");
                strcpy(fileSize, "  - ");
            } else if (endsWith(filename, ".html")) {
                strcpy(type, "/icons/text.gif");
            } else if (endsWith(filename,".gif") || endsWith(filename,".png")
                            || endsWith(filename,".svg")) {
                strcpy(type, "/icons/image.gif");
            } else {
                strcpy(type, "/icons/unknown.gif");
            }

            char time[64];
            struct stat stats;
            stat(fullName.c_str(), &stats);
            strftime(time, 64, "%F %H:%M", localtime(&(stats.st_mtime)));

            printf("%s: %ld\t%s\n", fullName.c_str(), size, time);

            write(fd, "<tr><td valign=\"top\"><img src=\"", 31);
            write(fd, type, strlen(type));
            write(fd, "\" alt=\"[   ]\"></td><td><a href=\"", 32);
            write(fd, realDir.c_str(), realDir.length());
            write(fd, filename, strlen(filename));
            write(fd, "\">", 2);
            write(fd, filename, strlen(filename));
            write(fd, "</a></td><td align=\"right\">", 27);
            write(fd, time, strlen(time));
            write(fd, "</td><td align=\"right\">", 23);
            write(fd, fileSize, strlen(fileSize));
            write(fd, "</td><td>&nbsp</td></tr>", 24);
        }
    }

    write(fd, "</tbody></table></body></html>", 30);
}

void processTimeRequest(int fd, char* clientIP) {
    struct timespec begin, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &begin);
    numRequest++;

    // Buffer used to store the name received from the client
    const int MaxLen = 1024;

    char currString[MaxLen + 1];
    char docpath[MaxLen + 1];
    char usrPassword[MaxLen + 1];
    
    char buffer[10000];
    int count = 0;

    int length = 0;
    int n;

    // Currently character read
    unsigned char newChar;
    // Last character read
    unsigned char oldChar = 0;

    int gotGet = 0;
    int gotPath = 0;
    int gotAuth = 0;
    int crlfDouble = 0;
 
    // Process Request
    while ( length < MaxLen && ( n = read( fd, &newChar, sizeof(newChar) ) ) > 0 ) {
        printf("%c", newChar);
        buffer[count] = newChar;
        count++;
        length++;

        // A whole word has been read
        if (newChar == ' ') {
            currString[length - 1] = '\0';
            if (gotGet == 0 && !strncmp("GET", currString, 3)) {
                gotGet++;
            } else if (gotGet == 1 && gotPath == 0) {
                //currString[length - 1] = '\0';
                strcpy(docpath, currString);
                gotPath++;
            } else if (gotAuth == 0 && !strncmp("Authorization", currString, 13)) {
                gotAuth++;
            } else if (gotAuth == 1 && !strncmp("Basic", currString, 5)) {
                gotAuth++;
            }
            length = 0;

        // The a line has been read until crlf
        } else if (newChar == '\n' && oldChar == '\r') {
            currString[length - 2] = '\0';
            if (gotAuth == 2) {
                strcpy(usrPassword, currString);
                gotAuth++;
            } else if (crlfDouble) {
                break;
            } else {
                crlfDouble++;
            }
            length = 0;

        // Put the read char in currString if normal char
        } else {
            if (crlfDouble && newChar != '\r') {
                crlfDouble = 0;
            }
            oldChar = newChar;
            currString[length - 1] = newChar;
        }
    }

    buffer[count] = '\0';
    printf("%s\n", buffer);
    printf("docpath is %s\n", docpath);
    printf("password is %s\n", usrPassword);

    if (!gotGet) return;
    // if the password given does not match send 401 Unauthorixed
    if (strcmp(usrPassword, password) != 0) {
        printf("authentication failed\n");
        write(fd, "HTTP/1.1 401 Unaothorized\r\n", 27);
        write(fd, "WWW-Authenticate: Basic Realm=", 30);
        write(fd, "\"klugj-cs252-lab5\"", 18);
        write(fd, "\r\n\r\n", 4);
        shutdown(fd, SHUT_WR);
        return;
    }
        

    //map docpath t17926492o realpath
    char cwd[256] = {0};
    getcwd(cwd, 256);
    char filepath[MaxLen];

    bool cgiBin = false;

    if (!strncmp(docpath, "/icons", 6)) {
        sprintf(filepath, "%s/http-root-dir%s", cwd, docpath);
    } else if (!strncmp(docpath, "/htdocs", 7)) {
        sprintf(filepath, "%s/http-root-dir%s", cwd, docpath);
    } else if (!strncmp(docpath, "/cgi-bin", 8)) {
        sprintf(filepath, "%s/http-root-dir%s", cwd, docpath);
    }else if (!strcmp(docpath, "/")) {
        sprintf(filepath, "%s/http-root-dir/htdocs/index.html", cwd);
    }else {
        sprintf(filepath, "%s/http-root-dir/htdocs%s", cwd, docpath);
    }

    std::string cFilePath(filepath);
    std::string query;
    size_t found;
    if ((found = cFilePath.find_last_of('?')) != std::string::npos) {
        query = cFilePath.substr(found+1);
        cFilePath = cFilePath.substr(0, found);
    }

    printf("realpath is %s\n", cFilePath.c_str());
    printf("query is %s\n", query.c_str());

    DIR *d;

    if (strstr(docpath, "cgi-bin")) {
        execCGIBin(const_cast<char*>(cFilePath.c_str()), const_cast<char*>(query.c_str()), fd);
    } else if ((d = opendir(cFilePath.c_str()))) {
        closedir(d);
        sendDir(const_cast<char*>(cFilePath.c_str()), docpath, 
                        const_cast<char*>(query.c_str()), fd);
    } else if (endsWith(filepath, "/stats") || endsWith(filepath, "/stats/")) {
        sendStats(fd);
    } else if (endsWith(filepath, "/logs") || endsWith(filepath, "/logs/")) {
        sendLog(fd);
    } else {
        // get content type
        char contentType[16];
        if (endsWith(filepath, ".html") || endsWith(filepath, ".html/")) {
            strcpy(contentType, "text/html");
        } else if (endsWith(filepath, ".gif") || endsWith(filepath, ".gif/")) {
            strcpy(contentType, "image/gif");
        } else if (endsWith(filepath, ".svg") || endsWith(filepath, ".svg/")) {
            strcpy(contentType, "image/svg+xml");
        } else if (endsWith(filepath, ".png") || endsWith(filepath, ".png/")) {
            strcpy(contentType, "image/png");
        } else if (endsWith(filepath, ".xbm")) {
            strcpy(contentType, "image/x-xbm");
        } else if (endsWith(filepath, ".ico") || endsWith(filepath, ".ico/")) {
            strcpy(contentType, "image/x-icon");
        }else {
            strcpy(contentType, "text/plain");
        }

        printf("content type is %s\n\n\n", contentType);

        int file = open(filepath, O_RDWR);

        if (file < 0) {
            // File Not Found Error
            perror("open");

            const char *notFound = "File not Found";
            write(fd, "HTTP/1.0 404FileNotFound\r\n", 26);
            write(fd, "Server: CS 252 lab5\r\n", 21);
            write(fd, "Content-type: ", 14);
            write(fd, contentType, strlen(contentType));
            write(fd, "\r\n\r\n", 4);
            write(fd, notFound, strlen(notFound));

        } else {
            // Send Header
            write(fd, "HTTP/1.0 200 OK\r\n", 17);
            write(fd, "Server: CS 252 lab5\r\n", 21);
            write(fd, "Content-type: ", 14);
            write(fd, contentType, strlen(contentType));
            write(fd, "\r\n\r\n", 4);

            // Send Document
            char data[MaxLen];
            int count;
            while(count = read(file, data, MaxLen)){
                if(write(fd, data, count) != count){
                    perror("write");
                    bzero(data, MaxLen);
                    break;
                }
            }
            close(file);
        }
    }

    shutdown(fd, SHUT_WR);
    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    double elapsed = end.tv_sec - begin.tv_sec + (end.tv_nsec - begin.tv_nsec) * 1.0e-9;

    if (elapsed < minTime) {
        minTime = elapsed;
        strcpy(minUrl, docpath); 
    }
    if (elapsed > maxTime) {
        maxTime = elapsed;
        strcpy(maxUrl, docpath);
    }

    FILE *log = fopen("log.txt", "at");
    fprintf(log, "%s - %s\n", clientIP, docpath);
    fclose(log);
}

int nSort(const void *a, const void *b) {
    struct myFile *fileA = (myFile *)a;
    struct myFile *fileB = (myFile *)b;
    return strcmp(fileA->name, fileB->name);
}

int naSort(const void *a, const void *b) {
    return nSort(b, a);
}

int lSort(const void *a, const void *b) {
    struct myFile *fileA = (myFile *)a;
    struct myFile *fileB = (myFile *)b;
    struct stat astat, bstat;
    stat(fileA->fullpath, &astat);
    stat(fileB->fullpath, &bstat);
    return difftime(astat.st_mtime, bstat.st_mtime);
}

int laSort(const void *a, const void *b) {
    return lSort(b, a);
}

int sSort(const void *a, const void *b) {
    struct myFile *fileA = (myFile *)a;
    struct myFile *fileB = (myFile *)b;
    struct stat astat, bstat;
    stat(fileA->fullpath, &astat);
    stat(fileB->fullpath, &bstat);
    int as = astat.st_size;
    int bs = bstat.st_size;
    return as > bs;
}

int saSort(const void *a, const void *b) {
    return sSort(b, a);
}

int endsWith(char *str, char *suffix) {
    if (!str || !suffix)
        return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix >  lenstr)
        return 0;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}
