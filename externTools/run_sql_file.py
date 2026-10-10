#!/usr/bin/env python3
"""Execute SQL statements from a file through the MiniOB obclient."""

import argparse
import errno
import fcntl
import os
import pty
import select
import signal
import socket
import sys
import termios
import time


PROMPT = b"miniob > "


class Obclient:
    def __init__(self, client_path, socket_path, timeout):
        self.client_path = client_path
        self.socket_path = socket_path
        self.timeout = timeout
        self.pid = None
        self.master_fd = None

    def __enter__(self):
        self.master_fd, slave_fd = pty.openpty()
        self.pid = os.fork()
        if self.pid == 0:
            try:
                os.setsid()
                fcntl.ioctl(slave_fd, termios.TIOCSCTTY, 0)
                os.dup2(slave_fd, 0)
                os.dup2(slave_fd, 1)
                os.dup2(slave_fd, 2)
                os.close(self.master_fd)
                os.close(slave_fd)
                os.execv(self.client_path, [self.client_path, "-s", self.socket_path])
            except Exception as exc:
                print(f"failed to start obclient: {exc}", file=sys.stderr)
                os._exit(127)

        os.close(slave_fd)
        try:
            self._read_until_prompt()
        except Exception:
            self.close()
            raise
        return self

    def __exit__(self, exc_type, exc_value, exc_tb):
        self.close()

    def _read_until_prompt(self):
        data = bytearray()
        deadline = None if self.timeout == 0 else time.monotonic() + self.timeout
        while True:
            if deadline is None:
                remaining = None
            else:
                remaining = deadline - time.monotonic()
            if remaining is not None and remaining <= 0:
                preview = bytes(data).decode("utf-8", errors="replace")
                raise TimeoutError(
                    "timed out waiting for obclient prompt; "
                    f"received: {preview!r}"
                )

            ready, _, _ = select.select([self.master_fd], [], [], remaining)
            if not ready:
                preview = bytes(data).decode("utf-8", errors="replace")
                raise TimeoutError(
                    "timed out waiting for obclient prompt; "
                    f"received: {preview!r}"
                )

            try:
                chunk = os.read(self.master_fd, 4096)
            except OSError as exc:
                if exc.errno == errno.EIO:
                    raise RuntimeError("obclient closed the terminal before returning a prompt") from exc
                raise RuntimeError(f"failed to read obclient output: {exc}") from exc
            if not chunk:
                raise RuntimeError("obclient closed the terminal")

            data.extend(chunk)
            prompt_index = data.rfind(PROMPT)
            if prompt_index != -1:
                return bytes(data[:prompt_index])

    def execute(self, sql):
        command = sql.encode("utf-8") + b"\r"
        os.write(self.master_fd, command)
        output = self._read_until_prompt().decode("utf-8", errors="replace")
        output = output.replace("\r\n", "\n").replace("\r", "\n")

        # The pseudo-terminal echoes the command before obclient prints the
        # server response. Remove that echo while preserving the response.
        echoed_command = sql
        echo_index = output.find(echoed_command)
        if echo_index != -1:
            output = output[echo_index + len(echoed_command):]
        return output.rstrip("\n")

    def close(self):
        if self.master_fd is not None:
            try:
                os.write(self.master_fd, b"exit\n")
                self._read_until_prompt()
            except (OSError, RuntimeError, TimeoutError):
                pass
            try:
                os.close(self.master_fd)
            except OSError:
                pass
            self.master_fd = None

        if self.pid is not None:
            try:
                _, status = os.waitpid(self.pid, os.WNOHANG)
                if status == 0:
                    os.kill(self.pid, signal.SIGTERM)
                    os.waitpid(self.pid, 0)
            except ChildProcessError:
                pass
            except ProcessLookupError:
                pass
            self.pid = None


def read_statements(sql_file):
    with open(sql_file, "r", encoding="utf-8") as file:
        for line_number, line in enumerate(file, 1):
            statement = line.strip()
            if not statement or statement.startswith("--") or statement.startswith("#"):
                continue
            yield line_number, statement


def wait_for_socket(socket_path, timeout):
    deadline = time.monotonic() + timeout
    while True:
        client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            client.settimeout(min(1.0, max(0.1, deadline - time.monotonic())))
            client.connect(socket_path)
            return
        except (FileNotFoundError, ConnectionRefusedError, TimeoutError, OSError):
            if time.monotonic() >= deadline:
                raise TimeoutError(
                    f"MiniOB socket is not accepting connections: {socket_path}"
                )
            time.sleep(0.2)
        finally:
            client.close()


def parse_args():
    parser = argparse.ArgumentParser(
        description="Execute one SQL statement per non-comment line through obclient."
    )
    parser.add_argument(
        "sql_file",
        help="SQL file, for example test/case/debug/date.sql",
    )
    parser.add_argument(
        "-c",
        "--client",
        default="./build_debug/bin/obclient",
        help="path to obclient (default: ./build_debug/bin/obclient)",
    )
    parser.add_argument(
        "-s",
        "--socket",
        default="./miniob/miniob.sock",
        help="MiniOB Unix socket (default: ./miniob/miniob.sock)",
    )
    parser.add_argument(
        "-t",
        "--timeout",
        type=float,
        default=20,
        help="seconds to wait for each response; 0 means unlimited (default: 20)",
    )
    parser.add_argument(
        "--wait",
        type=float,
        default=10,
        help="seconds to wait for the socket to accept connections (default: 10)",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    client_path = os.path.abspath(args.client)
    socket_path = os.path.abspath(args.socket)
    sql_file = os.path.abspath(args.sql_file)

    if not os.path.isfile(client_path):
        print(f"obclient does not exist: {client_path}", file=sys.stderr)
        return 2
    if not os.path.isfile(sql_file):
        print(f"SQL file does not exist: {sql_file}", file=sys.stderr)
        return 2

    try:
        wait_for_socket(socket_path, args.wait)
        with Obclient(client_path, socket_path, args.timeout) as client:
            for line_number, statement in read_statements(sql_file):
                print(f"[{line_number}] SQL> {statement}")
                try:
                    output = client.execute(statement)
                except Exception as exc:
                    print(f"[{line_number}] ERROR: {exc}", file=sys.stderr)
                    return 1
                if output:
                    print(output)
    except Exception as exc:
        print(f"failed to start obclient: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
