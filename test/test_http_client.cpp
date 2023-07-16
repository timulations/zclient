#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <unordered_map>

#include "zclient.hpp"
#include "zlogger.hpp"
#include "json/json.h"

using namespace zclient;

#ifndef MOCK_SERVER_UNSECURED_PORT
#define MOCK_SERVER_UNSECURED_PORT "3000"
#endif

#ifndef MOCK_SERVER_SECURED_PORT
#define MOCK_SERVER_SECURED_PORT "300"
#endif

/* Tests for unsecured HTTP */
class ClientTester {
public:
    ClientTester(
        const std::string& host,
        const std::string& port,
        const std::vector<std::pair<std::string, std::string>>& mock_server_endpoints
    )
    :_host{host}
    ,_port{port}
    ,_mock_server_endpoints{mock_server_endpoints}
    ,_endpoint_index{0}
    {}

    void test_http_basic_response();
    void test_sequential_http_responses();
    void test_http_request_header_and_body_echo();
    void test_connect_to_external_site(const std::string& hostname, const std::string& path, const std::string& port);

private:
    const std::string _host;
    const std::string _port;
    /* <endpoint, expected text response> */
    const std::vector<std::pair<std::string, std::string>> _mock_server_endpoints;

    int _endpoint_index;

    const std::pair<std::string, std::string>& get_endpoint_and_expected_resp_pair() {
        auto& ret = _mock_server_endpoints[++_endpoint_index];
        _endpoint_index %= _mock_server_endpoints.size();

        return ret;
    } 
};

void ClientTester::test_http_basic_response() {

    auto endpoint_and_expected_resp = get_endpoint_and_expected_resp_pair();

    zasync_exec([host = std::move(_host),
                 port = std::move(_port),
                 path = std::move(endpoint_and_expected_resp.first),
                 expected_resp = std::move(endpoint_and_expected_resp.second)
                ]() -> zasync {
        auto resp = co_await fetch(
            host,
            port,
            http_request{
                .method = http_method::get,
                .path = path
            }
        );

        /* the client has a timeout of zclient::FETCH_TIMEOUT_SECONDS */
        assert(resp.body == expected_resp);
    });
}

void ClientTester::test_http_request_header_and_body_echo() {
    /* Test that the header and body of the HTTP request is correctly interpreted.
     * the /echo endpoint is expected to echo back exactly the header and body it
     * received. */
    zasync_exec([host = std::move(_host),
                 port = std::move(_port),
                 path = "/echo"
                ]() -> zasync {

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

        /* the client has a timeout of zclient::FETCH_TIMEOUT_SECONDS */
        for (const auto& hd : header_data) {
            int idx = -1;
            
            for (int i = 0; i < resp.header_data.size(); ++i) {
                auto received_hd = resp.header_data[i];
                if (received_hd.first == hd.first) {
                    idx = i;
                    break;
                }
            }

            assert(idx != -1);
            assert(resp.header_data[idx].second == hd.second);
        }

        assert(resp.body == message_body);
    });
}

void ClientTester::test_sequential_http_responses() {
    /* Test that responses are received in sequential order with async/await */

    auto endpoint_and_expected_resp1 = get_endpoint_and_expected_resp_pair();
    auto endpoint_and_expected_resp2 = get_endpoint_and_expected_resp_pair();
    auto endpoint_and_expected_resp3 = get_endpoint_and_expected_resp_pair();

    zasync_exec([host = std::move(_host),
                 port = std::move(_port),
                 inputs = std::move(std::vector<std::pair<std::string, std::string>>{endpoint_and_expected_resp1, endpoint_and_expected_resp2, endpoint_and_expected_resp3})
                ]() -> zasync {

        std::vector<std::string> responses;

        auto resp1 = co_await fetch(
            host,
            port,
            http_request{
                .method = http_method::get,
                .path = inputs[0].first
            }
        );
        responses.emplace_back(resp1.body);

        assert(resp1.return_code == 200);

        auto resp2 = co_await fetch(
            host,
            port,
            http_request{
                .method = http_method::get,
                .path = inputs[1].first
            }
        );
        responses.emplace_back(resp2.body);

        assert(resp2.return_code == 200);

        auto resp3 = co_await fetch(
            host,
            port,
            http_request{
                .method = http_method::get,
                .path = inputs[2].first
            }
        );
        responses.emplace_back(resp3.body);

        assert(resp3.return_code == 200);

        assert(responses[0] == inputs[0].second);
        assert(responses[1] == inputs[1].second);
        assert(responses[2] == inputs[2].second);
    });
}

void ClientTester::test_connect_to_external_site(const std::string& hostname, const std::string& path, const std::string& port) {
    /* Test connection to a well known outside source */
    zasync_exec([port = std::move(port), path = std::move(path), hostname = std::move(hostname)]() -> zasync {

        auto resp = co_await fetch(
            hostname,
            port,
            http_request{
                .method = http_method::get,
                .path = path
            }
        );

        /* the client has a timeout of zclient::FETCH_TIMEOUT_SECONDS */
        assert(resp.return_code == 200);
        assert(resp.body.length());
    });
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cout << "Usage: \n";
        std::cout << "./test_http_client TEST_ENDPOINT_CONFIG_JSON\n";
        std::cout << "    where TEST_ENDPOINT_CONFIG_JSON is a JSON file of spec {'/endpoint/name': 'text_response', ...}";
        std::cout << std::endl;
    }

    LOG_DEBUG << "Attempting to load endpoint config file: " << argv[1];

    /* read the endpoint config file and ingest the endpoints and expected reponses */
    if (!std::filesystem::exists(argv[1])) {
        LOG_DEBUG << "Couldn't find " << argv[1];
        throw std::runtime_error("Could not find config JSON file: '" + std::string(argv[1]) + "', please make sure your path is correct\n");
    }

    std::ifstream ifs;
    ifs.open(argv[1]);

    LOG_DEBUG << "Opened: " << argv[1];

    Json::CharReaderBuilder builder;
    Json::Value root;
    JSONCPP_STRING errs;

    /* parse the JSON*/
    if (!parseFromStream(builder, ifs, &root, &errs)) {
        LOG_ERROR << errs;
        return EXIT_FAILURE;
    }

    std::vector<std::pair<std::string, std::string>> mock_server_endpoints;

    for (const auto& endpoint : root.getMemberNames()) {
        LOG_DEBUG << endpoint;
        mock_server_endpoints.push_back(std::make_pair(endpoint, root[endpoint].asString()));
    }

    LOG_DEBUG << "Creating tester";

    ClientTester http_tester{"http://localhost", MOCK_SERVER_UNSECURED_PORT, mock_server_endpoints};
    ClientTester https_tester{"https://localhost", MOCK_SERVER_SECURED_PORT, mock_server_endpoints};

    LOG_DEBUG << "Tester created, now commencing tests...";

    /* TODO: make multithreaded */
    #define RUN(x) x; printf(#x); printf("\n");
    RUN(http_tester.test_http_basic_response());
    RUN(http_tester.test_sequential_http_responses());
    RUN(http_tester.test_http_request_header_and_body_echo());
    RUN(http_tester.test_connect_to_external_site("http://www.google.com", "/", "80"));
    RUN(https_tester.test_connect_to_external_site("https://testnet.binance.vision", "/api/v3/time", "443"));
    LOG_DEBUG << "All tests pass!";

    zrun();
    #undef RUN

    return EXIT_SUCCESS;
}
