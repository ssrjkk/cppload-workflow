# @author ssrjkk | volley
"""Tests for VaultClient."""

from unittest.mock import Mock, patch, MagicMock
from volley import VaultClient, VaultConfig


class TestVaultClient:
    """Test VaultClient functionality."""

    def test_vault_client_default_config(self):
        """Test VaultClient default configuration."""
        with patch("urllib.request.urlopen") as mock_urlopen:
            mock_response = Mock()
            mock_response.status = 200
            mock_urlopen.return_value.__enter__ = Mock(return_value=mock_response)
            mock_urlopen.return_value.__exit__ = Mock(return_value=False)

            client = VaultClient()
            assert client.config.address == "http://127.0.0.1:8200"
            assert client.config.engine_path == "secret"
            assert client.config.timeout_seconds == 5

    @patch("urllib.request.urlopen")
    def test_vault_client_connected(self, mock_urlopen):
        """Test VaultClient successful connection."""
        mock_response = Mock()
        mock_response.status = 200
        mock_urlopen.return_value.__enter__ = Mock(return_value=mock_response)
        mock_urlopen.return_value.__exit__ = Mock(return_value=False)

        client = VaultClient()
        assert client.is_connected is True

    @patch("urllib.request.urlopen")
    def test_vault_client_connection_failure(self, mock_urlopen):
        """Test VaultClient connection failure."""
        mock_urlopen.side_effect = Exception("Connection refused")

        client = VaultClient()
        assert client.is_connected is False
        assert "Connection refused" in client._last_error

    @patch("urllib.request.urlopen")
    def test_vault_client_unhealthy(self, mock_urlopen):
        """Test VaultClient unhealthy response."""
        mock_response = Mock()
        mock_response.status = 500
        mock_urlopen.return_value.__enter__ = Mock(return_value=mock_response)
        mock_urlopen.return_value.__exit__ = Mock(return_value=False)

        client = VaultClient()
        assert client.is_connected is False

    @patch("urllib.request.urlopen")
    def test_get_secret_success(self, mock_urlopen):
        """Test successful secret retrieval."""
        # Health check
        health_response = Mock()
        health_response.status = 200

        # Secret response
        secret_response = Mock()
        secret_response.read.return_value = b'{"data": {"data": {"password": "secret123"}}}'

        mock_urlopen.side_effect = [
            MagicMock(
                __enter__=Mock(return_value=health_response), __exit__=Mock(return_value=False)
            ),
            MagicMock(
                __enter__=Mock(return_value=secret_response), __exit__=Mock(return_value=False)
            ),
        ]

        config = VaultConfig(token="test_token")
        client = VaultClient(config)

        result = client.get_secret("myapp", "password")
        assert result == "secret123"

    @patch("urllib.request.urlopen")
    def test_get_secret_not_found(self, mock_urlopen):
        """Test secret not found."""
        health_response = Mock()
        health_response.status = 200

        mock_response = Mock()
        mock_response.read.return_value = b'{"data": {"data": {}}}'

        mock_urlopen.side_effect = [
            MagicMock(
                __enter__=Mock(return_value=health_response), __exit__=Mock(return_value=False)
            ),
            MagicMock(
                __enter__=Mock(return_value=mock_response), __exit__=Mock(return_value=False)
            ),
        ]

        client = VaultClient(VaultConfig(token="token"))
        result = client.get_secret("myapp", "missing")
        assert result is None

    @patch("urllib.request.urlopen")
    def test_get_secret_error(self, mock_urlopen):
        """Test get_secret error handling."""
        health_response = Mock()
        health_response.status = 200

        mock_urlopen.side_effect = [
            MagicMock(
                __enter__=Mock(return_value=health_response), __exit__=Mock(return_value=False)
            ),
            Exception("Network error"),
        ]

        client = VaultClient(VaultConfig(token="token"))
        result = client.get_secret("myapp", "key")
        assert result is None

    @patch("urllib.request.urlopen")
    def test_get_secret_non_string_value(self, mock_urlopen):
        """Test get_secret with non-string value."""
        health_response = Mock()
        health_response.status = 200

        secret_response = Mock()
        secret_response.read.return_value = b'{"data": {"data": {"count": 42}}}'

        mock_urlopen.side_effect = [
            MagicMock(
                __enter__=Mock(return_value=health_response), __exit__=Mock(return_value=False)
            ),
            MagicMock(
                __enter__=Mock(return_value=secret_response), __exit__=Mock(return_value=False)
            ),
        ]

        client = VaultClient(VaultConfig(token="token"))
        result = client.get_secret("myapp", "count")
        assert result is None

    @patch("urllib.request.urlopen")
    def test_get_secret_map_success(self, mock_urlopen):
        """Test successful secret map retrieval."""
        health_response = Mock()
        health_response.status = 200

        secret_response = Mock()
        secret_response.read.return_value = (
            b'{"data": {"data": {"user": "admin", "pass": "secret"}}}'
        )

        mock_urlopen.side_effect = [
            MagicMock(
                __enter__=Mock(return_value=health_response), __exit__=Mock(return_value=False)
            ),
            MagicMock(
                __enter__=Mock(return_value=secret_response), __exit__=Mock(return_value=False)
            ),
        ]

        client = VaultClient(VaultConfig(token="token"))
        result = client.get_secret_map("myapp")
        assert result == {"user": "admin", "pass": "secret"}

    @patch("urllib.request.urlopen")
    def test_get_secret_map_empty(self, mock_urlopen):
        """Test get_secret_map with empty data."""
        health_response = Mock()
        health_response.status = 200

        secret_response = Mock()
        secret_response.read.return_value = b'{"data": {"data": {}}}'

        mock_urlopen.side_effect = [
            MagicMock(
                __enter__=Mock(return_value=health_response), __exit__=Mock(return_value=False)
            ),
            MagicMock(
                __enter__=Mock(return_value=secret_response), __exit__=Mock(return_value=False)
            ),
        ]

        client = VaultClient(VaultConfig(token="token"))
        result = client.get_secret_map("myapp")
        assert result == {}

    @patch("urllib.request.urlopen")
    def test_get_secret_map_error(self, mock_urlopen):
        """Test get_secret_map error handling."""
        health_response = Mock()
        health_response.status = 200

        mock_urlopen.side_effect = [
            MagicMock(
                __enter__=Mock(return_value=health_response), __exit__=Mock(return_value=False)
            ),
            Exception("Network error"),
        ]

        client = VaultClient(VaultConfig(token="token"))
        result = client.get_secret_map("myapp")
        assert result == {}

    @patch("urllib.request.urlopen")
    def test_get_secret_map_filters_non_strings(self, mock_urlopen):
        """Test get_secret_map filters non-string values."""
        health_response = Mock()
        health_response.status = 200

        secret_response = Mock()
        secret_response.read.return_value = b'{"data": {"data": {"user": "admin", "count": 42}}}'

        mock_urlopen.side_effect = [
            MagicMock(
                __enter__=Mock(return_value=health_response), __exit__=Mock(return_value=False)
            ),
            MagicMock(
                __enter__=Mock(return_value=secret_response), __exit__=Mock(return_value=False)
            ),
        ]

        client = VaultClient(VaultConfig(token="token"))
        result = client.get_secret_map("myapp")
        assert result == {"user": "admin"}

    def test_health_check_with_token(self):
        """Test health check with token configuration."""
        # Just verify the config is stored correctly
        config = VaultConfig(token="my_token", address="http://localhost:8200")
        assert config.token == "my_token"
        assert config.address == "http://localhost:8200"

    def test_get_secret_invalid_path(self):
        """Test get_secret rejects paths with invalid characters."""
        import pytest

        client = VaultClient(VaultConfig(token="token"))
        with pytest.raises(ValueError, match="Invalid secret path"):
            client.get_secret("../etc/passwd", "key")
