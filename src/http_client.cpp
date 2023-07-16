#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include "boost/certify/https_verification.hpp"

#include "asio_context_provider.hpp"
#include "http_client.hpp"
#include "zlogger.hpp"

namespace zclient {

struct http_client::impl {
    impl()
        :ssl_ctx_{boost::asio::ssl::context::sslv23_client}
    {
        ssl_ctx_.set_verify_mode(boost::asio::ssl::verify_peer | boost::asio::ssl::verify_fail_if_no_peer_cert);
        ssl_ctx_.set_default_verify_paths();

        boost::certify::enable_native_https_server_verification(ssl_ctx_);
    }

    ~impl()
    {}

    using executor_with_default = boost::asio::use_awaitable_t<>::executor_with_default<boost::asio::any_io_executor>;
    using tcp_stream = typename boost::beast::tcp_stream::rebind_executor<executor_with_default>::other;

    boost::asio::awaitable<http_response>
    fetch(
        const std::string& host,
        const std::string& port,
        const http_request& request,
        bool use_ssl
    )
    {
        if (use_ssl) {
            LOG_TRACE << "fetch_http_ssl for: " << host << ":" << port;
            auto resp = co_await fetch_http_ssl(host, port, request);
            co_return resp;
        } else {
            LOG_TRACE << "fetch_http for: " << host << ":" << port;
            auto resp = co_await fetch_http(host, port, request);
            co_return resp;
        }
    }

private:
    boost::asio::ssl::context ssl_ctx_;

    inline
    boost::beast::http::request<boost::beast::http::string_body>
    translate_http_request(
        const std::string& host,
        const http_request& request
    )
    {
        // Set up an HTTP request message
        boost::beast::http::request<boost::beast::http::string_body> req;

        req.version(HTTP_VERSION);

        switch (request.method) {
        case http_method::get:
            req.method(boost::beast::http::verb::get);
            break;
        case http_method::post:
            req.method(boost::beast::http::verb::post);
            break;
        case http_method::delete_:
            req.method(boost::beast::http::verb::delete_);
            break;
        case http_method::put:
            req.method(boost::beast::http::verb::put);
            break;
        default: abort();
        }

        req.target(request.path);
        req.set(boost::beast::http::field::host, host);
        req.set(boost::beast::http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        for (const auto& header_field : request.header_data) {
            req.set(header_field.first, header_field.second);
        }

        req.body() = request.body;
        req.prepare_payload();

        return req;
    }

    boost::asio::awaitable<http_response>
    fetch_http_ssl(
        const std::string& host,
        const std::string& port,
        const http_request& request
    )
    {
        // These objects perform our I/O
        // They use an executor with a default completion token of use_awaitable
        // This makes our code easy, but will use exceptions as the default error handling,
        // i.e. if the connection drops, we might see an exception.
        // See async_shutdown for error handling with an error_code.
        auto resolver = boost::asio::use_awaitable.as_default_on(boost::asio::ip::tcp::resolver(co_await boost::asio::this_coro::executor));
        boost::beast::ssl_stream<tcp_stream> stream{
                boost::asio::use_awaitable.as_default_on(boost::beast::tcp_stream(co_await boost::asio::this_coro::executor)),
                ssl_ctx_};

        LOG_TRACE << "Stream made";

        // Set SNI Hostname (many hosts need this to handshake successfully)
        if(! SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
            throw boost::system::system_error(static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category());

        LOG_TRACE << "SNI hostname set";

        // Look up the domain name
        boost::asio::ip::basic_resolver_results<boost::asio::ip::tcp> results;

        try {
            results = co_await resolver.async_resolve(host, port);
        } catch (std::exception& e) {
            LOG_ERROR << "Domain name resolution failed with error: " << e.what();
            throw e;
        }
        LOG_TRACE << "Domain named looked up for " << host << ":" << port;

        // Set the timeout.
        boost::beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(HTTP_TIMEOUT_SECONDS));

        // Make the connection on the IP address we get from a lookup
        try {
            co_await boost::beast::get_lowest_layer(stream).async_connect(results);
        } catch (std::exception& e) {
            LOG_ERROR << "Connection failed with error: " << e.what();
            throw e;
        }

        LOG_TRACE << "Host connected for " << host << ":" << port;

        // Set the timeout.
        boost::beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(HTTP_TIMEOUT_SECONDS));

        LOG_TRACE << "Performing SSL handshake for " << host << ":" << port;

        // Perform the SSL handshake
        try {
            co_await stream.async_handshake(boost::asio::ssl::stream_base::client);
        } catch (std::exception& e) {
            LOG_ERROR << "SSL handshake failed with error: " << e.what();
            throw e;
        }

        LOG_TRACE << "SSL handshake complete for " << host << ":" << port;

        auto req = translate_http_request(host, request);

        // Set the timeout.
        boost::beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(HTTP_TIMEOUT_SECONDS));

        // Send the HTTP request to the remote host
        co_await boost::beast::http::async_write(stream, req);

        LOG_TRACE << "Request written for " << host << ":" << port;

        // This buffer is used for reading and must be persisted
        boost::beast::flat_buffer b;

        // Declare a container to hold the response
        boost::beast::http::response<boost::beast::http::string_body> res;

        // Receive the HTTP response
        co_await boost::beast::http::async_read(stream, b, res);

        LOG_TRACE << "Response received from " << host << ":" << port;

        std::vector<std::pair<std::string,std::string>> header_data;

        const auto& header_base = res.base();
        for (const auto& header_field : header_base) {
            header_data.emplace_back(header_field.name_string(), header_field.value());
        }

