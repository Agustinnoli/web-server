#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/epoll.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>

#define MAX_EVENTS 1024
#define MAX_CACHE_ENTRIES 1024
#define CONTENT_TYPE_LENGTH 64
#define MAX_FDS 100000
#define TIMEOUT_SEGUNDOS 120
#define MAX_PATH_LENGTH 4096
#define MAX_CACHE_PATH_LENGTH 256
time_t ultimaActividad[MAX_FDS];

volatile int corriendo =1;



char pathAPublic[MAX_PATH_LENGTH];  
size_t lengthPathAPublic;


int clientesActivos[MAX_FDS];
int numClientesActivos = 0;

char* leerArchivo(const char* path, long *size)
{
        char absoluto[MAX_PATH_LENGTH];
        snprintf(absoluto, sizeof(absoluto), "%s%s", pathAPublic, path);
        FILE* file = fopen(absoluto, "rb"); if(!file) return NULL;
        if(fseek(file,0, SEEK_END)!=0) goto error;
        *size = ftell(file); if(*size <0) goto error;
        if(fseek(file,0, SEEK_SET)!=0) goto error;
        char* buffer = malloc (*size+1); if(!buffer) goto error;
        if(fread(buffer, 1, *size, file)!= *size){free(buffer); goto error;}
        fclose(file);
        buffer[*size] = '\0';
        return buffer;
error:
        fclose(file);
        return NULL;
}

typedef struct {
        char path[MAX_CACHE_PATH_LENGTH];
        char *content;
        long size;
        char contenType[CONTENT_TYPE_LENGTH];
        time_t ultimaVezModificado; 
} cacheEntry;

cacheEntry cache[MAX_CACHE_ENTRIES];
int cacheSize = 0;

const char* detectarExtension(const char* filePath) {
    const char* ext = strrchr(filePath, '.');  // último punto
    if (!ext) return "application/octet-stream";

    if (!strcmp(ext, ".html")) return "text/html";
    if (!strcmp(ext, ".css"))  return "text/css";
    if (!strcmp(ext, ".js"))   return "application/javascript";
    if (!strcmp(ext, ".json")) return "application/json";
    if (!strcmp(ext, ".png"))  return "image/png";
    if (!strcmp(ext, ".jpg"))  return "image/jpeg";
    if (!strcmp(ext, ".ico"))  return "image/x-icon";
    if (!strcmp(ext, ".svg"))  return "image/svg+xml";
    if (!strcmp(ext, ".woff2")) return "font/woff2";
    return "application/octet-stream";
}

cacheEntry* agregarEntradaCache(const char* path, char* content, long size, time_t TiempoDeModificacion){
        for(int i =0; i< cacheSize; i ++){
                if (!strcmp(cache[i].path, path)){
                        free(cache[i].content);
                        cache[i].content = content;
                        cache[i].size = size;
                        cache[i].ultimaVezModificado = TiempoDeModificacion;
                        return &cache[i];
                }
        }
        if(cacheSize >= MAX_CACHE_ENTRIES) return NULL;
        cache[cacheSize].content = content;
        cache[cacheSize].size = size;
        cache[cacheSize].ultimaVezModificado = TiempoDeModificacion;
        strncpy(cache[cacheSize].path, path, MAX_CACHE_PATH_LENGTH);
        cache[cacheSize].path[MAX_CACHE_PATH_LENGTH - 1] = '\0';
        strncpy(cache[cacheSize].contenType, detectarExtension(path), CONTENT_TYPE_LENGTH);
        cacheSize ++;
        return &cache[cacheSize-1];
}

//void llenarCache(const char* dirPath, const char* urlBase){
//        DIR* dir = opendir(dirPath); if(!dir){perror("opendir"); return;}
//        struct dirent* entry;
//        while((entry = readdir(dir))!=NULL){
//                if(entry->d_name[0] == '.') continue;
//                if(cacheSize >= MAX_CACHE_ENTRIES) break;
//
//                char filePath[512];
//                snprintf(filePath, sizeof(filePath), "%s/%s", dirPath, entry->d_name);
//
//                //"./public/index.html" → "/index.html"
//
//                char urlPath[MAX_PATH_LENGTH_LENGTH];
//                snprintf(urlPath, sizeof(urlPath), "%s/%s", urlBase, entry->d_name);
//                
//                struct stat info; 
//                if(stat(filePath, &info)!=0) continue;
//                if(S_ISDIR(info.st_mode)){llenarCache(filePath,urlPath); continue;}
//
//                long size;
//                char* content = leerArchivo(filePath,&size);if(!content) continue;
//
//                //if(!strcmp(entry->d_name,"index.html")){ // lo agrego 2 veces a la cache, una como / y oyta como index.html
//                //       char* content2 = malloc(size + 1);
//                //        memcpy(content2, content, size +1);
//                //        agregarEntredaCache(content2, size, urlBase, urlBase[0] ? urlBase : "/", filePath);
//                //}
//                agregarEntredaCache(content, size, urlBase, urlPath, filePath, time(NULL));                
//        }
//        closedir(dir);
//
//}

