// HttpResponse.hpp
#ifndef HTTP_RESPONSE_HPP
#define HTTP_RESPONSE_HPP
#include <string> 
#include <unordered_map>
#include <sstream>
#include <iostream>
#include <fstream>

class HttpResponse {
    std::ofstream out; 
    public:
        std::string head; //response head
        std::unordered_map<std::string, std::string> headers;
        std::string body;
        std::string firstline;
        bool isChunked = false;
        HttpResponse(std::string head) : head(head){
            std::istringstream responseStream(head);
            std::string line;
            getline(responseStream, line);
            firstline = line;
            firstline.pop_back();
            //--------------------------------------------------------------
            if (line.find("200") != std::string::npos){ //200 Ok
                while (getline(responseStream, line) && line != "\r\n") { // Stop at empty line
                    std::size_t pos = line.find(':');
                    if (pos != std::string::npos) {
                        std::string header_name = line.substr(0, pos);
                        std::string header_value = line.substr(pos + 2);
                        //std::cout << header_name << "and" << header_value << std::endl;
                        // Trim whitespace
                        // header_value.erase(header_value.begin(), std::find_if(header_value.begin(), header_value.end(), [](unsigned char ch) {
                        //     return !std::isspace(ch);
                        // }));
                        header_value.pop_back();//remove \r
                        headers[header_name] = header_value;  
                    }
                }
            }
        }

};


#endif // HTTP_RESPONSE_HPP
