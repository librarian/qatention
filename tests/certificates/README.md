Synthetic TLS fixtures for local tests only. The leaf private key is intentionally
public test data, never a production credential. The root private key is not kept.

The leaf covers localhost/127.0.0.1 and points to the test HTTP server on port 18081
for its missing issuer. Only the test process trusts root.pem, and restores its
previous trust configuration afterward. Certificates expire in October 2036.

The `purpose-*` fixtures use a separate test root and the same public test leaf
key. Their distinct serial numbers cover a valid TLS server, a client-only EKU,
and a leaf whose key usage permits certificate signing instead of TLS. The
purpose root's private key was discarded. These fixtures expire in October 2036.
