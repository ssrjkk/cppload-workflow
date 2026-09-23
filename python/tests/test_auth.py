"""Tests for auth providers and config dataclasses."""

from volley import (
    AuthConfig,
    AuthProvider,
    AuthType,
    HttpRequest,
    PoolConfig,
    VaultConfig,
    TraceConfig,
)


class TestAuthProvider:
    def test_api_key_header(self):
        p = AuthProvider(AuthConfig(type=AuthType.API_KEY, api_key="secret-key"))
        headers = {}
        p.apply_headers(headers)
        assert headers["X-API-Key"] == "secret-key"

    def test_bearer_token_header(self):
        p = AuthProvider(AuthConfig(type=AuthType.BEARER_TOKEN, token="tok123"))
        headers = {"Accept": "application/json"}
        p.apply_headers(headers)
        assert headers["Authorization"] == "Bearer tok123"
        assert headers["Accept"] == "application/json"

    def test_none_does_not_touch_headers(self):
        p = AuthProvider()
        headers = {"a": "b"}
        p.apply_headers(headers)
        assert headers == {"a": "b"}

    def test_refresh_token_non_oauth2_is_noop(self):
        p = AuthProvider(AuthConfig(type=AuthType.API_KEY, api_key="k"))
        assert p.refresh_token() is True


class TestConfigDataclasses:
    def test_auth_config_defaults(self):
        c = AuthConfig()
        assert c.type == AuthType.NONE
        assert c.api_key == ""

    def test_pool_config_defaults(self):
        c = PoolConfig()
        assert c.min_connections == 5
        assert c.keep_alive is True

    def test_vault_config_defaults(self):
        c = VaultConfig()
        assert c.engine_path == "secret"

    def test_trace_config_defaults(self):
        c = TraceConfig()
        assert c.sample_rate == 1.0

    def test_http_request_defaults(self):
        r = HttpRequest()
        assert r.method == "GET"
        assert r.target == "/"
        assert r.host == "localhost"
        assert r.port == "80"


class TestHttpRequest:
    def test_custom_fields(self):
        r = HttpRequest(method="POST", target="/api", body='{"a":1}')
        assert r.method == "POST"
        assert r.body == '{"a":1}'
