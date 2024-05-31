// HttpRequest.hpp
#ifndef HTTP_REQUEST_HPP
#define HTTP_REQUEST_HPP
#include <ctime>
#include <map>
#include <iostream>
#include <cstring>
#include <sstream>

class HttpRequest {
    
    public:
        std::string head;
        int requestId;
        std::string method;
        std::string url;
        std::time_t request_time;
        std::map<std::string, std::string> headers;
        std::string body; //当为post函数时 存入body
        std::string firstline;

        HttpRequest(std::string head) : head(head){
            request_time = std::time(nullptr); // 获取当前时间
            std::istringstream request_stream(head);
            std::string line;
            std::getline(request_stream, line);
            firstline = line;
            firstline.pop_back();
            std::istringstream status_line(line);
            status_line >> method >> url;
 
            while (std::getline(request_stream, line) && line != "\r\n") {
                auto colon_pos = line.find(':');
                if (colon_pos != std::string::npos) {
                    std::string header_name = line.substr(0, colon_pos);
                    std::string header_value = line.substr(colon_pos + 2);
                    // Remove any trailing carriage return from the header value
                    header_value.pop_back();//remove \r
                    headers[header_name] = header_value;
                }else{
                    break;
                }
            }
        }

        const char* getHost(){
            return headers["Host"].c_str();
        }
};

#endif // HTTP_REQUEST_HPP
