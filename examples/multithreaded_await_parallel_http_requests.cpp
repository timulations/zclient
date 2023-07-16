#include <iostream>
#include "zclient.hpp"

#include <thread>
#include <vector>

/* example of async-await style of firing off multiple parallel http requests 
 * with multithreaded boost::io_context */

using namespace zclient;

zasync query_google() {

    /* unsecured http request to google */
    auto resp1 = co_await fetch("http://www.google.com", "80", http_request{
        .method = http_method::get,
        .path = "/",
        .header_data = {}
    });
    std::cout << "Request 1 returned with code: " << resp1.return_code << std::endl;
    std::cout << "and data (first 100 chars): " << resp1.body.substr(0, 100) << std::endl;
}

zasync query_binance() {
    /* secured http request to binance */
    auto resp2 = co_await fetch("https://testnet.binance.vision", "443", http_request{
        .method = http_method::get,
        .path = "/api/v3/trades?symbol=BTCUSDT&limit=5",
        .header_data = {}
    });
    std::cout << "Request 2 returned with code: " << resp2.return_code << std::endl;
    std::cout << "and data (first 100 chars): " << resp2.body.substr(0, 100) << std::endl;
}

zasync query_cpp_reference() {
    /* secured http request to cppreference.com */
    auto resp3 = co_await fetch("https://en.cppreference.com", "443", http_request{
        .method = http_method::get,
        .path = "/w/cpp/language/basic_concepts",
        .header_data = {}
    });
    std::cout << "Request 3 returned with code: " << resp3.return_code << std::endl;
    std::cout << "and data (first 100 chars): " << resp3.body.substr(0, 100) << std::endl;  
}


/* This example demonstrates how to make http and https requests in parallel. In this case
 * there is no guarantee of which one returns first (it's a race!) */
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    constexpr int num_threads = 3;

    zasync_exec(query_cpp_reference);
    zasync_exec(query_google);
    zasync_exec(query_binance);

    /* zrun() calls .run() on io_context under the hood. io_context can be made to manage
     * the main event loop over multiple threads by calling .run() over multiple threads. */

    std::vector<std::thread> thread_pool;
    thread_pool.reserve(num_threads - 1);
    
    for (int i = 0; i < num_threads - 1; ++i) {
        thread_pool.emplace_back(
            []{
                zrun();
            }
        );
    }

    /* also run on the current main thread */
    zrun();

    /* block until all threads exit */

    for (auto& thr: thread_pool) {
        thr.join();
    }

    return EXIT_SUCCESS;
}