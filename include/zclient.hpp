#ifndef ZCLIENT_HPP
#define ZCLIENT_HPP

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <functional>

#include "asio_context_provider.hpp"
#include "http_client.hpp"
#include "websocket_client.hpp"

namespace zclient {

template <typename T>
using zawaitable = boost::asio::awaitable<T>;

using zasync = zawaitable<void>;

/* execute an asynchronous function */
void zasync_exec(std::function<zasync()> async_fcn) {
    boost::asio::co_spawn(get_io_context(), async_fcn, [](const std::exception_ptr& e) {
        if (e != nullptr) {
            try {
                std::rethrow_exception(e);
            }
            catch (std::exception const& ex) {
                std::cerr << "Asynchronous execution failed with: " << ex.what() << std::endl;
                throw ex;
            }
        }
    });
}

template <typename T>
void zasync_exec(std::function<zawaitable<T>()> async_fcn) {
    boost::asio::co_spawn(get_io_context(), async_fcn, [](const std::exception_ptr& e) {
        if (e != nullptr) {
            try {
                std::rethrow_exception(e);
            }
            catch (std::exception const& ex) {
                std::cerr << "Asynchronous execution failed with: " << ex.what() << std::endl;
                throw ex;
            }
        }
    });
}

#define FETCH_TIMEOUT_SECONDS HTTP_TIMEOUT_SECONDS

zawaitable<http_response> fetch(
    const std::string& host,
    const std::string& port,
    const http_request& request
)
{
    http_client http_client_;
    auto resp = co_await http_client_.fetch(host, port, request);
    co_return resp;
}

void fetch_then(
    const std::string& host,
    const std::string& port,
    const http_request& request,
    std::function<void(http_response&&)> callback
)
{
    http_client http_client_;
    http_client_.fetch_then(
        host,
        port,
        request,
        callback
    );
}


/* run all networking */
void zrun() {
    get_io_context().run();
}


/* stop all networking */
void zstop() {
    get_io_context().stop();
}

}

#endif // ZCLIENT_HPP