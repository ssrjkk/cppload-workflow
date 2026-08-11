# @author ssrjkk | cppload
import json
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(os.environ.get("PORT", "8080"))

ORDERS = [
    {"id": 101, "product_id": 1, "qty": 2, "status": "shipped"},
    {"id": 102, "product_id": 2, "qty": 5, "status": "processing"},
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
            return self._json({"status": "ok", "service": "order-service"})
        if self.path == "/orders":
            return self._json({"data": ORDERS})
        if self.path == "/metrics":
            return self._text(
                "order_service_requests_total {}\n"
                "order_service_orders_total {}\n".format(
                    requests_total, len(ORDERS)
                )
            )
        return self._json({"error": "not found"}, 404)

    def log_message(self, fmt, *args):
        return


if __name__ == "__main__":
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
