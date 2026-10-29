Synthetic TLS fixtures for local tests only. The leaf private key is intentionally
public test data, never a production credential. The root private key is not kept.

The leaf covers localhost/127.0.0.1 and points to the test HTTP server on port 18081
for its missing issuer. Only the test process trusts root.pem, and restores its
previous trust configuration afterward. Certificates expire in October 2036.
