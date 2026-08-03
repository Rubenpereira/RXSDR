#pragma once
#include <string>
#include <functional>
#include <map>
#include <thread>
#include <cstdint>

using HttpSocket = uintptr_t;

namespace masdr {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
    std::map<std::string,std::string> headers;
};

struct HttpResponse {
    int         status      = 200;
    std::string contentType = "application/json";
    std::string body;
};

class RestApi;

class HttpServer {
public:
    HttpServer();
    ~HttpServer();

    bool listen(uint16_t port);
    void stop();
    uint16_t port() const { return port_; }
    void setRestApi(RestApi* api) { api_ = api; }

private:
    void acceptLoop();
    void handleClient(HttpSocket sock);
    HttpResponse dispatchRequest(const HttpRequest& req);
    HttpResponse serveFile(const std::string& urlPath);

    HttpSocket serverSock_ = (HttpSocket)(~0ULL);
    uint16_t   port_       = 0;
    bool       running_    = false;
    RestApi*   api_        = nullptr;

    std::thread acceptThread_;
    std::string webRoot_;
};

} // namespace masdr
