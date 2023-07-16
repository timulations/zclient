#include <iostream>
#include "zclient.hpp"

#include <vector>

/* example of callback style of firing off multiple parallel http requests */

using namespace zclient;

void parallel_http_requests() {
    fetch_then(
        "www.google.com",
        "80",
        http_request{
            .method = http_method::get,
            .path = "/",
            .header_data = {}
        },
        [](http_response&& resp) {
            std::cout << "Request 1 returned with code: " << resp.return_code << std::endl;
            std::cout << "and data (first 100 chars): " << resp.body.substr(0, 100) << std::endl;
        }
    );

    fetch_then(
        "https://testnet.binance.vision",
        "443",
        http_request{
            .method = http_method::get,
            .path = "/api/v3/trades?symbol=BTCUSDT&limit=5",
            .header_data = {}
        },
        [](http_response&& resp) {
            std::cout << "Request 2 returned with code: " << resp.return_code << std::endl;
            std::cout << "and data (first 100 chars): " << resp.body.substr(0, 100) << std::endl;
        }
    );

    fetch_then(
        "https://en.cppreference.com",
        "443",
        http_request{
            .method = http_method::get,
            .path = "/w/cpp/language/basic_concepts",
            .header_data = {}
        },
        [](http_response&& resp) {
            std::cout << "Request 3 returned with code: " << resp.return_code << std::endl;
            std::cout << "and data (first 100 chars): " << resp.body.substr(0, 100) << std::endl;  
        }
    );
}


/* This example demonstrates how to make http and https requests in parallel. In this case
 * there is no guarantee of which one returns first (it's a race!) */
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    parallel_http_requests();
    zrun();

    return EXIT_SUCCESS;
}