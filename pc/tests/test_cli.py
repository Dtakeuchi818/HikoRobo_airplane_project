import unittest

from hikorobo_gcs.serial_cli import exchange


class FakeSerial:
    def __init__(self, responses):
        self.responses = list(responses)
        self.sent = b""

    def write(self, data):
        self.sent += data
        return len(data)

    def flush(self):
        pass

    def readline(self):
        return self.responses.pop(0) if self.responses else b""


class TestCommandExchange(unittest.TestCase):
    def test_ping(self):
        port = FakeSerial([b"boot text\n", b"OK PONG\n"])
        self.assertEqual(exchange(port, "PING", timeout=0.1), "OK PONG")
        self.assertEqual(port.sent, b"PING\n")

    def test_bad_request(self):
        port = FakeSerial([b"ERR UNKNOWN_COMMAND\n"])
        self.assertEqual(exchange(port, "INVALID", timeout=0.1), "ERR UNKNOWN_COMMAND")

    def test_rejects_empty(self):
        with self.assertRaises(ValueError):
            exchange(FakeSerial([]), "")


if __name__ == "__main__":
    unittest.main()
