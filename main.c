/*
 ------------------------------------------------------------------------------------
                            Protocolo FTP (documento RFC 959)
                            Manuel Amorim    +    Diogo Costa
 ------------------------------------------------------------------------------------
 Using PCRE2 for regular expressions
 //install with pack manager
 Ubuntu:
 apt-get install libpcre3 libpcre3-dev
 ArchLinux:
 pacman -Su pcre pcre2
 CentOS:
 yum install pcre pcre-devel
 Mac:
 brew install pcre2

 Compile:
 gcc -Wall main.c -lpcre2-8 -o main

*/
#define PCRE2_CODE_UNIT_WIDTH 8


#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pcre2.h>

#define FTP_PORT 21
#define RESPONSE_MAX_LENGTH 400
#define FILE_BUF_SIZE 1024

typedef struct connectionParameters{
    char user[100];
    char password[100];
    char host[400];
    char path[400];
    char file_name[100];
    //char host_name[100]; //official host name - returned by getip
    char ip[100]; //returned by getip
}connectionParameters;


//BASED TFC1738
void getRegex(char* reg_ex){
    /* Escape characters not implemented:
        * Can't use (":" |  "@" | "/") within user or password
        * Can't use ("/") inside folders or files of working directory
    */

    //Side-Note (used in this implementation):
    //escape in pcre regex: https://www.pcre.org/original/doc/html/pcrepattern.html
    //escape in C string: % --> %%; \ --> \\; ' --> \'

    char alpha[] = "a-zA-Z";
    char digit[] = "0-9";
    char safe[] = "$\\-_.+";
    char extra[] = "!*\'(),";

    /*Escape -> not implemented
    char hex[] = "0-9A-Fa-f";
    char escape[200];
    sprintf(escape, "%%[%s][%s]", hex, hex);
    //escape = "%" hex hex
    */

    char unreserved[200];
    sprintf(unreserved, "%s%s%s%s", alpha, digit, safe, extra);
    //unreserved = alpha | digit | safe | extra

    char uchar[200];
    sprintf(uchar, "%s", unreserved);
    //uchar = unreserved | escape -->  escape not implemented

    char user_or_password[200];
    sprintf(user_or_password, "[%s;?&=]*", uchar);
    //user = *[ uchar | ";" | "?" | "&" | "=" ]

    //simplified comparison (compared to RFC)
    char path[200];
    sprintf(path, "[%s?:@&=/]*", uchar);
    //fsegment = *[ uchar | "?" | ":" | "@" | "&" | "=" | "/"]

    char login[200];
    sprintf(login, "(?:(?<user>%s)(?::(?<password>%s))?@)?", user_or_password, user_or_password);
    // login  = [ user [ ":" password ] "@" ]

    //simplified comparison (compared to RFC)
    char host[200];
    sprintf(host, "[.%s%s]+\\.[.%s%s]+", alpha, digit, alpha, digit);
    //host = ["." | alpha | digit ]+ "."" ["." | alpha | digit ]+

    sprintf(reg_ex,"^ftp://%s(?<host>%s)(?<path>/%s)?$", login, host, path);
    //reg = "ftp://" login host ["/" path]
}

