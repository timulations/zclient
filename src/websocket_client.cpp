#include <boost/asio/co_spawn.hpp>
#include <boost/asio/experimental/as_tuple.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/asio/detached.hpp>

#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include "boost/certify/https_verification.hpp"

#include "asio_context_provider.hpp"
#include "websocket_client.hpp"
#include "zlogger.hpp"

namespace zclient {

struct websocket_client::impl {

    impl()
        :ssl_ctx_{boost::asio::ssl::context::tlsv12_client}
        ,p_ws_stream_var_{}
    {
        ssl_ctx_.set_verify_mode(boost::asio::ssl::verify_peer | boost::asio::ssl::verify_fail_if_no_peer_cert);
        ssl_ctx_.set_default_verify_paths();

        boost::certify::enable_native_https_server_verification(ssl_ctx_);
    }

    ~impl() {
        disconnect();
    }

    template <typename WsStreamPtr>
    boost::asio::awaitable<bool> connect_imp(
        const std::string& host,
        const std::string& port,
        const std::string& target,
        WsStreamPtr p_ws_stream,
        boost::asio::any_io_executor& ex
    )
    {
        using boost::asio::use_awaitable;
        using boost::asio::experimental::as_tuple;
        
        try
        {
            // These objects perform our I/O
            boost::asio::ip::tcp::resolver resolver(ex);

            // Look up the domain name
            auto const results = co_await resolver.async_resolve(
                host, port, use_awaitable);

            LOG_TRACE << "Domain resolved";

            // Set a timeout on the operation
            boost::beast::get_lowest_layer(*p_ws_stream).expires_after(
                std::chrono::seconds(30));

            // Make the connection on the IP address we get from a lookup
            auto ep = co_await boost::beast::get_lowest_layer(*p_ws_stream).async_connect(
                results, use_awaitable);

            LOG_TRACE << "Connected to server";

            if constexpr (std::is_same_v<WsStreamPtr, secured_ws_stream_ptr>) {
                // Set SNI Hostname (many hosts need this to handshake
                // successfully)
                if(! SSL_set_tlsext_host_name(
                    p_ws_stream->next_layer().native_handle(), host.c_str()))
                {
                    throw boost::beast::system_error(
                        static_cast<int>(::ERR_get_error()),
                        boost::asio::error::get_ssl_category());
                }
            }

            // Set a timeout on the operation
            boost::beast::get_lowest_layer(*p_ws_stream).expires_after(
                std::chrono::seconds(30));

            // Set a decorator to change the User-Agent of the handshake
            p_ws_stream->set_option(boost::beast::websocket::stream_base::decorator(
                [](boost::beast::websocket::request_type& req)
                {
                    req.set(
                        boost::beast::http::field::user_agent,
                        std::string(BOOST_BEAST_VERSION_STRING) +
                            " websocket-client-coro");
                }));

            if constexpr (std::is_same_v<WsStreamPtr, secured_ws_stream_ptr>) {
                // Perform the SSL handshake
                co_await p_ws_stream->next_layer().async_handshake(
                    boost::asio::ssl::stream_base::client, use_awaitable);
            }

            LOG_TRACE << "SSL handshake success";

            // Turn off the timeout on the tcp_stream, because
            // the websocket stream has its own timeout system.
            boost::beast::get_lowest_layer(*p_ws_stream).expires_never();

            // Set suggested timeout settings for the websocket
            p_ws_stream->set_option(boost::beast::websocket::stream_base::timeout::suggested(
                boost::beast::role_type::client));

            // Perform the websocket handshake
            co_await p_ws_stream->async_handshake(host + ':' + port, target, use_awaitable);

            LOG_TRACE << "Websocket handshake success";

            co_return true;
            
        } catch (boost::beast::system_error const& se) {
            std::cerr << "Connection failed due to: " << se.what() << std::endl;
            co_return false;
        }
    }

    boost::asio::awaitable<bool> connect(
        const std::string& host,
        const std::string& port,
        const std::string& target,
        bool use_ssl
    )
    {
        auto ex = co_await boost::asio::this_coro::executor;

        if (use_ssl) {
            p_ws_stream_var_ = std::make_shared<secured_ws_stream>(ex, ssl_ctx_);

            auto res = co_await connect_imp(
                host,
                port,
                target,
                std::get<secured_ws_stream_ptr>(p_ws_stream_var_),
                ex
            );

            co_return res;
        } else {
            p_ws_stream_var_ = std::make_shared<unsecured_ws_stream>(ex);

            auto res = co_await connect_imp(
                host,
                port,
                target,
                std::get<unsecured_ws_stream_ptr>(p_ws_stream_var_),
                ex
            );

            co_return res;
        }
    }

    template <typename WsStreamPtr>
    boost::asio::awaitable<std::string> read_imp(WsStreamPtr p_ws_stream) {

        if (!is_connected()) {
            throw websocket_server_disconnected_exception("Connection is not open");
        }

        using boost::asio::use_awaitable;
        using boost::asio::experimental::as_tuple;

        // This buffer will hold the incoming message
        boost::beast::flat_buffer buffer;

        // Read a message into our buffer
        auto [ec, bytes] =
            co_await p_ws_stream->async_read(buffer, as_tuple(use_awaitable));

        std::string ret = std::string(boost::beast::buffers_to_string(buffer.data()));
        buffer.consume(buffer.size());

        LOG_TRACE << "Read " << ret.length() << " characters from server";

        if(ec)
        {
            // eof is to be expected for some services
            if(ec != boost::asio::error::eof)
                throw boost::beast::system_error(ec);
        }

        co_return ret;
    }

