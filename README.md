# ZCLIENT
Tired of callback hell when writing C++ networking code? Introducing ZCLIENT, a coroutines-based awaitable networking client built on Boost::Beast in C++20. Start writing asynchronous network code that reads like it's synchronous today! :rocket::rocket::rocket:

## Supported Protocols
- http://
- https://
- ws://
- wss://

With the SSL-secured variants (https:// and wss://), SSL verification is enabled by default, and it searches for trusted certificates on your computer by default (powered by https://github.com/djarek/certify.git).

## Coverage 
| Branch | Mac OS | Linux | Windows |
| ------ | ------ | ----- | ------- |
| main   | ✅ | ?    |   ?     |

## Requirements / Dependencies:
- C++20: Required for coroutines support
- Boost::Beast: The legendary library that this library is built on
- OpenSSL: Required for TLS/secure sockets 
- CMake: 3.16 or later

If you would like to run the tests:
- NodeJS: For setting up mock servers to test zclient against

## Getting Started and Building

ZCLIENT comes as a static library which you will need to build. 

There is a CURL-like frontend `zclient_cli` which integrates the library, and other example code in `examples/`.

```
git clone --recursive https://github.com/timulations/zclient.git
cd zclient
mkdir build && cd build

# build library
make libzclient

# build frontend client
make zclient_cli

# build all
make
```

## Running Tests
```
# Assumes you have a build/ folder from before
cd build
ctest --verbose
```

## zclient_cli Usage
Similar to CURL
```
./zclient_cli

URL must be provided. Prefixes: 'http://', 'https://', 'ws://', 'wss://'
Allowed options:
  -h [ --help ]               print this help message
  --url arg                   Specify the URL to request. http:// for unsecured
                              and https:// for secured
  -X [ --request ] arg        Specify the HTTP request method. Supported = 
                              [GET, POST, PUT, DELETE]
  -H [ --headers ] arg        Specify the headers. Format = 'key1:value1 
                              key2:value2 ...'
  -d [ --data ] arg           Send data with the request body.
  -l [ --limit_response ] arg Limit the number of characters of the response to
                              dump out
```


## Library Usage
### Making asynchronous HTTP requests sequentially
Similar to the `await fetch()`-style JavaScript syntax. Being powered by `boost::asio` under the hood, `zrun()` is required to be called on every thread which you intend to use for networking handling, in order to `.run()` the underlying `io_context` for completion queue handling to actually happen. 

What happens is that execution will suspend when it hits each `co_await` and return to the caller, until the request has returned with a response, then the thread will resume execution from where it left off to go to the next requests in `sequential_http_requests()`. Asynchronous code that reads synchronously!

`zasync_exec` calls boost's `co_spawn` under the hood.

```cpp
#include <iostream>
#include "zclient.hpp"

#include <vector>

/* async-await style chaining of asynchronous http requests */

using namespace zclient;

zasync sequential_http_requests() {

    /* unsecured http request to google */
    auto resp1 = co_await fetch("http://www.google.com", "80", http_request{
        .method = http_method::get,
        .path = "/"
    });
    std::cout << "Request 1 returned with code: " << resp1.return_code << std::endl;
    std::cout << "and data (first 100 chars): " << resp1.body.substr(0, 100) << std::endl;

    /* secured http request to binance */
    auto resp2 = co_await fetch("https://testnet.binance.vision", "443", http_request{
        .method = http_method::get,
        .path = "/api/v3/trades?symbol=BTCUSDT&limit=5"
    });
    std::cout << "Request 2 returned with code: " << resp2.return_code << std::endl;
    std::cout << "and data (first 100 chars): " << resp2.body.substr(0, 100) << std::endl;

    /* secured http request to cppreference.com */
    auto resp3 = co_await fetch("https://en.cppreference.com", "443", http_request{
        .method = http_method::get,
        .path = "/w/cpp/language/basic_concepts"
    });
    std::cout << "Request 3 returned with code: " << resp3.return_code << std::endl;
    std::cout << "and data (first 100 chars): " << resp3.body.substr(0, 100) << std::endl;  
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    zasync_exec(sequential_http_requests);
    zrun();

    return EXIT_SUCCESS;
}
```

### Making HTTP requests in parallel
In this case the three HTTP requests are fired off in parallel, handled by a single thread (can use more threads by spawning more threads then calling `zrun()` in each of them). Unlike the previous case.
```cpp
#include <iostream>
#include "zclient.hpp"

#include <vector>

/* example of async-await style of firing off multiple parallel http requests */
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

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    zasync_exec(query_cpp_reference);
    zasync_exec(query_google);
    zasync_exec(query_binance);

    zrun();

    return EXIT_SUCCESS;
}
```

### Callback-style HTTP requests (one request after the previous one returns with response)
Still want to do callback? That's still possible. ZCLIENT was developed so we *don't* have to do this, but it is still supported. This example sends request 2 after request 1 responds with a response, then request 3 after request 2, etc.
```cpp
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

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    sequential_http_requests();
    zrun();

    return EXIT_SUCCESS;
}
```


### Making POST requests
Simply populate `http_request` appropriately. See `http_client.hpp` for documentation. `PUT` and `DELETE` requests are also supported.
```cpp
    std::vector<std::pair<std::string, std::string>> header_data{
        {"first_name", "Timmo"},
        {"surname", "Awesome"},
        {"age", "25"}, 
        {"profession", "SWE"}
    };

    std::string message_body{"Hello there, the headers have my details"};

    auto header_data_with_content_type = header_data;
    header_data_with_content_type.push_back({"Content-Type", "text/plain"});

    auto resp = co_await fetch(
        host,
        port,
        http_request{
            .method = http_method::post,
            .path = path,
            .header_data = header_data_with_content_type,
            .body = message_body
        }
    );
```


### Websockets
Websocket client is also supported in an awaitable style. A client object is created and then connected to a server. After the connect request responds and is successful, you are good to go to read and write from/to the server. The read and write should ideally occur on separate threads of activity so they don't block each other. But if you read and write to happen in the same loop, be my guest - have them in one thread.

Here's an example of a simple websocket client from `zclient_cli`

```cpp
zclient::zasync_exec(
    [hostname = std::move(hostname),
     port = std::move(port),
     target = std::move(req.path)
    ]() -> zclient::zasync {

        /* create websocket client object */
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
```