//1
// RFC1738 -> url: ftp://[<user>:<password>@]<host>/<url-path>
// RETURN -1 --> ERROR; 0 --> SUCCESSFUL
// parms returned through parms argument
int parseURL(char *url, connectionParameters *params){
    char regexSTR[200];
    getRegex(regexSTR);
    printf("\nReference regular expression: %s\n", regexSTR);

    int errcode;
    PCRE2_SIZE erroffset;
    pcre2_code *regex = pcre2_compile(regexSTR, strlen(regexSTR), 0, &errcode, &erroffset, NULL);
    if (regex == NULL){ 
        PCRE2_UCHAR8 buffer[128];
        pcre2_get_error_message(errcode, buffer, 120);
        printf("%d\t%s\n", errcode, buffer);
        printf("ERROR COMPILING REGEX");
        return -1;
    }

    pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(regex, NULL);
    int rc = pcre2_match(regex, url, strlen(url), 0, 0, match_data, NULL);

    PCRE2_SIZE* ovector;
    if (rc < 0 || (int)(ovector = pcre2_get_ovector_pointer(match_data))[0] != 0 ) {
        printf("No match\n");
        pcre2_match_data_free(match_data);
        pcre2_code_free(regex);
        return -1;
    }
    
    /*
    Print match and all groups
    for(int i = 0; i < rc; i++){
        PCRE2_SPTR start = url + ovector[2*i];
        PCRE2_SIZE slen = ovector[2*i+1] - ovector[2*i];
        printf( "%2d: %.*s\n", i, (int)slen, (char *)start );
    }
    */
    int namecount;
    pcre2_pattern_info( regex, PCRE2_INFO_NAMECOUNT, &namecount);
    //printf("\nNumber of groups: %d\n", namecount);

    PCRE2_SPTR tabptr;
    PCRE2_SPTR name_table;
    int name_entry_size;
    pcre2_pattern_info( regex, PCRE2_INFO_NAMETABLE, &name_table);         
    pcre2_pattern_info( regex, PCRE2_INFO_NAMEENTRYSIZE, &name_entry_size);

    tabptr = name_table;
    for (int i = 0; i < namecount; i++){
        int n = (tabptr[0] << 8) | tabptr[1];

        char group_name[15];
        strcpy(group_name, tabptr + 2);

        char info[400];
        sprintf( info,"%.*s", (int)(ovector[2*n+1] - ovector[2*n]), url + ovector[2*n]);

        //printf("(%d) %s: %s\n", n, group_name, info); //TODO --> remove this print and put in main

        if(strcmp(group_name, "host") == 0){
            strcpy(params->host, info);
        }else if(strcmp(group_name, "user") == 0){
            if(strcmp(info, "")==0){
                strcpy(params->user, "anonymous");
            }else{
                strcpy(params->user, info);
            }
        }else if(strcmp(group_name, "password") == 0){
            strcpy(params->password, info);
        }else if(strcmp(group_name, "path") == 0){
            if(info[0] == '\0' || (info[0] == '/' && info[1] == '\0')){
                strcpy(params->path, "/");
                strcpy(params->file_name, "");
            }else{
                //Getting file name and path
                char* last_bar_ptr = strrchr(info, '/');
                strcpy(params->file_name, last_bar_ptr + 1);
                *last_bar_ptr = '\0';
                strcpy(params->path, info);
            }
        }else {
            printf("ERROR: unknown group name!");
        }

        tabptr += name_entry_size;
    }

    pcre2_match_data_free(match_data);
    pcre2_code_free(regex);
    return 0;
}

//2 
// RETURN -1 --> ERROR; 0 --> SUCCESSFUL
// ip returned through ip argument
int getip(char* host, char* ip){
    struct hostent *h;

    if ((h = gethostbyname(host)) == NULL) {
        printf("ERROR: gethostbyname");
        return -1;
    }

    printf("Host name  : %s\n", h->h_name);
    printf("IP Address : %s\n", inet_ntoa(*((struct in_addr *) h->h_addr)));

    strcpy(ip, inet_ntoa(*((struct in_addr *) h->h_addr)));
    return 0;
}

//3
// (based socketTCP.c)
// RETURN -1 --> ERROR; else --> socketfd (int)
int createAndConnectSocket(char* ip, int port){
    int sockfd;
    struct sockaddr_in server_addr;

    /*server address handling*/
    bzero((char *) &server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);    /*32 bit Internet address network byte ordered*/
    server_addr.sin_port = htons(port);        /*server TCP port must be network byte ordered */

    /*open a TCP socket*/
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Error opening socket");
        return -1;
    }

    /*connect to the server*/
    if (connect(sockfd,
                (struct sockaddr *) &server_addr,
                sizeof(server_addr)) < 0) {
        perror("Error connecting to the server");
        return -1;
    }
    return sockfd;

}

//4.1
// RETURN -1 --> ERROR; 0 --> SUCCESSFUL
// (based socketTCP.c)
int sendToControlSocket(int socket_fd, char *header, char *body){
    printf("Sending:   %s %s\n", header, body);

    char *message = malloc(strlen(header) + 1 + strlen(body) + 1 + 1);
    sprintf(message, "%s %s\n", header,body );

    //Write Header
    int bytes = write(socket_fd, message, strlen(message));
    if (bytes > 0) printf("Written Header bytes: %ld\n", bytes); 
    else {
        printf("Error writting to socket\n");
        free(message);
        return -1;
    }
    free(message);
    
    return 0;
}

//4.2
//RFC 959-FTP
void receiveFromControlSocket(int socket_fd, int size, char *response) {
    printf("Receiving from control socket... \n");
    FILE *fp = fdopen(socket_fd, "r");

    while(1){
        response = fgets(response, size, fp); //returns char* (and also saves in 1st argument )
        printf("%s", response);

        if('1' <= response[0] && response[0]<='5' && response[3] == ' ') break;
    }
}



