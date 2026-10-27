"""Offline tests; all credentials are synthetic and all PNGs stay in temporary dirs."""
import contextlib
import io
import os
from pathlib import Path
import pty
import select
import struct
import subprocess
import sys
import tempfile
import termios
import time
import unittest
from unittest.mock import patch
from urllib.parse import parse_qs, urlsplit
import zlib

import network_admin_qr as qr


class AdminQrTests(unittest.TestCase):
    def test_fragment_and_local_targets(self):
        token = "a" * 64
        parsed = urlsplit(qr.admin_url(token))
        self.assertEqual(parsed.query, "")
        self.assertEqual(parsed.path, "/network")
        self.assertEqual(parse_qs(parsed.fragment), {"admin": [token]})
        self.assertEqual(qr.admin_url(token.upper(), "http://192.168.1.20/network"),
                         "http://192.168.1.20/network#admin=" + token)
        self.assertIn("BalanceBot.local", qr.admin_url(token, "http://BalanceBot.local/network"))
        for target in ("https://192.168.4.1/network", "http://example.org/network", "http://8.8.8.8/network",
                       "http://192.168.4.1/network?admin=x", "http://192.168.4.1/network#admin=x",
                       "http://user:secret@192.168.4.1/network", "http://BalanceBot.local:0/network",
                       "http://BalanceBot.local:bad/network", "http://192.168.4.1/other"):
            with self.subTest(target=target), self.assertRaises(ValueError):
                qr.admin_url(token, target)
        for value in ("short", "g" * 64, "a" * 63, "a" * 65):
            with self.assertRaises(ValueError):
                qr.admin_url(value)

    def test_real_libqrencode_png_pixels_and_quiet_zone(self):
        matrix = qr.qr_matrix(qr.admin_url("b" * 64))
        self.assertGreaterEqual(len(matrix), 21)
        image = qr.png_bytes(matrix)
        self.assertEqual(image[:8], b"\x89PNG\r\n\x1a\n")
        offset, chunks = 8, {}
        while offset < len(image):
            length = struct.unpack(">I", image[offset:offset + 4])[0]
            kind = image[offset + 4:offset + 8]
            data = image[offset + 8:offset + 8 + length]
            crc = struct.unpack(">I", image[offset + 8 + length:offset + 12 + length])[0]
            self.assertEqual(crc, zlib.crc32(kind + data) & 0xffffffff)
            chunks[kind] = data
            offset += length + 12
        self.assertEqual(set(chunks), {b"IHDR", b"IDAT", b"IEND"})
        width, height = struct.unpack(">II", chunks[b"IHDR"][:8])
        self.assertEqual(width, (len(matrix) + 8) * 8)
        self.assertEqual(width, height)
        pixels = zlib.decompress(chunks[b"IDAT"])
        for y in range(height):
            row = pixels[y * (width + 1):(y + 1) * (width + 1)]
            self.assertEqual(row[0], 0)
            for x in range(width):
                mx, my = x // 8 - 4, y // 8 - 4
                black = 0 <= mx < len(matrix) and 0 <= my < len(matrix) and matrix[my][mx]
                self.assertEqual(row[x + 1], 0 if black else 255)

    def test_private_output_no_overwrite_or_repository_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "admin.png"
            qr.write_private(output, b"test")
            self.assertEqual(output.stat().st_mode & 0o777, 0o600)
            with self.assertRaises(FileExistsError):
                qr.write_private(output, b"overwrite")
            link = Path(directory) / "link.png"
            link.symlink_to(output)
            with self.assertRaises(FileExistsError):
                qr.write_private(link, b"overwrite")
            self.assertEqual(output.read_bytes(), b"test")
        with self.assertRaises(ValueError):
            qr.write_private(Path(__file__).resolve().parents[1] / "data" / "admin-test.png", b"test")

    def test_cli_no_secret_in_arguments_stdout_stderr_or_errors(self):
        token = "c" * 64
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "admin.png"
            stdout, stderr = io.StringIO(), io.StringIO()
            with patch.object(qr.getpass, "getpass", return_value=token), contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                self.assertEqual(qr.main(["--output", str(output)]), 0)
                self.assertEqual(qr.main(["--output", str(output)]), 1)
            self.assertTrue(output.is_file())
            self.assertNotIn(token, stdout.getvalue() + stderr.getvalue())
            self.assertNotIn("#admin=", stdout.getvalue() + stderr.getvalue())
            rejected = io.StringIO()
            with contextlib.redirect_stderr(rejected), self.assertRaises(SystemExit):
                qr.main(["--output", str(output), "--token", token])
            self.assertNotIn(token, rejected.getvalue())

    def test_actual_terminal_prompt_disables_echo(self):
        token = "d" * 64
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "admin.png"
            master, slave = pty.openpty()
            process = subprocess.Popen([sys.executable, str(Path(qr.__file__).resolve()), "--output", str(output)],
                                       stdin=slave, stdout=slave, stderr=slave)
            captured = b""
            try:
                deadline = time.monotonic() + 10
                while b"sem eco" not in captured and time.monotonic() < deadline:
                    if select.select([master], [], [], 0.1)[0]:
                        captured += os.read(master, 4096)
                self.assertIn(b"sem eco", captured)
                self.assertFalse(termios.tcgetattr(slave)[3] & termios.ECHO)
                os.write(master, (token + "\n").encode())
                process.wait(timeout=10)
                while select.select([master], [], [], 0.1)[0]:
                    captured += os.read(master, 4096)
                self.assertEqual(process.returncode, 0)
                self.assertNotIn(token.encode(), captured)
                self.assertTrue(output.exists())
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
                os.close(master)
                os.close(slave)


if __name__ == "__main__":
    unittest.main()
