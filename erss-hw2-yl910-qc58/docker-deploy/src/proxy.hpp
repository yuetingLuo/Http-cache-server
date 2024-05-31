#ifndef PROXY_H
#define PROXY_H

#include <string> // std::string
#include <iostream> // std::cout, std::cerr
#include "CacheManager.hpp" // cacheManager class definition
#include "HttpRequest.hpp" // HttpRequest class definition
#include "HttpResponse.hpp" // HttpResponse class definition
using namespace std;

struct RequestArgs {
    int* client_fd;
    std::string ip;
};


class proxy {
    public:
        static int RequestID;
        static CacheManager *cache;
    
        // Constructor that accepts parameters for initialization
        proxy() {cache = new CacheManager(1000);}
        // proxy(int requestID, CacheManager *cacheManager) : RequestID(requestID), cache(cacheManager) {}


        static int client_socket_init(const char *hostname, const char *port); // For local connections
        static int server_socket_init(string port); // For remote server connections
        static void* process_request(void* arg); // Process requests, check and store in cache
        static HttpRequest* recv_request(int client_fd);
        static int forward_request_to_server(int server_fd, const HttpRequest* request);
        static HttpResponse* receive_response_from_server(int server_fd,int client_fd);
        static int forward_response_to_client(int client_fd, const HttpResponse * curRes);
        static void https_connect(HttpRequest *Req, int client_fd);
        
        static string visualizeNewlines(const std::string& input);
        
        // Additional utility methods can be added here
};

#endif // PROXY_H

// Proxy.cpp implementation file will include the actual logic for these methods.