//4
//RFC 959-FTP
/* RETURN
1 -> X
2 -> Successful
3 -> Request needs aditional info, try again
4 -> X
5 -> Error, aborting
-1 -->4: sendToControlSocket() error
*/
int sendReceiveControlSocketAction(int socket_fd, char *header, char *body, int response_size, char *response){
    if (sendToControlSocket(socket_fd, header, body) == -1) return -1;
    while(1){
        receiveFromControlSocket(socket_fd, response_size, response);
        if(response[0] == '1'){
            /*Positive Preliminary reply
            The requested action is being initiated; 
            expect another reply before proceeding with a new command;
            */
            printf("-->Listening to another reply<--\n");
            continue;
        }else if(response[0] == '2'){
            /*Positive Completion reply
            The requested action has been successfully completed.  
            */
            printf("-->Successful request<--\n");
            return 2;
        }else if(response[0] == '3'){
            /*Positive Intermediate reply
            The command has been accepted, but the user should send another command specifying more information.
            */
            printf("-->Request needs aditional info, try again <--\n");
            return 3;
        }else if(response[0] == '4'){
            /*Transient Negative Completion reply
            The command was not accepted but the error condition is temporary and 
            the action may be requested again exactly the same. 
            */
            printf("-->Request not accepted. Trying again exactly the same... <--\n");
            if (sendToControlSocket(socket_fd, header, body) == -1) return -1;
            continue;
        }else if(response[0] == '5'){
            /*Permanent Negative Completion reply
            The command was not accepted. 
            The User-process is discouraged from repeating the exact request.
            */
            printf("-->Request not accepted. Aborting... <--");
            return 5;
        }
    }

}

//5
//-1 --> ERROR
//0 --> SUCCESS
int login(int socket_fd, char* username, char* password){
    printf("\n\nSending Username:\n");
    char response[RESPONSE_MAX_LENGTH];
    int ftp_code = sendReceiveControlSocketAction(socket_fd, "user", username, RESPONSE_MAX_LENGTH, response);

    if (ftp_code == 3) {
        printf("Sending Password\n");
        ftp_code = sendReceiveControlSocketAction(socket_fd, "pass", password, RESPONSE_MAX_LENGTH, response);
        if(ftp_code==2){
            printf("Login successful\n");
            return 0;
        }else{
            printf("Error sending password\n");
            return -1;
        }

    }else if(ftp_code == 2){
        printf("Login successful\n");
        return 0;
    }else{
        printf("Error Login In\n");
        return -1;
    }

};

//6 -> go to path
//-1 --> ERROR
//0 --> SUCCESS
int changeWorkingDirectory(int socket_fd, char *path){
    printf("\n\nChanging working directory to %s:\n", path);
    char response[RESPONSE_MAX_LENGTH];
    int ftp_code = sendReceiveControlSocketAction(socket_fd, "CWD", path, RESPONSE_MAX_LENGTH, response);
    if(ftp_code == 2){
        printf("CWD successful\n");
        return 0;
    }
    return -1;
};

//7
// -> enter Passive Mode (send PASV command)
// -> open socket with PASV port
//-1 --> ERROR
//else --> pasv_fd -> Successful
int getServerPortForFile(int socket_fd){
    printf("\n\nEntering Passive Mode:\n");
    char response[RESPONSE_MAX_LENGTH];
    int ftp_code = sendReceiveControlSocketAction(socket_fd, "PASV", "", RESPONSE_MAX_LENGTH, response);
    if(ftp_code == 2){
        char* ptr_first_parenthesis = strchr(response, '(');

        int ip_parts[6];
        if(sscanf(ptr_first_parenthesis, "(%d,%d,%d,%d,%d,%d)", &ip_parts[0], &ip_parts[1], &ip_parts[2], &ip_parts[3],&ip_parts[4],&ip_parts[5]) <0){
            printf("Error parsing the ip from PASV");
            return -1;
        }
        char ip[100];
        sprintf(ip, "%d.%d.%d.%d", ip_parts[0], ip_parts[1], ip_parts[2], ip_parts[3]);
        //PORT = element5 * 256 + element6
        int port = ip_parts[4] *256 + ip_parts[5];
        
        printf("PASV successful, ip:%s, port:%d\n", ip, port);

        //Create socket
        int pasv_fd = createAndConnectSocket(ip, port);
        if(pasv_fd == -1){
            printf("ERROR creating passive socket\n");
            return -1;
        }
        
        printf("Socket created and connected\n");
        return pasv_fd;
    }
    return -1;
}