cacheEntry* buscarCache(const char* path) {
    for (int i = 0; i < cacheSize; i++) {if (!strcmp(cache[i].path, path)) return &cache[i];}
    return NULL;
}


void handlerFather(int sig) {
    signal(SIGINT,  SIG_IGN);  
    signal(SIGTERM, SIG_IGN);
    corriendo = 0;
    kill(0, SIGTERM);          // con 0 se manda a todos los procesos del grupo pero eso se incluye a el mismo por eso hay que redefinir los handler a los originales
}
void handlerHijo(int sig){
        corriendo =0;
}



void maximizarFDS()
{
        struct rlimit limit;
        getrlimit(RLIMIT_NOFILE, &limit);
        limit.rlim_cur = limit.rlim_max;
        if(setrlimit(RLIMIT_NOFILE, &limit) != 0) perror("FDlimite");
}



void error404(int clientFD)
{
        char *body = "<h1>404 Not Found</h1>";
        char respuesta[512];
        snprintf(respuesta, sizeof(respuesta),
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %zu\r\n"
            "\r\n"
            "%s",
            strlen(body), body);
        write(clientFD, respuesta, strlen(respuesta));
}
int agregarCliente(int serverFD, int epollFD){
        int clientFD = accept(serverFD, NULL, NULL); if(clientFD <0) return -1;

        if (clientFD >= MAX_FDS || numClientesActivos >= MAX_FDS) {close(clientFD);return -1;}
        
        ultimaActividad[clientFD] = time(NULL);
        clientesActivos[numClientesActivos++] = clientFD;

        struct epoll_event event;
        event.events = EPOLLIN;
        event.data.fd = clientFD;
        epoll_ctl(epollFD, EPOLL_CTL_ADD, clientFD, &event);   
        return 1;
}
void eliminarCliente(int clientFD, int epollFD){
        epoll_ctl(epollFD, EPOLL_CTL_DEL, clientFD, NULL);
        close(clientFD); 
        ultimaActividad[clientFD] = 0;

        for (int i = 0; i < numClientesActivos; i++) {
            if (clientesActivos[i] == clientFD) {clientesActivos[i] = clientesActivos[--numClientesActivos];break;}
        }
}

void redirect(int clientFD, char* path, int shouldClose){
        char redirect[512];
        snprintf(redirect, sizeof(redirect),
                "HTTP/1.1 301 Moved Permanently\r\n"
                "Location: %s\r\n"
                "Content-Length: 0\r\n"
                "Connection: %s\r\n"
                "\r\n",
                path, shouldClose ? "close" : "keep-alive");
        write(clientFD, redirect, strlen(redirect));
        return;
}

