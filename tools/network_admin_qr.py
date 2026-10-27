#!/usr/bin/env python3
"""Offline administrative QR. Token is prompted without echo, never a command argument."""
import argparse
import ctypes
import ctypes.util
import getpass
import ipaddress
import os
from pathlib import Path
import re
import struct
import sys
from urllib.parse import urlencode, urlsplit, urlunsplit
import warnings
import zlib


def admin_url(token, base_url="http://192.168.4.1/network"):
    """Validate without including the credential in any error message."""
    token = token.strip().lower()
    if not re.fullmatch(r"[0-9a-f]{64}", token):
        raise ValueError("Token deve conter exatamente 64 caracteres hexadecimais.")
    try:
        parts = urlsplit(base_url)
        port = parts.port
        host = parts.hostname or ""
    except ValueError:
        raise ValueError("URL local inválida.") from None
    try:
        address = ipaddress.ip_address(host)
        local = address.version == 4 and address.is_private and not (address.is_loopback or address.is_unspecified or address.is_multicast)
    except ValueError:
        local = host.lower() == "balancebot.local"
    if (parts.scheme != "http" or not local or parts.path != "/network" or
            parts.username is not None or parts.password is not None or parts.query or parts.fragment or
            any(ord(ch) <= 32 for ch in base_url) or (port is not None and not 1 <= port <= 65535)):
        raise ValueError("Use http://<IP privado>/network ou http://BalanceBot.local/network, sem query, fragmento ou credenciais.")
    return urlunsplit((parts.scheme, parts.netloc, parts.path, "", urlencode({"admin": token})))


class QRcode(ctypes.Structure):
    _fields_ = [("version", ctypes.c_int), ("width", ctypes.c_int),
                ("data", ctypes.POINTER(ctypes.c_ubyte))]


def qr_matrix(payload):
    library_name = ctypes.util.find_library("qrencode")
    if not library_name:
        raise RuntimeError("Biblioteca local ausente. No Fedora: sudo dnf install qrencode-libs")
    library = ctypes.CDLL(library_name)
    library.QRcode_encodeString8bit.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int]
    library.QRcode_encodeString8bit.restype = ctypes.POINTER(QRcode)
    library.QRcode_free.argtypes = [ctypes.POINTER(QRcode)]
    library.QRcode_free.restype = None
    # Version auto, correction Q (enum value 2), case-sensitive bytes.
    pointer = library.QRcode_encodeString8bit(payload.encode("utf-8"), 0, 2)
    if not pointer:
        raise RuntimeError("Não foi possível codificar o QR localmente.")
    try:
        width = pointer.contents.width
        if not 21 <= width <= 177:
            raise RuntimeError("Dimensões inesperadas do QR local.")
        return [list(pointer.contents.data[y * width + x] & 1 for x in range(width)) for y in range(width)]
    finally:
        library.QRcode_free(pointer)


def png_bytes(matrix, scale=8):
    """Grayscale PNG, four-module quiet zone, no credential-bearing text/metadata."""
    width = len(matrix)
    if not width or any(len(row) != width for row in matrix) or not 2 <= scale <= 16:
        raise ValueError("Matriz ou escala de QR inválida.")
    dimension = (width + 8) * scale
    white = b"\xff" * dimension
    rows = [white] * (4 * scale)
    for row in matrix:
        pixels = b"\xff" * (4 * scale) + b"".join((b"\x00" if cell else b"\xff") * scale for cell in row) + b"\xff" * (4 * scale)
        rows.extend([pixels] * scale)
    rows.extend([white] * (4 * scale))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", dimension, dimension, 8, 0, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(b"".join(b"\0" + row for row in rows))) + chunk(b"IEND", b""))


def write_private(path, content):
    path = Path(path).expanduser().absolute()
    project = Path(__file__).resolve().parent.parent
    if path.resolve().is_relative_to(project):
        raise ValueError("Salve o QR fora do repositório; nunca em data/ ou em arquivos versionados.")
    # Refuse overwrite and symlinks. No token in filename, stdout or PNG metadata.
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(content)
    except BaseException:
        path.unlink(missing_ok=True)
        raise


class PrivateArgumentParser(argparse.ArgumentParser):
    def error(self, message):
        # argparse's default error echoes unknown arguments, potentially an accidental --token.
        self.print_usage(sys.stderr)
        self.exit(2, "Argumentos inválidos. Use --output e opcionalmente --base-url; o token deve ser digitado no prompt, nunca em argumentos.\n")


def main(argv=None):
    parser = PrivateArgumentParser(description="Gera QR administrativo offline com token deste boot, digitado sem eco. O QR é uma credencial sensível.")
    parser.add_argument("--output", type=Path, required=True, help="PNG fora do repositório; arquivo novo, modo 0600")
    parser.add_argument("--base-url", default="http://192.168.4.1/network", help="URL local do robô, sem query/fragmento")
    arguments = parser.parse_args(argv)
    try:
        # Fail closed instead of falling back to an echoed password on a non-TTY.
        with warnings.catch_warnings():
            warnings.simplefilter("error", getpass.GetPassWarning)
            token = getpass.getpass("Token NET ADMIN deste boot (sem eco): ")
        payload = admin_url(token, arguments.base_url)
        matrix = qr_matrix(payload)
        write_private(arguments.output, png_bytes(matrix))
    except (ValueError, OSError, RuntimeError, getpass.GetPassWarning, EOFError):
        # Deliberately omit exception details/arguments: they may include sensitive input.
        print("Não foi possível gerar o QR. Verifique token, TTY, biblioteca qrencode-libs e destino novo fora do repositório.", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nOperação cancelada.", file=sys.stderr)
        return 130
    print("QR local criado (0600). Abra o PNG no seu computador e leia com o Android. Não compartilhe nem publique; HTTP permite interceptação. Use rede confiável e gere outro QR após reboot.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
