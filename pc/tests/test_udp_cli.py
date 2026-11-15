"""Local loopback tests: no XIAO or Wi-Fi required."""
import socket
import threading
import unittest

from hikorobo_gcs.udp_cli import exchange, validate_command


class LocalUdpTests(unittest.TestCase):
    def test_validate_command(self):
        self.assertEqual(validate_command("PING"), b"PING\n")
        with self.assertRaises(ValueError):
            validate_command("PING\nGET_RATE")
        with self.assertRaises(ValueError):
            validate_command("あ")
        with self.assertRaises(ValueError):
            validate_command("X" * 96)

    def test_exchange_loopback(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            server.bind(("127.0.0.1", 0))
            port = server.getsockname()[1]
            received = []

            def reply_once():
                data, peer = server.recvfrom(512)
                received.append(data)
                server.sendto(b"OK PONG\n", peer)

            worker = threading.Thread(target=reply_once, daemon=True)
            worker.start()
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
                response, rtt = exchange(client, ("127.0.0.1", port), "PING")
            worker.join(timeout=2)
            self.assertEqual(response, "OK PONG")
            self.assertEqual(received, [b"PING\n"])
            self.assertGreaterEqual(rtt, 0)

    def test_timeout(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            server.bind(("127.0.0.1", 0))
            port = server.getsockname()[1]
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
                with self.assertRaises(TimeoutError):
                    exchange(client, ("127.0.0.1", port), "PING", timeout=0.04)


if __name__ == "__main__":
    unittest.main()
