#ifndef ASIO_CONTEXT_PROVIDER_HPP
#define ASIO_CONTEXT_PROVIDER_HPP

#include <boost/asio/io_context.hpp>

namespace zclient {
/* global singleton for io_context access  */
class asio_context_provider {
public:
    static asio_context_provider& get_instance() {
        static asio_context_provider instance;
        return instance;
    }

    boost::asio::io_context& get_io_context() {
        return io_context_;
    }
private:
    asio_context_provider() = default;
    ~asio_context_provider() = default;

    boost::asio::io_context io_context_;
};

inline boost::asio::io_context& get_io_context() {
    return asio_context_provider::get_instance().get_io_context();
}

} // ns zclient

#endif // ASIO_CONTEXT_PROVIDER_HPP