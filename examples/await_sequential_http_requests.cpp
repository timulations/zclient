#include <iostream>
#include "zclient.hpp"

#include <vector>

/* async-await style chaining of asynchronous http requests */

using namespace zclient;

zasync sequential_http_requests() {

    /* unsecured http request to google */
    auto resp1 = co_await fetch("http://www.google.com", "80", http_request{
        .method = http_method::get,
        .path = "/",
        .header_data = {}
    });
    std::cout << "Request 1 returned with code: " << resp1.return_code << std::endl;
    std::cout << "and data (first 100 chars): " << resp1.body.substr(0, 100) << std::endl;

    /* secured http request to binance */
    auto resp2 = co_await fetch("https://testnet.binance.vision", "443", http_request{
        .method = http_method::get,
        .path = "/api/v3/trades?symbol=BTCUSDT&limit=5",
        .header_data = {}
    });
    std::cout << "Request 2 returned with code: " << resp2.return_code << std::endl;
    std::cout << "and data (first 100 chars): " << resp2.body.substr(0, 100) << std::endl;

    /* secured http request to cppreference.com */
    auto resp3 = co_await fetch("https://en.cppreference.com", "443", http_request{
        .method = http_method::get,
        .path = "/w/cpp/language/basic_concepts",
        .header_data = {}
    });
    std::cout << "Request 3 returned with code: " << resp3.return_code << std::endl;
    std::cout << "and data (first 100 chars): " << resp3.body.substr(0, 100) << std::endl;  
}


/* This example demonstrates how to sequentially make http and https requests. Only make 
 * the later requests after the earlier ones have returned */
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    zasync_exec(sequential_http_requests);
    zrun();

    return EXIT_SUCCESS;
}