#include <iostream>
#include <boost/program_options.hpp>
#include <optional>
#include <thread>
#include <mutex>
#include <csignal>

#include "zclient.hpp"
#include "zlogger.hpp"

namespace po = boost::program_options;

static void fill_hostname_path_and_port(const std::string url_after_prefix, std::string& hostname, std::string& path, std::string& port, const char* prefix) {
    
    auto portval = url_after_prefix.find(":");
    auto nextslash = url_after_prefix.find("/");

    if (nextslash == std::string::npos && portval == std::string::npos) {
        hostname = url_after_prefix;
        port = "";
        path = "/";
    } else if (nextslash != std::string::npos && portval == std::string::npos) {
        hostname = url_after_prefix.substr(0, nextslash);
        path = url_after_prefix.substr(nextslash);
        port = "";
    } else if (nextslash == std::string::npos && portval != std::string::npos) {
        hostname = url_after_prefix.substr(0, portval);
        port = url_after_prefix.substr(portval + 1);
        path = "/";
    } else {
        hostname = url_after_prefix.substr(0, portval);
        port = url_after_prefix.substr(portval + 1, nextslash - portval - 1);
        path = url_after_prefix.substr(nextslash);
    }

    LOG_INFO << "Hostname = " << hostname << ", Port = " << port << ", Path = " << path;
}

enum class connection_type {
    http,
    https,
    ws,
    wss
};

void signal_handler(int signal) {
    if (signal == SIGINT) { /* ctrl + C */
        zclient::zstop();
        std::cout << "\nio_context stopped. Press Enter to quit." << std::endl;
    }
}

