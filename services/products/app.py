# @author ssrjkk | cppload
import json
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(os.environ.get("PORT", "8080"))

PRODUCTS = [
    {"id": 1, "name": "Laptop Pro", "price": 1999.99, "sku": "LAP-1001"},
    {"id": 2, "name": "Wireless Mouse", "price": 49.99, "sku": "ACC-2002"},
    {"id": 3, "name": "Mechanical Keyboard", "price": 129.99, "sku": "ACC-2003"},
]

requests_total = 0


class Handler(BaseHTTPRequestHandler):
    def _json(self, payload, code=200):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _text(self, body, content_type="text/plain; version=0.0.4"):
        data = body.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        global requests_total
        requests_total += 1
        if self.path == "/health":
            return self._json({"status": "ok", "service": "product-service"})
        if self.path == "/products":
            return self._json({"data": PRODUCTS})
        if self.path == "/metrics":
            return self._text(
                "product_service_requests_total {}\n"
                "product_service_products_total {}\n".format(
                    requests_total, len(PRODUCTS)
                )
            )
        return self._json({"error": "not found"}, 404)

    def log_message(self, fmt, *args):
        return


if __name__ == "__main__":
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
