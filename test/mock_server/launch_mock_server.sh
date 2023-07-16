#!/bin/bash

set -xe

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

# endpoint_config.json server_key server_cert unsecured_port secured_port
nohup node $SCRIPT_DIR/mock_server.js $SCRIPT_DIR/test_endpoint_config.json $1 $2 $3 $4 > server.log 2>&1 &