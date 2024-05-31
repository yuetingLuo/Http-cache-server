#include "proxy.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <iostream>
#include <fstream>
#include <thread>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <sys/stat.h>
#include <signal.h>
#include <ctime>
#include <chrono>
#include <iomanip>
#include <arpa/inet.h>
#include "CacheManager.hpp"
#include "logfile.hpp"

//Global variables
int RequestID = 0;
CacheManager* proxy::cache = nullptr;
std::ofstream log_file("/var/log/erss/proxy.log");
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int proxy:: client_socket_init(const char *hostname, const char *port) {
    struct addrinfo hints, *res, *p;
    int server_fd;
    int yes=1;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; 
    hints.ai_socktype = SOCK_STREAM; // TCP连接
    hints.ai_flags = AI_CANONNAME;
    
    std::string a = hostname;
    int rv = getaddrinfo(hostname, port, &hints, &res);

    if (rv != 0) {
        time_t now = time(0);
        tm *ltm = localtime(&now);
        std::cerr <<ltm->tm_hour << ":"<<ltm->tm_min<<":"<< ltm->tm_sec << " getaddrinfo error: " << gai_strerror(rv) << std::endl;
        std::cerr<<"hostname: "<<visualizeNewlines(a) << std::endl;
        std::cerr << gai_strerror(rv) << std::endl;
        return -1;
    }

    // loop through all the results and bind to the first we can
    for(p = res; p != NULL; p = p->ai_next) {
        if ((server_fd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (connect(server_fd, res->ai_addr, res->ai_addrlen) < 0) {
            perror("Failed to connect to server");
            close(server_fd);
            return -1;
        }
        break;
    }

    if (server_fd == -1) {
        perror("Failed to create socket for server connection");
        return -1;
    }

    if (p == NULL)  {
        perror("server: failed to bind\n");
        exit(1);
    }
    freeaddrinfo(res);
    return server_fd; 
}

int proxy:: server_socket_init(string port) {
    int proxy_fd;
    if((proxy_fd = socket(PF_INET, SOCK_STREAM, 0)) == -1){
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in proxy_addr;
    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_addr.s_addr = INADDR_ANY;//for add ips  
    proxy_addr.sin_port = htons(stoi(port)); 

    if(bind(proxy_fd, (struct sockaddr *)& proxy_addr, sizeof(proxy_addr)) == -1){
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if(listen(proxy_fd, 10) < 0){ //backlog作为连接请求等待队列的长度，多线程时怎么处理？
        perror("listen");
        exit(EXIT_FAILURE);
    }

    return proxy_fd;
}

// 将字符串中的 \r 和 \n 字符转换为可视化的字符串
std::string proxy::visualizeNewlines(const std::string& input) {
    std::string result;
    for (char c : input) {
        switch (c) {
            case '\r':
                result += "\\r";
                break;
            case '\n':
                result += "\\n";
                break;
            default:
                result += c;
        }
    }
    return result;
}


HttpRequest* proxy:: recv_request(int client_fd){
    char buffer[4096];
    const int buffer_size = 4096;
    std::string request_head;
    std::string request_body;
    int bytes_received;
    int body_len = 0;
    HttpRequest* request;

    while (1) {
        bytes_received = recv(client_fd, buffer, sizeof(buffer)-1, 0);
        if(bytes_received < 0){
            perror("receive response from client error");
        }
        if(bytes_received == 0){//server colse the connection
            close(client_fd);
            break;
        }
        // buffer[bytes_received] = '\0';
        request_head.append(buffer, bytes_received);
        size_t header_end = request_head.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            request_body = request_head.substr(header_end+4);
            request_head = request_head.substr(0, header_end+2);
            break; 
        }
    }


    request = new HttpRequest(request_head);
    auto it = request->headers.find("Content-Length");
    if (it != request->headers.end()) {
        // Content-Length found, retrieve its value and convert to integer
        int remaining_length = body_len - request_body.length();
        while (remaining_length > 0 && (bytes_received = recv(client_fd, buffer, std::min(buffer_size, remaining_length), 0)) > 0) {
            request_body.append(buffer, bytes_received);
            remaining_length -= bytes_received;
        }
        request->body = request_body;
    }  
    return request;
}


int proxy:: forward_request_to_server(int server_fd, const HttpRequest* request) {

    std::string request_data = request->head + "\r\n" + request->body;
    const char *send_req = request_data.c_str();
    int send_len = strlen(send_req);

    while(send_len > 0){
        int send_to = send(server_fd, send_req, send_len, 0);
        send_req += send_to;
        send_len -= send_to;
    }

    return 0;
}


void* proxy:: process_request(void* arg) {
    RequestArgs* args = static_cast<RequestArgs*>(arg);
    int client_fd = *args->client_fd;
    std::string ip = args->ip;
    delete args; 

    // int client_fd = *(int*)arg;
    // std::string ip;
    // free(arg); // 释放之前分配的内存

    // pthread_mutex_lock(&mutex);
    // log_file <<"process_Request"<<std::endl;
    // pthread_mutex_unlock(&mutex);
    HttpRequest *curReq = recv_request(client_fd);
    RequestID ++;
    curReq->requestId = RequestID;

    std::time_t current_time = std::time(0);
    std::tm* time_now = std::gmtime(&current_time);
    const char* curr_time =  std::asctime(time_now);

    pthread_mutex_lock(&mutex);
    log_file << curReq->requestId << ": " << curReq->firstline << " from " << ip << " @ " << curr_time;
    pthread_mutex_unlock(&mutex);

    const char* host = curReq->getHost();
    const char* port = "80";//服务器的端口号设置
//    std::string a = host;
//    std::cout<<"hostname: "<<visualizeNewlines(a) << std::endl;;

    if(curReq->method == "GET"){
        int server_fd = client_socket_init(host, port);
        if(server_fd < 0){
            std::string msg400("HTTP/1.1 400 Bad Request\r\n\r\n");
            send(client_fd, msg400.c_str(), msg400.length(), 0);
            pthread_mutex_lock(&mutex);
            log_file << curReq->requestId << ": WARNING 400 Bad Request" << std::endl;
            pthread_mutex_unlock(&mutex);
            perror("Fail connect server.");
        }
//        std::cout<<"Get method" << std::endl;
        pthread_mutex_lock(&mutex);
        int cache_check = cache->valid_check(curReq);
        //log_file << "Cache Check" << cache_check << std::endl;
        pthread_mutex_unlock(&mutex);
        if(cache_check == 0) { //不存在这个缓存或禁止使用缓存
            pthread_mutex_lock(&mutex);
            log_file << curReq->requestId << ": Requesting " <<curReq->firstline << " from " << host << std::endl;
            pthread_mutex_unlock(&mutex);
            forward_request_to_server(server_fd, curReq);

            HttpResponse * curRes = receive_response_from_server(server_fd, client_fd);
            pthread_mutex_lock(&mutex);
            log_file << curReq->requestId << ": Received " <<curRes->firstline << " from " << host << std::endl;
            pthread_mutex_unlock(&mutex);

            std::string status = curRes->head.substr(curRes->head.find(" ") + 1, 3);
            if (status == "200") {
                // Response is cacheable, store it in cache
                pthread_mutex_lock(&mutex);
                if (cache->cacheable_check(curRes, curReq->requestId)) {
                    cache->store(curRes, curReq->url, curReq->requestId);
                    //log_file << "Cache Stored" << std::endl;
                } 
                pthread_mutex_unlock(&mutex);

                if(forward_response_to_client(client_fd, curRes) < 0) {
                    perror("response error");
                }
                pthread_mutex_lock(&mutex);
                log_file << curReq->requestId << ": Responding " <<curRes->firstline << std::endl;
                pthread_mutex_unlock(&mutex);
            } else {  //错误的情况
                pthread_mutex_lock(&mutex);
                log_file << "ERROR STATE CODE" << status;
                std::string msg = curRes->head.substr(0, curRes->head.find("\r\n"));
                if (status[0] == '4') {
                    log_file << ": ERROR " << msg << std::endl;
                }
                else if (status[0] == '5') {
                    log_file <<  ": ERROR " << msg << std::endl;
                }
                else if (status[0] == '3') {
                    log_file <<  ": WARNING " << msg << std::endl;
                }
                pthread_mutex_unlock(&mutex);
            }

        }else if (cache_check == 1) { //数据在缓存中且是新鲜的，直接返回给client
            pthread_mutex_lock(&mutex);
            CacheBlock* CacheRes = cache->get_Cache_block(curReq->url);
            pthread_mutex_unlock(&mutex);
            std::string head = CacheRes->head;

            //Change Date to current time
            auto now = std::chrono::system_clock::now();
            auto now_c = std::chrono::system_clock::to_time_t(now);
            std::tm now_tm = *std::gmtime(&now_c);
            std::ostringstream oss;
            oss << std::put_time(&now_tm, "Date: %a, %d %b %Y %H:%M:%S GMT");
            std::size_t datePos = head.find("Date:");
            if (datePos != std::string::npos) {
                std::size_t endLinePos = head.find("\r", datePos);
                head.replace(datePos, endLinePos - datePos, oss.str());
            }

            HttpResponse * curRes = new HttpResponse(head);
            curRes->body = CacheRes->body;
            // pthread_mutex_lock(&mutex);
            // log_file << "Send Cache to Client" << std::endl;
            // pthread_mutex_unlock(&mutex);
            if(forward_response_to_client(client_fd, curRes) < 0) {
                perror("response error");
            }
            pthread_mutex_lock(&mutex);
            log_file << curReq->requestId << ": Responding " <<curRes->firstline << std::endl;
            pthread_mutex_unlock(&mutex);
        } else { //数据在缓存中，但需要重新验证
            CacheBlock* onecache = cache->get_Cache_block(curReq->url);
            std::string match = "";
            if (onecache->etag != "") {
                match = "If-None-Match: " + onecache->etag + "\r\n";
            }
            std::string modified = "";
            if (onecache->last_modified != "") {
                modified = "If-Modified-Since: " + onecache->last_modified + "\r\n";
            }
            //生成带Etag和Last-Modified的新request
            std::size_t insertPos = curReq->head.find("\r\n\r\n") + 2;
            std::string newReqdata = curReq->head;
            newReqdata.insert(insertPos, match + modified);
            HttpRequest* newReq = new HttpRequest(newReqdata);

            pthread_mutex_lock(&mutex);
            log_file << curReq->requestId << ": Requesting " <<curReq->firstline << " from " << host << std::endl;
            pthread_mutex_unlock(&mutex);
            forward_request_to_server(server_fd, newReq);

            HttpResponse * curRes = receive_response_from_server(server_fd,client_fd);
            pthread_mutex_lock(&mutex);
            log_file << curReq->requestId << ": Received " <<curRes->firstline << " from " << host << std::endl;
            pthread_mutex_unlock(&mutex);

            std::string status = curRes->head.substr(curRes->head.find(" ") + 1, 3);
//            std::cout<<"status" << status << std::endl;
            if (status == "200") { //缓存需要被更新
                cache->update(curRes, newReq->url);
                if(forward_response_to_client(client_fd, curRes) < 0) {
                    perror("response error");
                }
                pthread_mutex_lock(&mutex);
                log_file << curReq->requestId << ": Responding " <<curRes->firstline << std::endl;
                pthread_mutex_unlock(&mutex);
            } else if (status == "304") {//缓存未被修改，直接返回给client
                CacheBlock* CacheRes = cache->get_Cache_block(curReq->url);
                std::string head = CacheRes->head;

                //Change Date to current time
                auto now = std::chrono::system_clock::now();
                auto now_c = std::chrono::system_clock::to_time_t(now);
                std::tm now_tm = *std::gmtime(&now_c);
                std::ostringstream oss;
                oss << std::put_time(&now_tm, "Date: %a, %d %b %Y %H:%M:%S GMT");
                std::size_t datePos = head.find("Date:");
                if (datePos != std::string::npos) {
                    std::size_t endLinePos = head.find("\r", datePos);
                    head.replace(datePos, endLinePos - datePos, oss.str());
                }

                HttpResponse * curRes = new HttpResponse(head);
                curRes->body = CacheRes->body;
//                std::cout << "Send Cache to Client" << std::endl;
                if(forward_response_to_client(client_fd, curRes) < 0) {
                    perror("response error");
                }
                pthread_mutex_lock(&mutex);
                log_file << curReq->requestId << ": Responding " <<curRes->firstline << std::endl;
                pthread_mutex_unlock(&mutex);
            } else {  //错误的情况
                pthread_mutex_lock(&mutex);
                log_file << "ERROR STATE CODE" << status << std::endl;
                std::string msg = curRes->head.substr(0, curRes->head.find("\r\n"));
                if (status[0] == '4') {
                    log_file << ": ERROR " << msg << std::endl;
                }
                else if (status[0] == '5') {
                    log_file <<  ": ERROR " << msg << std::endl;
                }
                else if (status[0] == '3') {
                    log_file <<  ": WARNING " << msg << std::endl;
                }
                pthread_mutex_unlock(&mutex);
            }
        }
    }else if(curReq->method == "POST"){
        int server_fd = client_socket_init(host, port);
        if(server_fd < 0){
            perror("Fail connect server.");
        }

        pthread_mutex_lock(&mutex);
        log_file << curReq->requestId << ": Requesting " <<curReq->firstline << " from " << host << std::endl;
        pthread_mutex_unlock(&mutex);
        forward_request_to_server(server_fd, curReq);

        HttpResponse * curRes = receive_response_from_server(server_fd,client_fd);
        pthread_mutex_lock(&mutex);
        log_file << curReq->requestId << ": Received " <<curRes->firstline << " from " << host << std::endl;
        pthread_mutex_unlock(&mutex);
        if(forward_response_to_client(client_fd, curRes) < 0){
                perror("response error");
        }
        pthread_mutex_lock(&mutex);
        log_file << curReq->requestId << ": Responding " <<curRes->firstline << std::endl;
        pthread_mutex_unlock(&mutex);
    } else if (curReq->method == "CONNECT") { //connect 
        https_connect(curReq, client_fd);
    } else { //error
        std::string msg501("HTTP/1.1 501 Not Implemented\r\n\r\n");
        send(client_fd, msg501.c_str(), msg501.length(), 0);
        pthread_mutex_lock(&mutex);
        log_file << curReq->requestId << ": WARNING 501 Not Implemented" << std::endl;
        pthread_mutex_unlock(&mutex);
        //perror("Other http request, do not support in the proxy");
    }
    // pthread_mutex_lock(&mutex);
    // log_file << "========Done one Request========" << std::endl;
    // pthread_mutex_unlock(&mutex);
    close(client_fd);
    pthread_exit(NULL);
}


HttpResponse* proxy::receive_response_from_server(int server_fd, int client_fd){
//    time_t now = time(0);
//    tm *ltm = localtime(&now);
//    std::cout<<ltm->tm_hour << ":"<<ltm->tm_min<<":"<< ltm->tm_sec<<" receive_response_from_server"<<endl;
    const int buffer_size = 4096;
    char buffer[buffer_size];
    std::string response_head;
    std::string response_body;
    int bytes_received;
    HttpResponse* response;
    int body_len;

    while (1) {
        bytes_received = recv(server_fd, buffer, buffer_size, 0);
        if(bytes_received < 0){
            perror("receive response from server error");
        }
        if(bytes_received == 0){//server colse the connection
            break;
        }
        response_head.append(buffer, bytes_received);
        try {
            size_t header_end = response_head.find("\r\n\r\n");
            if (header_end != std::string::npos) {
//                std::cout<<"Found end of header of response"<<std::endl;
                // std::cout<<visualizeNewlines(response_head)<<std::endl;;
                response_body = response_head.substr(header_end+4);
                response_head = response_head.substr(0, header_end+2);

                break;
            }
        }catch (const std::exception& e) {
            std::cerr << "An exception occurred: " << e.what() << std::endl;
        }                
    }
    
    // std::cout<<"receive_response_from_server: Response header" << std::endl;
    // std::cout<< response_head<<std::endl;
    // std::cout<< visualizeNewlines(response_head)<<std::endl;
    // std::cout<<"receive_response_from_server: Response body" << std::endl;
    // std::cout<< response_body<<std::endl;

    response = new HttpResponse(response_head);
//    for (const auto& header : response->headers) {
//        std::cout << visualizeNewlines(header.first) << ": " << visualizeNewlines(header.second) << std::endl;
//    }
//    std::cout<<"response created"<<std::endl;

    if(response->headers.find("Content-Length") != response->headers.end()){
        body_len = std::stoi(response->headers["Content-Length"]);
        int remaining_length = body_len - response_body.length();

        while (remaining_length > 0 && (bytes_received = recv(server_fd, buffer, std::min(buffer_size, remaining_length), 0)) > 0) {
            response_body.append(buffer, bytes_received);
            remaining_length -= bytes_received;
        }

    }else if(response->headers.find("Transfer-Encoding") != response->headers.end() && response->headers["Transfer-Encoding"] == "chunked"){
        response->isChunked = true;
        std::string first = response_head +"\r\n"+ response_body;
        send(client_fd, first.c_str(), first.length(), 0);
        char buffer[4096] = {0};
        while (true){
            int len = recv(server_fd, buffer, sizeof(buffer), 0);
            if(len <= 0){
                break;
            }
            std::string temp(buffer, len);
            response_body+=temp;
            int send_len = send(client_fd, buffer, len, 0);
            if (send_len <= 0) {
                break;
            }
            if (response_body.find("0\r\n\r\n") != std::string::npos) {
               break; // 找到结束序列，结束循环
            }
        }
    }

    response->body = response_body;

    return response;
}


int proxy:: forward_response_to_client(int client_fd, const HttpResponse * curRes) {
//    std::cout << "Forwarding response to client." << endl;
    if(curRes->isChunked){
        return 0;
    }
    std::string response_data = curRes->head + "\r\n" + curRes->body;

    size_t response_length = response_data.length();

    //ssize_t bytes_sent = send(client_fd, response_data, response_length, 0);
    ssize_t bytes_sent = send(client_fd, response_data.c_str(), response_length, 0);
    if (bytes_sent < 0) {
        perror("Failed to send response to client");
        return -1; 
    }
    return 0; 
}


void proxy::https_connect(HttpRequest *Req, int client_fd){
    const char* host = Req->getHost();
    std::string host_string = host;
    std::string port;
    size_t pos = host_string.find(':');
    if (pos != std::string::npos) {
        port = host_string.substr(pos+1);
        host_string = host_string.substr(0, pos); // 从头截取到 ":" 之前的子串
    } else {
        port = "443"; //https 默认为443
    }
    const char* char_host = host_string.c_str();
    const char* char_port = port.c_str();
    //connect tp server
    int server_fd = client_socket_init(char_host, char_port);
    if(server_fd < 0){
        perror("Fail connect server.");
    }

    std::string OK = "HTTP/1.1 200 OK\r\n\r\n";
    if (send(client_fd, OK.c_str(), OK.length(), 0) == -1) {
        perror("Failed to send 200 OK to client");
    }

    //Start the tunnel
//    std::cout << "Tunnel start" << std::endl;
    fd_set read_fds;
    struct timeval tv;
    tv.tv_sec = 1;
    int len;

    while (1) {
        char buffer[65536];
        FD_ZERO(&read_fds);
        FD_SET(client_fd, &read_fds);
        FD_SET(server_fd, &read_fds);
        if(select(FD_SETSIZE, &read_fds, NULL, NULL, &tv) == -1){
            perror("Failed in connection");
            break;
        }
        if (FD_ISSET(client_fd, &read_fds)) {
            len = recv(client_fd, buffer, sizeof(buffer), 0);
            if (len <= 0) break;
            len = send(server_fd, buffer, len, 0);
            if (len < 0) break;
        }
        if (FD_ISSET(server_fd, &read_fds)) {
            len = recv(server_fd, buffer, sizeof(buffer), 0);
            if (len <= 0) break;
            len = send(client_fd, buffer, len, 0);
            if (len < 0) break;
        } 
    }
    close(server_fd);

    pthread_mutex_lock(&mutex);
    log_file << Req->requestId << ": Tunnel closed" << std::endl;
    pthread_mutex_unlock(&mutex);
}



int proxy::RequestID = 0;

int main(void){

    freopen("error.log", "a", stderr); // 将stderr重定向到error.log文件

    signal(SIGCHLD, SIG_IGN);
    proxy p;
    const char * port = "12345";
    int proxy_fd = p.server_socket_init(port);
    log_file << "Proxy server listening" <<std::endl;

    while(true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int* client_fd = (int*)malloc(sizeof(int));
        *client_fd = accept(proxy_fd, (struct sockaddr*)&client_addr, &client_len);
        struct sockaddr_in * addr = (struct sockaddr_in *)&client_addr;
        std::string ip = inet_ntoa(addr->sin_addr);
        if(*client_fd >= 0) {
            RequestArgs* args = new RequestArgs{client_fd, ip};
            pthread_t thrd;
            if(pthread_create(&thrd, NULL, &proxy::process_request, (void*)args) != 0) {
            //if (pthread_create(&thrd, NULL, &proxy::process_request, (void*)client_fd) != 0) {
                perror("Failed to create thread");
                //free(client_fd); // 如果线程创建失败，释放分配的内存
                delete args; // 如果线程创建失败，释放分配的内存
            } else {
                pthread_detach(thrd); // 确保线程结束后资源被回收
            }
        } else {
            free(client_fd); // 如果accept失败，释放分配的内存
            perror("Failed to accept client connection.");
        }
    }
}
