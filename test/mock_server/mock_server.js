const express = require('express');
const path = require('path');
const http = require('http');
const https = require('https');
const fs = require('fs');
var bodyParser = require('body-parser');

/* PID dumping so we can terminate server externally later */
const pidFilePath = path.join(__dirname, 'mock_server_pid.txt');
fs.writeFileSync(pidFilePath, process.pid.toString());

/* Create express server */
const app = express();

var bodyParseOptions = {
  inflate: true,
  limit: '100kb',
  type: '*/*'
};

app.use(bodyParser.raw(bodyParseOptions));

/* Create endpoints based on provided JSON configuration */
const jsonFilePath = process.argv[2];
const serverKeyPath = process.argv[3];
const serverCertPath = process.argv[4];
const unsecured_port = process.argv[5];
const secured_port = process.argv[6];
const endpointConfigurations = JSON.parse(fs.readFileSync(jsonFilePath));

Object.entries(endpointConfigurations).forEach(([endpoint, responseText]) => {
  app.get(endpoint, (req, res) => {
    res.send(responseText);
  });
});

app.post("/echo", (req, res) => {
  /* echo back the same headers and body we received */
  res.set(req.headers);
  console.log(req.body);
  res.send(req.body);
});

/* Start the server - listen on both unsecured HTTP port and secured HTTPS port */
app.listen(unsecured_port, () => {
  console.log(`Mock server is running on http://localhost:${unsecured_port} with PID:${process.pid}`);
});

const httpsOptions = {
  key: fs.readFileSync(serverKeyPath), /* read private (decryption) key */
  cert: fs.readFileSync(serverCertPath) /* read certificate (contains public key) */
};

const httpsServer = https.createServer(httpsOptions, app); /* pass the request listeners to HTTPS server */
httpsServer.listen(secured_port, () => {
  console.log(`Mock server is running on https://localhost:${secured_port} with PID:${process.pid}`)
})