int main(int argc, char *argv[]) {
    
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help,h", "print this help message")
        ("url", po::value<std::string>(), "Specify the URL to request. http:// for unsecured and https:// for secured")
        ("request,X", po::value<std::string>(), "Specify the HTTP request method. Supported = [GET, POST, PUT, DELETE]")
        ("headers,H", po::value<std::vector<std::string>>()->multitoken(), "Specify the headers. Format = 'key1:value1 key2:value2 ...'")
        ("data,d", po::value<std::string>(), "Send data with the request body.")
        ("limit_response,l", po::value<unsigned>(), "Limit the number of characters of the response to dump out");
    ;

    std::optional<unsigned> response_print_limit = std::nullopt;

    po::positional_options_description positional_options;
    positional_options.add("url", 1);

    po::variables_map vm;
    try {
        po::store(po::command_line_parser(argc, argv).options(desc).positional(positional_options).run(), vm);
        po::notify(vm);
    } catch (const po::error& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return EXIT_SUCCESS;
    }

    zclient::http_request req;
    std::string hostname;
    std::string port;
    connection_type conntype;

    if (!vm.count("url")) {
        std::cout << "URL must be provided. Prefixes: 'http://', 'https://', 'ws://', 'wss://'" << std::endl;
        std::cout << desc << std::endl;
        return EXIT_FAILURE;
    }
    
    /* split URL into hostname and path */
    std::string url = vm["url"].as<std::string>();
    size_t idx = 0;
    if ((idx = url.find("http://")) != std::string::npos) {
        fill_hostname_path_and_port(url.substr(idx + strlen("http://")), hostname, req.path, port, "http://");
        hostname = "http://" + hostname; // TODO: optimise
        if (!port.length()) port = "80";
        conntype = connection_type::http;
    } else if ((idx = url.find("https://")) != std::string::npos) {
        fill_hostname_path_and_port(url.substr(idx + strlen("https://")), hostname, req.path, port, "https://");
        hostname = "https://" + hostname; // TODO: optimise
        if (!port.length()) port = "443";
        conntype = connection_type::https;
    } else if ((idx = url.find("ws://")) != std::string::npos) {
        fill_hostname_path_and_port(url.substr(idx + strlen("ws://")), hostname, req.path, port, "ws://");
        hostname = "ws://" + hostname; // TODO: optimise
        if (!port.length()) port = "80";
        conntype = connection_type::ws;
    } else if ((idx = url.find("wss://")) != std::string::npos) {
        fill_hostname_path_and_port(url.substr(idx + strlen("wss://")), hostname, req.path, port, "wss://");
        hostname = "wss://" + hostname; // TODO: optimise
        if (!port.length()) port = "443";
        conntype = connection_type::wss;
    } else {
        std::cerr << "Unrecognized prefix. Only http:// or http:// supported" << std::endl;
        return EXIT_FAILURE;
    }

    if (vm.count("request")) {

        std::string_view request_str(vm["request"].as<std::string>());

        if (request_str == "GET") {
            req.method = zclient::http_method::get;
        } else if (request_str == "POST") {
            req.method = zclient::http_method::post;
        } else if (request_str == "PUT") {
            req.method = zclient::http_method::put;
        } else if (request_str == "DELETE") {
            req.method = zclient::http_method::delete_;
        } else {
            std::cerr << "Unsupported request method " << vm["request"].as<std::string>() << std::endl;
            return EXIT_FAILURE;
        }
    } else {
        req.method = zclient::http_method::get;
    }

    
    if (vm.count("headers")) {
        auto raw_header_fields = vm["headers"].as<std::vector<std::string>>();

        for (const auto& raw_header_field : raw_header_fields) {
            size_t idx = raw_header_field.find(":");
            if (idx == std::string::npos) {
                std::cerr << "Header fields must be provided as format key:value, found invalid value: '" << raw_header_field << "'" << std::endl;
                return EXIT_FAILURE;
            }

            req.header_data.push_back({raw_header_field.substr(0, idx), raw_header_field.substr(idx + 1)});
        }

        LOG_INFO << "Header data:";

        for (const auto& header_d : req.header_data) {
            LOG_INFO << "    " << header_d.first << ": " << header_d.second;
        }
    }

    req.body = "";

    if (vm.count("data")) {
        req.body = vm["data"].as<std::string>();
    }

    if (req.body.length()) {
        LOG_INFO << "Body data [first 100 chars]:";
        LOG_INFO << req.body.substr(0, 100);
    }

    if (vm.count("limit_response")) {
        response_print_limit = vm["limit_response"].as<unsigned>();
    }
    
    if (conntype == connection_type::http || conntype == connection_type::https) {
        zclient::zasync_exec(
            [hostname = std::move(hostname),
            port = std::move(port),
            req = std::move(req),
            response_print_limit = std::move(response_print_limit)
            ]() -> zclient::zasync {
                std::cout << "Sending request to " << hostname << ":" << port << std::endl;
                auto resp = co_await zclient::fetch(
                    hostname,
                    port,
                    req
                );

                std::cout << "Request to " << hostname << ":" << port << " returned with code " << resp.return_code << std::endl;
                
                std::cout << "HEADER ===" << std::endl;
                for (const auto& hdata : resp.header_data) {
                    std::cout << hdata.first << ": " << hdata.second << std::endl;
                }

                std::cout << "BODY ===" << std::endl;
                
                if (!response_print_limit.has_value())
                    std::cout << resp.body << std::endl;
                else
                    std::cout << resp.body.substr(0, response_print_limit.value()) << std::endl;
            }
        );
    } else {
        /* websockets */
        zclient::zasync_exec(
            [hostname = std::move(hostname),
             port = std::move(port),
             target = std::move(req.path)
            ]() -> zclient::zasync {
                zclient::websocket_client ws_client;
                
                LOG_INFO << "Connecting to hostname = " << hostname << ", port = " << port << ", path = " << target;
                auto result = co_await ws_client.connect(hostname, port, target);

                if (!result) {
                    LOG_ERROR << "could not connect to test websocket server";
                    co_return;
                }

                /* start a separate coroutine-thread for listening to incoming websocket events */
                zclient::zasync_exec(
                    [&]() -> zclient::zasync {
                        while (true) {
                            try {
                                auto message = co_await ws_client.read();
                                std::cout << hostname << ":" << port << target << " said: " << message << std::endl;
                            } catch (zclient::websocket_server_disconnected_exception& e) {
                                std::cout << "Server has terminated the session" << std::endl;
                                break;
                            }                 
                        }
                    }
                );

                /* block here so the above zasync_exec doesn't have dangling reference capture */
                while (true) {
                    try {
                        std::string message;
                        std::getline(std::cin, message);

                        co_await ws_client.write(message);

                        if (message == "exit" || message == "quit" || message == "exit()" || message == "quit()") {
                            break;
                        }
                    } catch (zclient::websocket_server_disconnected_exception& e) {
                        std::cout << "Server has terminated the session" << std::endl;
                        break;
                    }

                }
            }
        );
    }

    /* execute in two threads. For websockets, one thread is needed for listening and one thread
     * is needed for writing (two blocking loops). */
    std::thread t1{
        [](){
            zclient::zrun();
        }
    };

    std::signal(SIGINT, signal_handler);

    zclient::zrun();

    std::cout << "Terminating ..." << std::endl;

    /* if we get here it means SIGINT or SIGTERM */
    t1.join();

    return EXIT_SUCCESS;
}
