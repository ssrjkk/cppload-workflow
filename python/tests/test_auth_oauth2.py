# @author ssrjkk | volley
"""Tests for AuthProvider OAuth2 and advanced features."""

import time
from unittest.mock import Mock, patch
from volley import AuthProvider, AuthConfig, AuthType


class TestAuthProviderOAuth2:
    """Test AuthProvider OAuth2 functionality."""

    @patch("urllib.request.urlopen")
    def test_oauth2_init_fetches_token(self, mock_urlopen):
        """Test OAuth2 initialization fetches token."""
        mock_response = Mock()
        mock_response.read.return_value = b'{"access_token": "test_token", "expires_in": 3600}'
        mock_urlopen.return_value.__enter__ = Mock(return_value=mock_response)
        mock_urlopen.return_value.__exit__ = Mock(return_value=False)

        config = AuthConfig(
            type=AuthType.OAUTH2,
            client_id="test_client",
            client_secret="test_secret",
            token_endpoint="http://auth.example.com/token",
        )
        provider = AuthProvider(config)

        assert provider._current_token == "test_token"
        assert provider._token_expiry > time.time()

    @patch("urllib.request.urlopen")
    def test_oauth2_apply_headers(self, mock_urlopen):
        """Test OAuth2 applies bearer token to headers."""
        mock_response = Mock()
        mock_response.read.return_value = b'{"access_token": "oauth_token", "expires_in": 3600}'
        mock_urlopen.return_value.__enter__ = Mock(return_value=mock_response)
        mock_urlopen.return_value.__exit__ = Mock(return_value=False)

        config = AuthConfig(
            type=AuthType.OAUTH2,
            client_id="client",
            client_secret="secret",
            token_endpoint="http://auth.example.com/token",
        )
        provider = AuthProvider(config)

        headers = {}
        provider.apply_headers(headers)
        assert headers["Authorization"] == "Bearer oauth_token"

    @patch("urllib.request.urlopen")
    def test_oauth2_token_refresh_on_expiry(self, mock_urlopen):
        """Test OAuth2 refreshes expired token."""
        mock_response = Mock()
        mock_response.read.return_value = b'{"access_token": "new_token", "expires_in": 3600}'
        mock_urlopen.return_value.__enter__ = Mock(return_value=mock_response)
        mock_urlopen.return_value.__exit__ = Mock(return_value=False)

        config = AuthConfig(
            type=AuthType.OAUTH2,
            client_id="client",
            client_secret="secret",
            token_endpoint="http://auth.example.com/token",
        )
        provider = AuthProvider(config)
        provider._token_expiry = time.time() - 100  # Expired

        headers = {}
        provider.apply_headers(headers)

        assert headers["Authorization"] == "Bearer new_token"

    def test_is_expired(self):
        """Test _is_expired method."""
        provider = AuthProvider()
        provider._token_expiry = time.time() - 100
        assert provider._is_expired() is True

        provider._token_expiry = time.time() + 100
        assert provider._is_expired() is False

    @patch("urllib.request.urlopen")
    def test_refresh_token_success(self, mock_urlopen):
        """Test successful token refresh."""
        mock_response = Mock()
        mock_response.read.return_value = b'{"access_token": "refreshed", "expires_in": 3600}'
        mock_urlopen.return_value.__enter__ = Mock(return_value=mock_response)
        mock_urlopen.return_value.__exit__ = Mock(return_value=False)

        config = AuthConfig(
            type=AuthType.OAUTH2,
            client_id="client",
            client_secret="secret",
            token_endpoint="http://auth.example.com/token",
        )
        provider = AuthProvider(config)

        result = provider.refresh_token()
        assert result is True
        assert provider._current_token == "refreshed"

    def test_refresh_token_non_oauth2(self):
        """Test refresh_token returns True for non-OAuth2."""
        provider = AuthProvider()
        result = provider.refresh_token()
        assert result is True

    @patch("urllib.request.urlopen")
    def test_refresh_token_failure(self, mock_urlopen):
        """Test token refresh failure."""
        mock_urlopen.side_effect = Exception("Network error")

        config = AuthConfig(
            type=AuthType.OAUTH2,
            client_id="client",
            client_secret="secret",
            token_endpoint="http://auth.example.com/token",
        )

        with patch(
            "volley.core.AuthProvider._fetch_token", side_effect=Exception("Network error")
        ):
            provider = AuthProvider.__new__(AuthProvider)
            provider.config = config
            provider._current_token = ""
            provider._token_expiry = 0.0
            result = provider.refresh_token()
            assert result is False

    @patch("urllib.request.urlopen")
    def test_fetch_token_with_expiry_buffer(self, mock_urlopen):
        """Test token expiry includes 60s buffer."""
        mock_response = Mock()
        mock_response.read.return_value = b'{"access_token": "token", "expires_in": 3600}'
        mock_urlopen.return_value.__enter__ = Mock(return_value=mock_response)
        mock_urlopen.return_value.__exit__ = Mock(return_value=False)

        config = AuthConfig(
            type=AuthType.OAUTH2,
            client_id="client",
            client_secret="secret",
            token_endpoint="http://auth.example.com/token",
        )
        provider = AuthProvider(config)

        expected_expiry = time.time() + 3540  # 3600 - 60
        assert abs(provider._token_expiry - expected_expiry) < 5

    @patch("urllib.request.urlopen")
    def test_fetch_token_min_expiry(self, mock_urlopen):
        """Test token expiry has minimum of 1 second."""
        mock_response = Mock()
        mock_response.read.return_value = b'{"access_token": "token", "expires_in": 10}'
        mock_urlopen.return_value.__enter__ = Mock(return_value=mock_response)
        mock_urlopen.return_value.__exit__ = Mock(return_value=False)

        config = AuthConfig(
            type=AuthType.OAUTH2,
            client_id="client",
            client_secret="secret",
            token_endpoint="http://auth.example.com/token",
        )
        provider = AuthProvider(config)

        assert provider._token_expiry > time.time()
