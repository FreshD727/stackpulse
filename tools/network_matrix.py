"""Run two real native clients through deterministic UDP impairment proxies.

This exercises ENet retransmission over actual sockets, including connection and
snapshot traffic. It never opens a public listener or changes firewall settings.
"""
import argparse
import heapq
import json
import os
import random
import re
import socket
import subprocess
import threading
import time
from pathlib import Path


CASES = {
    "baseline": (0, 0, 0, 0),
    "latency": (45, 0, 0, 0),
    "jitter_reorder": (35, 30, 0, 0),
    "loss_duplicate": (20, 15, 0.05, 0.10),
    "combined": (50, 45, 0.08, 0.08),
}


class Proxy(threading.Thread):
    def __init__(self, server_port, profile, seed):
        super().__init__(daemon=True)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(0.002)
        self.port = self.sock.getsockname()[1]
        self.server = ("127.0.0.1", server_port)
        self.client = None
        self.profile = profile
        self.rng = random.Random(seed)
        self.stopped = threading.Event()
        self.stats = dict(received=0, dropped=0, duplicated=0, forwarded=0, max_queue=0)

    def run(self):
        pending = []
        serial = 0
        delay, jitter, loss, duplicate = self.profile
        while not self.stopped.is_set():
            try:
                data, sender = self.sock.recvfrom(65536)
                if sender == self.server:
                    target = self.client
                else:
                    self.client = sender
                    target = self.server
                self.stats["received"] += 1
                if self.rng.random() < loss:
                    self.stats["dropped"] += 1
                elif target:
                    copies = 2 if self.rng.random() < duplicate else 1
                    self.stats["duplicated"] += copies - 1
                    for _ in range(copies):
                        serial += 1
                        due = time.monotonic() + max(0, delay + self.rng.uniform(-jitter, jitter)) / 1000
                        heapq.heappush(pending, (due, serial, data, target))
                    self.stats["max_queue"] = max(self.stats["max_queue"], len(pending))
            except socket.timeout:
                pass
            except OSError:
                break
            now = time.monotonic()
            while pending and pending[0][0] <= now:
                _, _, data, target = heapq.heappop(pending)
                self.sock.sendto(data, target)
                self.stats["forwarded"] += 1
        self.sock.close()


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def run_case(args, name):
    directory = args.output / (args.mode + "-" + name)
    directory.mkdir(parents=True, exist_ok=True)
    port = free_port()
    server_log = (directory / "server.log").open("w")
    processes = []
    proxies = []
    logs = [server_log]
    flags = subprocess.CREATE_NO_WINDOW if hasattr(subprocess, "CREATE_NO_WINDOW") else 0
    try:
        suffix = ".exe" if os.name == "nt" else ""
        server = subprocess.Popen([str(args.bin / ("stackpulse_server" + suffix)), "--port", str(port),
                                   "--mode", args.mode], stdout=server_log, stderr=subprocess.STDOUT,
                                  creationflags=flags)
        processes.append(server)
        time.sleep(0.3)
        for player in range(2):
            proxy = Proxy(port, CASES[name], 727 + player)
            proxy.start()
            proxies.append(proxy)
            log = (directory / f"client-{player + 1}.log").open("w")
            logs.append(log)
            command = [str(args.bin / ("stackpulse_client" + suffix)), "--connect", "127.0.0.1",
                       "--port", str(proxy.port), "--mode", args.mode, "--online-bot",
                       "--online-exit-ticks", str(args.ticks), "--mute", "--data-dir",
                       str(directory / f"data-{player + 1}")]
            processes.append(subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                              creationflags=flags))
        deadline = time.monotonic() + args.timeout
        failure = None
        while any(process.poll() is None for process in processes[1:]):
            if time.monotonic() > deadline:
                failure = f"native clients exceeded {args.timeout}s"
                break
            if server.poll() is not None:
                failure = f"server exited early ({server.returncode})"
                break
            time.sleep(0.1)
        for log in logs:
            log.flush()
        client_text = [(directory / f"client-{i}.log").read_text() for i in (1, 2)]
        # Native bot emits authoritative end state, not its speculative board hash.
        pattern = r"AUTHORITATIVE_RESULT tick=(\d+) result=(winner|draw) hash=([0-9a-fA-F]+)"
        terminal = [re.findall(pattern, text) for text in client_text]
        server_terminal = re.findall(pattern, (directory / "server.log").read_text())
        result = dict(case=name, mode=args.mode, exits=[p.returncode for p in processes[1:]],
                      failure=failure,
                      terminal=[values[-1] if values else None for values in terminal],
                      server_terminal=server_terminal[-1] if server_terminal else None,
                      proxies=[proxy.stats for proxy in proxies])
        result["passed"] = (failure is None and result["exits"] == [0, 0] and all(result["terminal"])
                            and bool(result["server_terminal"])
                            and all(value == result["server_terminal"] for value in result["terminal"]))
        (directory / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result), flush=True)
        return result["passed"]
    finally:
        for proxy in proxies:
            proxy.stopped.set()
        for proxy in proxies:
            proxy.join(timeout=2)
        for process in processes:
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        for log in logs:
            log.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("out/network-matrix"))
    parser.add_argument("--case", choices=["all", *CASES], default="all")
    parser.add_argument("--mode", choices=["fusion", "modern"], default="modern")
    parser.add_argument("--ticks", type=int, default=2400)
    parser.add_argument("--timeout", type=int, default=90)
    args = parser.parse_args()
    cases = CASES if args.case == "all" else [args.case]
    passed = [run_case(args, name) for name in cases]
    return 0 if all(passed) else 1


if __name__ == "__main__":
    raise SystemExit(main())
