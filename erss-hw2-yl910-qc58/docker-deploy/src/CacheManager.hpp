#ifndef CACHE_MANAGER_H
#define CACHE_MANAGER_H

#include <unordered_map>
#include <map>
#include <ctime>
#include <sstream>
#include <iostream>
#include <string>
#include "HttpResponse.hpp"
#include "HttpRequest.hpp"

class CacheBlock{
    public:
        std::string head; //response head
        std::string url; // Request URL
        std::string body; // body of response
        std::time_t response_time; 
        std::string cache_control; 
        std::string etag; 
        std::string last_modified; 
        int max_age;

        CacheBlock(const std::string& head, const std::string& url, const std::string& body, 
                   const std::time_t& response_time, const std::string& cache_control = "",
                   const std::string& etag = "", const std::string& last_modified = "",
                   int max_age = -1);
};

class CacheManager {
    public:
        CacheManager();
        CacheManager(int capacity);
        ~CacheManager();

        bool cacheable_check(HttpResponse *response, int id); //check whethe a response can be store in cache
        void store(HttpResponse *response, std::string RequestURL, int id); //sotre one cache block
        void update(HttpResponse *response, std::string RequestURL); //Update new data of one URL that is already in cache
        void remove(); //move the oldest cache block, because the cache is full

        // 0 there is no such cache or it request "no-store"
        // 1 cache is fresh
        // 2 cache need revalidate
        int valid_check(HttpRequest *request); ///Validate if the data in cache can be send to client directly

        CacheBlock* get_Cache_block(std::string RequestURL); //return one Cacheblock


    private:
        int capacity = 1000;
        std::unordered_map<std::string, CacheBlock*> URLmap; //Use URL to search cache block


    
    // CacheManager(const CacheManager&) = delete;
    // CacheManager& operator=(const CacheManager&) = delete;
};

#endif