        /* compose the response */
        http_response resp = {
            .return_code = static_cast<unsigned>(res.result()),
            .body = res.body(),
            .header_data = std::move(header_data)
        };
        
        LOG_TRACE << "Response composed";

        // Gracefully close the stream - do not threat every error as an exception!
        auto [ec] = co_await stream.async_shutdown(boost::asio::as_tuple(boost::asio::use_awaitable));
        if (ec == boost::asio::error::eof || ec == boost::asio::ssl::error::stream_truncated)
        {
            // Rationale:
            // http://stackoverflow.com/questions/25587403/boost-asio-ssl-async-shutdown-always-finishes-with-an-error
            ec = {};
        }
        if (ec)
            throw boost::system::system_error(ec, "shutdown");

        // If we get here then the connection is closed gracefully
        LOG_TRACE << "HTTPS connection closed for " << host << ":" << port;

        co_return resp;
    }

    boost::asio::awaitable<http_response>
    fetch_http(
        const std::string& host,
        const std::string& port,
        const http_request& request
    )
    {
        // These objects perform our I/O
        // They use an executor with a default completion token of use_awaitable
        // This makes our code easy, but will use exceptions as the default error handling,
        // i.e. if the connection drops, we might see an exception.
        auto resolver = boost::asio::use_awaitable.as_default_on(boost::asio::ip::tcp::resolver(co_await boost::asio::this_coro::executor));
        auto stream = boost::asio::use_awaitable.as_default_on(boost::beast::tcp_stream(co_await boost::asio::this_coro::executor));

        LOG_TRACE << "Looking up domain name for: " << host << ":" << port;

        // Look up the domain name
        boost::asio::ip::basic_resolver_results<boost::asio::ip::tcp> results;
        try {
            results = co_await resolver.async_resolve(host, port);
        } catch (std::exception& e) {
            LOG_ERROR << "Domain name resolution failed with error: " << e.what();
            throw e;
        }

        LOG_TRACE << "Resolved for: " << host << ":" << port;

        // Set the timeout.
        stream.expires_after(std::chrono::seconds(30));

        // Make the connection on the IP address we get from a lookup
        try {
            co_await stream.async_connect(results);
        } catch (std::exception& e) {
            LOG_ERROR << "Connection failed with error: " << e.what();
            throw e;
        }

        LOG_TRACE << "Connected to: " << host << ":" << port;

        auto req = translate_http_request(host, request);

        // Set the timeout.
        stream.expires_after(std::chrono::seconds(30));

        // Send the HTTP request to the remote host
        co_await boost::beast::http::async_write(stream, req);

        LOG_TRACE << "Request written for " << host << ":" << port;

        // This buffer is used for reading and must be persisted
        boost::beast::flat_buffer b;

        // Declare a container to hold the response
        boost::beast::http::response<boost::beast::http::string_body> res;

        // Receive the HTTP response
        co_await boost::beast::http::async_read(stream, b, res);

        LOG_TRACE << "Response received from " << host << ":" << port;

        std::vector<std::pair<std::string,std::string>> header_data;

        const auto& header_base = res.base();
        for (const auto& header_field : header_base) {
            header_data.emplace_back(header_field.name_string(), header_field.value());
        }

        /* compose the response */
        http_response resp = {
            .return_code = static_cast<unsigned>(res.result()),
            .body = res.body(),
            .header_data = std::move(header_data)
        };
        
        LOG_TRACE << "Response composed for " << host << ":" << port;

        // Gracefully close the socket
        boost::beast::error_code ec;
        stream.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);

        // not_connected happens sometimes
        // so don't bother reporting it.
        //
        if(ec && ec != boost::beast::errc::not_connected)
            throw boost::system::system_error(ec, "shutdown");

        // If we get here then the connection is closed gracefully
        LOG_TRACE << "HTTP connection closed for " << host << ":" << port;

        co_return resp;
    }
};

http_client::http_client()
    :pimpl_{std::make_unique<impl>()}
{}

http_client::~http_client() {
    pimpl_.reset();
}

boost::asio::awaitable<http_response> 
http_client::fetch(
    const std::string& host,
    const std::string& port,
    const http_request& request
)
{
    /* parse host for http prefix to decide which protocol to use
     * (http or https) */
    bool use_ssl = false;
    std::string token{"://"};

    std::size_t idx = host.find(token);
    if (idx != std::string::npos) {
        auto prefix = host.substr(0, idx);
        if (prefix == "https") {
            use_ssl = true;
        } else if (prefix == "http") {
            use_ssl = false;
        } else {
            throw std::invalid_argument("Unrecognized prefix: " + prefix);
        }
    }

    const std::string host_to_use = (idx != std::string::npos) ? host.substr(idx + token.length()) : host;
    LOG_TRACE << "Commencing fetching from host: " <<  host_to_use;

    auto resp = co_await pimpl_->fetch(
        host_to_use,
        port,
        request,
        use_ssl
    );

    co_return resp;
}

void 
http_client::fetch_then(
    const std::string& host,
    const std::string& port,
    const http_request& request,
    std::function<void(http_response&&)> callback
)
{
    boost::asio::co_spawn(
        get_io_context(), 
        [host = std::move(host),
         port = std::move(port),
         request = std::move(request),
         callback = std::move(callback)
        ]() -> boost::asio::awaitable<void> {
            http_client temp_client;
            
            auto resp = co_await temp_client.fetch(host, port, request);
            callback(std::move(resp));
    }, boost::asio::detached);
}

http_client::http_client(http_client&& other)
    :pimpl_{std::move(other.pimpl_)}
{}

http_client& http_client::operator=(http_client&& other) {
    pimpl_ = std::move(other.pimpl_);
    return *this;
}

} // ns zclient