//8
// -> send command RETR <filename> to control socket, to start transfering file
//-1 --> ERROR
//0 --> SUCCESS
int retrCommandControl(int ctrl_socket_fd, char* file_name){
    printf("\n\nSending RETR %s command to control socket:\n", file_name);
    char response[RESPONSE_MAX_LENGTH];
    int ftp_code = sendReceiveControlSocketAction(ctrl_socket_fd, "RETR", file_name, RESPONSE_MAX_LENGTH, response);
    if(ftp_code == 2){
        printf("Transfer Complete to passive socket\n");
        return 0;
    }
    return -1;
};

//9
// Gets data from passive socket and saves in a file in your computer
//Closes data socket
//-1 --> ERROR
//0 --> SUCCESS
int saveFile(int pasv_socket_fd, char* filename){
    FILE *fp = fopen(filename, "w"); 
    if (fp == NULL){
        printf("Error creating file %s\n", filename);
        return -1;
    }

    char buf[FILE_BUF_SIZE];
    int bytes;
    printf("Saving data into file %s...\n", filename);
    while((bytes = read(pasv_socket_fd, buf, FILE_BUF_SIZE))){
        if((bytes = fwrite(buf, bytes, 1, fp)) < 0){
            printf("Error writing data to file\n");
            return -1;
        }
    }
    printf("Finished witing file %s\n", filename);
    if(fclose(fp) !=0){
        printf("Error closing file\n");
        return -1;
    }
    printf("Closed successfully file %s\n", filename);
    close(pasv_socket_fd);

}

//10 -> send command QUIT
//-1 --> ERROR
//0 --> SUCCESS
int disconnectCtrlSocket(int ctrl_socket_fd){
    printf("\n\nSending QUIT command to control socket:\n");
    char response[RESPONSE_MAX_LENGTH];
    int ftp_code = sendReceiveControlSocketAction(ctrl_socket_fd, "QUIT", "", RESPONSE_MAX_LENGTH, response);
    if(ftp_code == 2){
        printf("Disconnecting successful\n");
        return 0;
    }
    return -1;
}

//download ftp://[<user>:<password>@]<host>/<url-path>
int main(int argc, char ** argv) {
    if(argc != 3 || strcmp(argv[1], "download") !=0){
        printf("usage: download ftp://[<user>:<password>@]<host>/<url-path> \n");
        exit(1);
    }

    char* url = argv[2];
    printf("URL: %s\n", url);

    connectionParameters *params = malloc(sizeof(*params));
    //1
    if(parseURL(url, params) == -1){
        printf("ERROR parse");
        return -1;
    }

    printf("User: %s\n", params->user);
    printf("Password: %s\n", params->password);
    printf("Host: %s\n", params->host);
    printf("Path: %s\n", params->path);
    printf("Filename: %s\n", params->file_name);


    //2
    char ip[100];
    if(getip(params->host, ip)==-1){
        printf("ERROR Getip");
        return -1;
    }
    strcpy(params->ip, ip);

    //3
    int ctrl_fd = createAndConnectSocket(params->ip, FTP_PORT);
    if(ctrl_fd == -1){
        printf("ERROR createAndConnectSocket");
        return -1;
    }else{
        printf("Socket created and connected");
    }

    //4 - Test
    if(sendToControlSocket(ctrl_fd, "test", "test") == -1){
        printf("ERROR sendToControlSocket test\n");
        return -1;
    }else{
        printf("Test successful\n");
    }

    char response[RESPONSE_MAX_LENGTH];
    receiveFromControlSocket(ctrl_fd, RESPONSE_MAX_LENGTH, response);

    //5
    if(login(ctrl_fd, params->user, params->password)==-1){
        printf("ERROR login()");
        return -1;
    }

    //6
    if(changeWorkingDirectory(ctrl_fd, params->path)==-1){
        printf("ERROR changeWorkingDirectory()");
        return -1;
    }

    //7
    int pasv_fd = getServerPortForFile(ctrl_fd);
    if(pasv_fd ==-1){
        printf("ERROR getServerPortForFile()");
        return -1;
    }

    //8
    if(retrCommandControl(ctrl_fd, params->file_name)==-1){
        printf("ERROR retrCommandControl()");
        return -1;
    }

    //9
    if(saveFile(pasv_fd, params->file_name)==-1){
        printf("ERROR saveFile()");
        return -1;
    }

    //10
    if(disconnectCtrlSocket(ctrl_fd)==-1){
        printf("ERROR disconnectCtrlSocket()");
        return -1;
    }

    return 0;
}
