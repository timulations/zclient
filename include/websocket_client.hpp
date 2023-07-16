#ifndef WEBSOCKET_CLIENT_HPP
#define WEBSOCKET_CLIENT_HPP

#include <string>
#include <functional>
#include <memory>
#include <boost/asio/awaitable.hpp>
#include <stdexcept>

namespace zclient {

class websocket_server_disconnected_exception : public std::exception {
public:
    websocket_server_disconnected_exception(const std::string& message) : message_{message} {}
    const char* what() const noexcept override {
        return message_.c_str();
    }

private:
    std::string message_;
};


class websocket_client {
public:
    websocket_client();
    ~websocket_client();

    websocket_client(const websocket_client& other) = delete;
    websocket_client& operator=(const websocket_client& other) = delete;

    websocket_client(websocket_client&& other);
    websocket_client& operator=(websocket_client&& other);

    /* returns true on successful connection */
    boost::asio::awaitable<bool> connect(
        const std::string& host,
        const std::string& port,
        const std::string& target
    );

    bool is_connected() const;

    /* throws websocket_server_disconnected_exception if attempted while the 
     * websocket client is not connected to any server. This can also happen
     * if the server disconnects/ends the session. */
    boost::asio::awaitable<std::string> read();
    boost::asio::awaitable<void> write(const std::string& message);

    void disconnect();

private:
    struct impl;
    std::unique_ptr<impl> pimpl_;
};

} // ns zclient

#endif 