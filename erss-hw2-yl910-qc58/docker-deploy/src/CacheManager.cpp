#include "CacheManager.hpp"
#include "logfile.hpp"

CacheBlock::CacheBlock(const std::string& head, const std::string& url, const std::string& body, 
                       const std::time_t& response_time, const std::string& cache_control,
                       const std::string& etag, const std::string& last_modified,
                       int max_age)
        : head(head), url(url), body(body), response_time(response_time),
          cache_control(cache_control), etag(etag), last_modified(last_modified),
          max_age(max_age) {};


CacheManager::CacheManager(int capacity):capacity(capacity) {};


CacheManager::~CacheManager(){
    for (auto Block: URLmap) {
        delete Block.second;
    }
}


//check whethe a response can be store in cache
bool CacheManager::cacheable_check(HttpResponse *response, int id) {
    if (response->head.find("200 OK") == std::string::npos) {
        return false;
    }
    auto it = response->headers.find("Cache-Control");
    if (it != response->headers.end()) {
        if (it->second.find("private") != std::string::npos) {
            log_file << id << ": not cacheable because it is private" << std::endl;
            return false;
        } else if (it->second.find("no-store") != std::string::npos) {
            log_file << id << ": not cacheable because it says no-store" << std::endl;
            return false;
        }
    }
    return true;
}


//sotre one cache block
void CacheManager::store(HttpResponse *response, std::string RequestURL, int id) {
    

    if (URLmap.size() >= 1000) {
        remove();  //too many cache block
    }


    std::string head = response->head;
    std::string body = response->body;
    
    std::time_t response_time = time(0);
    std::string cache_control = (response->headers.count("Cache-Control") > 0) ? response->headers["Cache-Control"]: "";
    std::string etag = (response->headers.count("ETag") > 0) ? response->headers["ETag"]: "";
    std::string last_modified = (response->headers.count("Last-Modified") > 0) ? response->headers["Last-Modified"]: "";
    int max_age = (cache_control.substr(0, 8) == "max-age=") ? std::stoi(cache_control.substr(8)): -1;
    max_age = (cache_control.substr(0, 9) == "s-maxage=") ? std::stoi(cache_control.substr(9)): max_age;
    //pthread_mutex_lock(&mutex);
    // log_file << "----------------------------------------------------" << std::endl;
    // log_file << "cache-control: " << cache_control << std::endl;
    // log_file << "e-tag: " << etag << std::endl;
    // log_file << "last_modified: " << last_modified << std::endl;
    // log_file << "max_age: " << max_age << std::endl;
    // log_file << "----------------------------------------------------" << std::endl;
    //pthread_mutex_unlock(&mutex);
    if (max_age > 0) {
        time_t expire_time = response_time + max_age;
        struct tm *local_time = localtime(&expire_time);
        char buffer[80];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", local_time);
        log_file << id << ": cached, expires at " << buffer << std::endl;
    } else {
        log_file << id << ": cached, but requires re-validation" << std::endl;
    }
    
    


    CacheBlock* onecache = new CacheBlock(head, RequestURL, body, response_time, cache_control, etag, last_modified, max_age);
    URLmap[RequestURL] = onecache;
}


//move the oldest cache block, because the cache is full
void CacheManager::remove() {
    std::time_t min;
    std::string URLtoRemove = "";
    for (auto iter = URLmap.begin(); iter != URLmap.end(); ++iter) {
        std::time_t time = iter->second->response_time;
        if (iter == URLmap.begin()) {
            min = time;
            URLtoRemove = iter->first;
        } else if (min > time) {
            min = time;
            URLtoRemove = iter->first;
        }
    }
    if (URLtoRemove != "") {
        URLmap.erase(URLtoRemove);
    }
}