    boost::asio::awaitable<std::string> read() {
        if (std::holds_alternative<secured_ws_stream_ptr>(p_ws_stream_var_)) {
            auto res = co_await read_imp(std::get<secured_ws_stream_ptr>(p_ws_stream_var_));
            co_return res;
        } else {
            auto res = co_await read_imp(std::get<unsecured_ws_stream_ptr>(p_ws_stream_var_));
            co_return res;   
        }
    }

    template <typename WsStreamPtr>
    boost::asio::awaitable<void> write_imp(WsStreamPtr p_ws_stream, const std::string& message) {

        if (!is_connected()) {
            throw websocket_server_disconnected_exception("Connection is not open");
        }

        using boost::asio::use_awaitable;
        using boost::asio::experimental::as_tuple;

        co_await p_ws_stream->async_write(boost::asio::buffer(message), as_tuple(use_awaitable));
        LOG_TRACE << "Wrote " << message.length() << " characters to server";
    }

    boost::asio::awaitable<void> write(const std::string& message) {
        if (std::holds_alternative<secured_ws_stream_ptr>(p_ws_stream_var_)) {
            co_await write_imp(std::get<secured_ws_stream_ptr>(p_ws_stream_var_), message);
        } else {
            co_await write_imp(std::get<unsecured_ws_stream_ptr>(p_ws_stream_var_), message);
        }
    }

    bool is_connected() const {
        if (std::holds_alternative<secured_ws_stream_ptr>(p_ws_stream_var_)) {
            return std::get<secured_ws_stream_ptr>(p_ws_stream_var_)->is_open();
        } else {
            return std::get<unsecured_ws_stream_ptr>(p_ws_stream_var_)->is_open();
        }
    }

    template <typename WsStreamPtr>
    void disconnect_imp(WsStreamPtr p_ws_stream) {
        if (p_ws_stream) {
            try {
                p_ws_stream->close(boost::beast::websocket::close_code::normal);
            } catch (boost::wrapexcept<boost::system::system_error> const& se) {
                const auto& e_code = se.code();
                if (e_code != boost::asio::ssl::error::stream_truncated) {
                    throw se;
                }
            }
        

            LOG_TRACE << "Successfully disconnected";
        }
    }

    void disconnect() {
        if (is_connected()) {
            if (std::holds_alternative<secured_ws_stream_ptr>(p_ws_stream_var_)) {
                disconnect_imp(std::get<secured_ws_stream_ptr>(p_ws_stream_var_));
            } else {
                disconnect_imp(std::get<unsecured_ws_stream_ptr>(p_ws_stream_var_));
            }
        }
    }    


private:
    boost::asio::ssl::context ssl_ctx_;

    using secured_ws_stream = boost::beast::websocket::stream<
                                  boost::beast::ssl_stream<boost::beast::tcp_stream>
                              >;
    using secured_ws_stream_ptr = std::shared_ptr<secured_ws_stream>;

    using unsecured_ws_stream = boost::beast::websocket::stream<boost::beast::tcp_stream>;
    using unsecured_ws_stream_ptr = std::shared_ptr<unsecured_ws_stream>;

    std::variant<secured_ws_stream_ptr, unsecured_ws_stream_ptr> p_ws_stream_var_;
};

websocket_client::websocket_client()
    :pimpl_{std::make_unique<impl>()}
{}


websocket_client::~websocket_client() {
    pimpl_.reset();
}

boost::asio::awaitable<bool> websocket_client::connect(
    const std::string& host,
    const std::string& port,
    const std::string& target
)
{
    /* parse host for http prefix to decide which protocol to use
     * (ws or wss) */
    bool use_ssl = false;
    std::string token{"://"};

    std::size_t idx = host.find(token);
    if (idx != std::string::npos) {
        auto prefix = host.substr(0, idx);
        if (prefix == "wss") {
            use_ssl = true;
        } else if (prefix == "ws") {
            use_ssl = false;
        } else {
            throw std::invalid_argument("Unrecognized prefix: " + prefix);
        }
    }

    const std::string host_to_use = (idx != std::string::npos) ? host.substr(idx + token.length()) : host;

    auto resp = co_await pimpl_->connect(host_to_use, port, target, use_ssl);
    co_return resp;
}

bool websocket_client::is_connected() const {
    return pimpl_->is_connected();
}

boost::asio::awaitable<std::string> websocket_client::read() {
    auto res = co_await pimpl_->read();
    co_return res;
}

boost::asio::awaitable<void> websocket_client::write(const std::string& message) {
    co_await pimpl_->write(message);
}

void websocket_client::disconnect() {
    return pimpl_->disconnect();
}

websocket_client::websocket_client(websocket_client&& other)
    :pimpl_{std::move(other.pimpl_)}
{}

websocket_client& websocket_client::operator=(websocket_client&& other) {
    pimpl_ = std::move(other.pimpl_);
    return *this;
}

} // ns zclient