time_t manejarPath(char* path, size_t* pathLen, int epollFD, int shouldClose, int clientFD){
        
        char* queryStart = strchr(path, '?');if (queryStart) *queryStart = '\0'; //limpia el path a partir de ? que sirve para logins y esas cosas
        
        // construir el path completo
        char ruta[MAX_PATH_LENGTH];
        snprintf(ruta, sizeof(ruta), "./public%s", path);
        
        if (realpath(ruta, path) == NULL) {goto error;};  // no existe o error

        // verificar que resuelto empiece con el path a public
        if(strncmp(path, pathAPublic, lengthPathAPublic)){goto error;}
        
        struct stat info;
        if(stat(path, &info)!=0){goto error;} 
        
        const char* relativo = path + lengthPathAPublic;
        strncpy(path, relativo, MAX_CACHE_PATH_LENGTH-1);
        *pathLen = strlen(path);
       

        if(S_ISDIR(info.st_mode)){
                printf(";");
                if((path[*pathLen-1] != '/')&&(*pathLen +1< MAX_CACHE_PATH_LENGTH)){ 
                        printf("%s\n",path);
                        path[*pathLen] = '/';
                        path[*pathLen+1] = '\0';
                        redirect(clientFD, path,shouldClose);
                        if(shouldClose){eliminarCliente(clientFD, epollFD);}
                        *pathLen = 0;
                        return -1;                
                }
                if((*pathLen + strlen("index.html") )<= MAX_CACHE_PATH_LENGTH){
                        printf(".");
                        snprintf(path + *pathLen, MAX_CACHE_PATH_LENGTH - *pathLen, "index.html");
                        *pathLen += strlen("index.html") ;
                        
                        snprintf(ruta, sizeof(ruta), "%s%s", pathAPublic, path);
                        struct stat infoIndex;
                        if (stat(ruta, &infoIndex) != 0) goto error;
                        return infoIndex.st_mtime;
                }
                goto error;
        }
        return info.st_mtime;
error:
        error404(clientFD);
        if(shouldClose){eliminarCliente(clientFD, epollFD);};
        *pathLen = 0;
        return -1;
}
void responderCliente(char* buffer, int bufferSize, int clientFD, int epollFD){
        memset(buffer,0,bufferSize);
        int bytesLeidos = read(clientFD, buffer, bufferSize);if(bytesLeidos <=0){eliminarCliente(clientFD, epollFD);return;}

        ultimaActividad[clientFD] = time(NULL);

        char method[8];char path[MAX_PATH_LENGTH];char version[16];
        sscanf(buffer, "%7s %255s %15s", method, path, version);

        int shouldClose = strstr(buffer, "Connection: close") != NULL;
        size_t pathLen;
        
        time_t ultimaVezModificado = manejarPath(path, &pathLen, epollFD, shouldClose, clientFD);if(!pathLen){return;}
        
        cacheEntry* entry = buscarCache(path); 
        if(!entry||(ultimaVezModificado > entry->ultimaVezModificado)){
                //aca no casheria losarchivos grandes pero voy a poner la cache lru
                long size;
                char* content = leerArchivo(path,&size);if(!content){error404(clientFD);if(shouldClose){eliminarCliente(clientFD, epollFD);} return;}
                entry = agregarEntradaCache(path, content, size, ultimaVezModificado);
        }

        char header[256];
        snprintf(header, sizeof(header),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: %s\r\n"
                "Content-Length: %ld\r\n"
                "Connection: %s\r\n"
                "\r\n",
                entry->contenType,
                entry->size,
                shouldClose ? "close" : "keep-alive");
        write(clientFD, header, strlen(header));
        write(clientFD, entry->content, entry->size);

        if(shouldClose){eliminarCliente(clientFD, epollFD);}
        return;     
}

void rutinaHijo(int serverFD){
        
        signal(SIGINT,  handlerHijo);
        signal(SIGTERM, handlerHijo);
        
        char buffer[4096];

        int epollFD = epoll_create1(0); if(epollFD< 0){perror("epoll"); return;}

        struct epoll_event event;
        event.events = EPOLLIN;
        event.data.fd = serverFD;
        epoll_ctl(epollFD, EPOLL_CTL_ADD, serverFD, &event);
        
        struct epoll_event events[MAX_EVENTS];

        while (corriendo) {
                int n = epoll_wait(epollFD, events, MAX_EVENTS, 1000); if(n<0){if(errno == EINTR) break; perror("epoll wait"); break;}
                for(int i = 0; i<n; i ++){
                        int fd = events[i].data.fd;
                        if (fd == serverFD){if(agregarCliente(serverFD, epollFD)<=0){perror("accept");}}
                        else{responderCliente(buffer, sizeof(buffer),fd, epollFD);}
                }
                
                time_t ahora = time(NULL);
                for (int i = numClientesActivos - 1; i >= 0; i--) {
                    int fd = clientesActivos[i];
                    if (ahora - ultimaActividad[fd] > TIMEOUT_SEGUNDOS) {eliminarCliente(fd, epollFD);}
                }
        }
        close(epollFD);
}
int main()
{
        signal(SIGINT,  handlerFather);
        signal(SIGTERM, handlerFather);
        maximizarFDS();

        if (realpath("./public", pathAPublic) == NULL) return 0;
        lengthPathAPublic = strlen(pathAPublic);

        //llenarCache("./public", "");

        int serverFD = socket(AF_INET6, SOCK_STREAM, 0); if (serverFD < 0) {perror("socket"); return 1;}
        
        int opt = 1;
        setsockopt(serverFD, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        int no = 0;
        setsockopt(serverFD, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no));

        struct sockaddr_in6 addr = {
                .sin6_family = AF_INET6,
                .sin6_addr = in6addr_any,
                .sin6_port = htons(4080),
        };

        if (bind(serverFD,(struct sockaddr*) &addr, sizeof(addr)) < 0){perror("bind");return 1;}

        listen(serverFD,128);

        int cores = sysconf(_SC_NPROCESSORS_ONLN);
        
        for(int i =0;i<cores; ++i){
                pid_t pid = fork(); if(pid<0){perror("fork");continue;}
                if(pid == 0) {rutinaHijo(serverFD); return 0;}        
        }
        while(corriendo){
                wait(NULL);
                if(!corriendo) break;
                pid_t pid = fork(); if(pid<0){perror("fork");continue;}
                if(pid == 0) {rutinaHijo(serverFD); return 0;}

        }
        
        close(serverFD);
        return 0;
        
}