//Update new data of one URL that is already in cache
void CacheManager::update(HttpResponse *response, std::string RequestURL) {

    //If there are "no-store" or "private" in "cache_control" of Http response, remove this cache block
    auto it = response->headers.find("Cache-Control");
    if (it != response->headers.end()) {
        if (it->second.find("private") != std::string::npos || it->second.find("no-store") != std::string::npos) {
            URLmap.erase(RequestURL);;
        }
    }
    URLmap[RequestURL]->body = response->body;
    URLmap[RequestURL]->head = response->head;
    URLmap[RequestURL]->response_time = time(0);
    URLmap[RequestURL]->cache_control = (response->headers.count("Cache-Control") > 0) ? response->headers["Cache-Control"]: "";
    URLmap[RequestURL]->etag = (response->headers.count("ETag") > 0) ? response->headers["ETag"]: "";
    URLmap[RequestURL]->last_modified = (response->headers.count("Last-Modified") > 0) ? response->headers["Last-Modified"]: "";
    URLmap[RequestURL]->max_age = (URLmap[RequestURL]->cache_control.substr(0, 8) == "max-age=") ? std::stoi(URLmap[RequestURL]->cache_control.substr(8)): -1;
    URLmap[RequestURL]->max_age = (URLmap[RequestURL]->cache_control.substr(0, 9) == "s-maxage=") ? std::stoi(URLmap[RequestURL]->cache_control.substr(9)): URLmap[RequestURL]->max_age;

}


//Validate if the data in cache can be send to client directly
int CacheManager::valid_check(HttpRequest *request) {
    std::string URL = request->url;
    bool req_cache_control = request->headers.count("Cache-Control") > 0;

    //The URL doesn't exist in the cache or the request says "no-store"
    if (URLmap.count(URL) == 0 ||
        (req_cache_control && request->headers["Cache-Control"].find("no-store") != std::string::npos)) {
        log_file << request->requestId << ": not in cache" << std::endl;
        return 0;
    }

    //both the Cache-Control of request and cache have no "no-cache"
    if (URLmap[URL]->cache_control.find("no-cache") == std::string::npos && !(req_cache_control && request->headers["Cache-Control"].find("no-cache") != std::string::npos)) {
        int req_max_age = -1;
        if (req_cache_control && request->headers["Cache-Control"].find("max-age") != std::string::npos) {
            req_max_age = std::stoi(request->headers["Cache-Control"].substr(9));
        }
        int max_age = -1;
        if (req_max_age != -1 && URLmap[URL]->max_age != -1) {
            max_age = req_max_age < URLmap[URL]->max_age ? req_max_age : URLmap[URL]->max_age;
        } else if (req_max_age == -1) {
            max_age = URLmap[URL]->max_age;
        } else {
            max_age = req_max_age;
        }
        
        //need revalidate
        if (max_age == 0) {
            log_file << request->requestId << ": in cache, requires validation" << std::endl;
            return 2;
        }
        
        std::time_t age = request->request_time - URLmap[URL]->response_time;
        //pthread_mutex_lock(&mutex);
        //log_file << "Age is: " << age << std::endl;
        //pthread_mutex_unlock(&mutex);
        if (max_age != -1) {
            if (age < max_age) {
                log_file << request->requestId << ": in cache, valid" << std::endl;
                return 1; //age < max-age, fresh
            } else {
                time_t expire_time = URLmap[URL]->response_time + max_age;
                struct tm *local_time = localtime(&expire_time);
                char buffer[80];
                strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", local_time);
                log_file << request->requestId << ": in cache, but expired at" << buffer << std::endl;
                return 2; //age > max-age, need revalidate
            }
        } else { //there is no max_age, need revalidate
            //std::cout << "no max_age" << std::endl;
            log_file << request->requestId << ": in cache, requires validation" << std::endl;
            return 2;
        }
    } else {
        //There is "no-cache" in cache block, need revalidate
        log_file << request->requestId << ": in cache, requires validation" << std::endl;
        return 2;
    }
}

//return one Cacheblock
CacheBlock* CacheManager::get_Cache_block(std::string RequestURL) {
    return URLmap[RequestURL];
}