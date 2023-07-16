#ifndef HTTPS_CLIENT_HPP
#define HTTPS_CLIENT_HPP

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ssl/context.hpp>

namespace zclient {

enum class http_method {
    get,
    post,
    delete_,
    put
};

struct http_request {
    http_method method;
    std::string path;
    std::vector<std::pair<std::string,std::string>> header_data;
    std::string body;
};

struct http_response {
    unsigned return_code;
    std::string body;
    std::vector<std::pair<std::string,std::string>> header_data;
};

#define HTTP_TIMEOUT_SECONDS 30
#define HTTP_VERSION 11 /* version 1.1 */

class http_client {
public:
    http_client();
    ~http_client();

    http_client(const http_client& other) = delete;
    http_client& operator=(const http_client& other) = delete;
    
    http_client(http_client&& other);
    http_client& operator=(http_client&& other);

    boost::asio::awaitable<http_response> 
    fetch(
        /* prefix with http:// for unsecured or https:// for secured. No http prefix = unsecured */
        const std::string& host,
        const std::string& port,
        const http_request& request
    );

    /* callback-style fetch */
    void fetch_then(
        /* prefix with http:// for unsecured or https:// for secured. No http prefix = unsecured */
        const std::string& host,
        const std::string& port,
        const http_request& request,
        std::function<void(http_response&&)> callback
    );

private:
    struct impl;
    std::unique_ptr<impl> pimpl_;
};

} // ns zclient

#endif // HTTPS_CLIENT_HPP