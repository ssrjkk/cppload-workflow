# @author ssrjkk | cppload
"""Tests for HttpClient class."""

from unittest.mock import Mock, patch
from cppload import HttpClient, HttpRequest


class TestHttpClient:
    """Test HttpClient HTTP requests."""

    def test_http_client_default_values(self):
        """Test HttpClient default initialization."""
        client = HttpClient()
        assert client.timeout_ms == 5000
        assert client.keep_alive is True

    @patch("urllib.request.urlopen")
    def test_request_success(self, mock_urlopen):
        """Test successful HTTP request."""
        mock_response = Mock()
        mock_response.status = 200
        mock_response.read.return_value = b'{"status": "ok"}'
        mock_response.headers = {"Content-Type": "application/json"}
        mock_urlopen.return_value = mock_response

        client = HttpClient()
        req = HttpRequest(method="GET", target="/api/test", host="example.com", port="80")
        result = client.request(req)

        assert result["status_code"] == 200
        assert result["body"] == '{"status": "ok"}'
        assert "latency_us" in result
        assert result["latency_us"] >= 0

    @patch("urllib.request.urlopen")
    def test_request_https_port_443(self, mock_urlopen):
        """Test HTTPS request on port 443."""
        mock_response = Mock()
        mock_response.status = 200
        mock_response.read.return_value = b"OK"
        mock_response.headers = {}
        mock_urlopen.return_value = mock_response

        client = HttpClient()
        req = HttpRequest(method="GET", target="/", host="example.com", port="443")
        result = client.request(req)

        assert result["status_code"] == 200

    @patch("urllib.request.urlopen")
    def test_request_with_body(self, mock_urlopen):
        """Test HTTP request with body."""
        mock_response = Mock()
        mock_response.status = 201
        mock_response.read.return_value = b"Created"
        mock_response.headers = {}
        mock_urlopen.return_value = mock_response

        client = HttpClient()
        req = HttpRequest(
            method="POST",
            target="/api/create",
            body='{"name": "test"}',
            host="example.com",
            port="80",
        )
        result = client.request(req)

        assert result["status_code"] == 201

    @patch("urllib.request.urlopen")
    def test_request_http_error(self, mock_urlopen):
        """Test HTTP error response."""
        import urllib.error

        error = urllib.error.HTTPError(
            url="http://example.com/notfound",
            code=404,
            msg="Not Found",
            hdrs={},
            fp=Mock(read=Mock(return_value=b"Not Found")),
        )
        mock_urlopen.side_effect = error

        client = HttpClient()
        req = HttpRequest(method="GET", target="/notfound", host="example.com", port="80")
        result = client.request(req)

        assert result["status_code"] == 404
        assert result["body"] == "Not Found"

    @patch("urllib.request.urlopen")
    def test_request_connection_error(self, mock_urlopen):
        """Test connection error."""
        mock_urlopen.side_effect = Exception("Connection refused")

        client = HttpClient()
        req = HttpRequest(method="GET", target="/", host="example.com", port="80")
        result = client.request(req)

        assert result["status_code"] == 0
        assert "Connection refused" in result["body"]
        assert result["latency_us"] == 0

    @patch("urllib.request.urlopen")
    def test_request_with_headers(self, mock_urlopen):
        """Test request with custom headers."""
        mock_response = Mock()
        mock_response.status = 200
        mock_response.read.return_value = b"OK"
        mock_response.headers = {}
        mock_urlopen.return_value = mock_response

        client = HttpClient()
        req = HttpRequest(
            method="GET", target="/", host="example.com", port="80", headers={"X-Custom": "value"}
        )
        result = client.request(req)

        assert result["status_code"] == 200
