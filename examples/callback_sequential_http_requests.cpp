#include <iostream>
#include "zclient.hpp"

#include <vector>

/* callback-style chaining of asynchronous http requests */

using namespace zclient;

void sequential_http_requests() {

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

            /* secured http request to binance */
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

                    /* secured http request to cppreference.com */
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
            );
        }
    );
}


/* This example demonstrates how to sequentially make http and https requests. Only make 
 * the later requests after the earlier ones have returned */
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    sequential_http_requests();
    zrun();

    return EXIT_SUCCESS;
}