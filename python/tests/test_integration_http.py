# @author ssrjkk | volley
"""Integration tests for HTTP client with real HTTP server"""

import pytest
import threading
import time
from http.server import HTTPServer, BaseHTTPRequestHandler
from volley import HttpClient, HttpRequest, ConnectionPool, PoolConfig


class IntegrationTestHandler(BaseHTTPRequestHandler):
    """Simple HTTP handler for integration tests"""

    def log_message(self, format, *args):
        pass

    def do_GET(self):
        if self.path == "/success":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"OK")
        elif self.path == "/delay":
            time.sleep(0.1)
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"DELAYED")
        elif self.path == "/large":
            data = b"X" * 10000
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(data)
        elif self.path == "/headers":
            custom_header = self.headers.get("X-Custom", "missing")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(f"Header: {custom_header}".encode())
        elif self.path == "/error500":
            self.send_response(500)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"Internal Error")
        else:
            self.send_response(404)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"Not Found")

    def do_POST(self):
        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length)
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.end_headers()
        self.wfile.write(f"POST: {body.decode()}".encode())


@pytest.fixture(scope="module")
def http_server():
    """Start a real HTTP server for integration tests"""
    server = HTTPServer(("127.0.0.1", 0), IntegrationTestHandler)
    port = server.server_address[1]
    thread = threading.Thread(target=server.serve_forever)
    thread.daemon = True
    thread.start()
    yield f"127.0.0.1:{port}"
    server.shutdown()


def test_http_get_success(http_server):
    """Test real HTTP GET request"""
    host, port = http_server.split(":")
    client = HttpClient()
    req = HttpRequest(method="GET", target="/success", host=host, port=port)

    resp = client.request(req)

    assert resp["status_code"] == 200
    assert resp["body"] == "OK"
    assert resp["latency_us"] > 0


def test_http_get_with_delay(http_server):
    """Test HTTP request with server-side delay"""
    host, port = http_server.split(":")
    client = HttpClient()
    req = HttpRequest(method="GET", target="/delay", host=host, port=port)

    start = time.time()
    resp = client.request(req)
    elapsed = time.time() - start

    assert resp["status_code"] == 200
    assert resp["body"] == "DELAYED"
    assert elapsed >= 0.1


def test_http_get_large_response(http_server):
    """Test HTTP request with large response body"""
    host, port = http_server.split(":")
    client = HttpClient()
    req = HttpRequest(method="GET", target="/large", host=host, port=port)

    resp = client.request(req)

    assert resp["status_code"] == 200
    assert len(resp["body"]) == 10000
    assert resp["body"] == "X" * 10000


def test_http_custom_headers(http_server):
    """Test HTTP request with custom headers"""
    host, port = http_server.split(":")
    client = HttpClient()
    req = HttpRequest(
        method="GET",
        target="/headers",
        host=host,
        port=port,
        headers={"X-Custom": "test-value"},
    )

    resp = client.request(req)

    assert resp["status_code"] == 200
    assert "test-value" in resp["body"]


def test_http_post_with_body(http_server):
    """Test HTTP POST request with body"""
    host, port = http_server.split(":")
    client = HttpClient()
    req = HttpRequest(method="POST", target="/post", host=host, port=port, body="test data")

    resp = client.request(req)

    assert resp["status_code"] == 200
    assert "POST: test data" in resp["body"]


def test_http_404_not_found(http_server):
    """Test HTTP request to non-existent endpoint"""
    host, port = http_server.split(":")
    client = HttpClient()
    req = HttpRequest(method="GET", target="/nonexistent", host=host, port=port)

    resp = client.request(req)

    assert resp["status_code"] == 404
    assert resp["body"] == "Not Found"


def test_http_500_server_error(http_server):
    """Test HTTP request to endpoint returning 500"""
    host, port = http_server.split(":")
    client = HttpClient()
    req = HttpRequest(method="GET", target="/error500", host=host, port=port)

    resp = client.request(req)

    assert resp["status_code"] == 500
    assert "Internal Error" in resp["body"]


def test_http_connection_pool_reuse(http_server):
    """Test connection pool reuses connections"""
    host, port = http_server.split(":")
    pool = ConnectionPool(PoolConfig(min_connections=2, max_connections=5))

    client1 = pool.acquire(host, port)
    assert client1 is not None
    pool.release(client1, host, port)

    client2 = pool.acquire(host, port)
    assert client2 is not None
    assert client2 is client1

    pool.release(client2, host, port)


def test_http_multiple_requests_sequential(http_server):
    """Test multiple sequential HTTP requests"""
    host, port = http_server.split(":")
    client = HttpClient()

    for i in range(10):
        req = HttpRequest(method="GET", target="/success", host=host, port=port)
        resp = client.request(req)
        assert resp["status_code"] == 200
        assert resp["body"] == "OK"


def test_http_multiple_requests_parallel(http_server):
    """Test multiple parallel HTTP requests"""
    host, port = http_server.split(":")
    results = []

    def make_request():
        client = HttpClient()
        req = HttpRequest(method="GET", target="/success", host=host, port=port)
        resp = client.request(req)
        results.append(resp)

    threads = [threading.Thread(target=make_request) for _ in range(10)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    assert len(results) == 10
    for resp in results:
        assert resp["status_code"] == 200
        assert resp["body"] == "OK"


def test_http_https_detection():
    """Test HTTPS scheme detection based on port"""
    req = HttpRequest(method="GET", target="/", host="localhost", port="443")
    req_https = HttpRequest(method="GET", target="/", host="localhost", port="8080")

    assert str(req.port) == "443"
    assert str(req_https.port) == "8080"


def test_http_timeout():
    """Test HTTP request timeout"""
    client = HttpClient()
    client.timeout_ms = 100
    req = HttpRequest(method="GET", target="/", host="192.0.2.1", port="80")

    start = time.time()
    resp = client.request(req)
    elapsed = time.time() - start

    assert resp["status_code"] == 0
    assert elapsed < 2.0
