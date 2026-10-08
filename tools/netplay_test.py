"""Synchronize real SDL peers, including a lossy/reordering UDP relay."""
from pathlib import Path
import heapq
import json
import socket
import subprocess
import sys
import tempfile
import threading
import time


class Relay:
    def __init__(self, server_port):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(("127.0.0.1", 0))
        self.socket.settimeout(0.005)
        self.port = self.socket.getsockname()[1]
        self.server = ("127.0.0.1", server_port)
        self.client = None
        self.stop = threading.Event()
        self.seen = set()
        self.queue = []
        self.counter = self.dropped = self.duplicated = 0
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.stop.is_set():
            try:
                packet, sender = self.socket.recvfrom(2048)
            except (socket.timeout, ConnectionResetError):
                packet = None
            if packet:
                if sender == self.server:
                    destination, direction = self.client, "server"
                else:
                    self.client = sender
                    destination, direction = self.server, "client"
                if destination:
                    seq = int.from_bytes(packet[1:3], "little") if packet[0] in (1, 2) else 0
                    key = (direction, packet[0], seq)
                    first = key not in self.seen
                    self.seen.add(key)
                    if first and (packet[0] == 4 or packet[0] == 1 and seq % 7 == 0
                                  or packet[0] == 2 and seq % 5 == 0):
                        self.dropped += 1
                    else:
                        delay = (seq % 4) * 0.017 if packet[0] in (1, 2) else 0
                        self.schedule(packet, destination, delay)
                        if packet[0] == 1 and seq % 11 == 0:
                            self.schedule(packet, destination, delay + 0.08)
                            self.duplicated += 1
            now = time.monotonic()
            while self.queue and self.queue[0][0] <= now:
                _, _, packet, destination = heapq.heappop(self.queue)
                self.socket.sendto(packet, destination)

    def schedule(self, packet, destination, delay):
        self.counter += 1
        heapq.heappush(self.queue, (time.monotonic() + delay, self.counter, packet, destination))

    def close(self):
        self.stop.set()
        self.thread.join(timeout=1)
        self.socket.close()


exe = Path(sys.argv[1]).resolve()
results = []
with tempfile.TemporaryDirectory(prefix="nesticle-net-") as directory:
    root = Path(directory)
    subprocess.run([sys.executable, str(Path(__file__).with_name("make_demo.py")),
                    str(root / "demo.nes"), "--two-players"], check=True)
    rom = bytearray((root / "demo.nes").read_bytes());rom[6] |= 2
    (root / "demo.nes").write_bytes(rom)
    battery = bytes([0x7f]) * 8192
    (root / "demo.sav").write_bytes(battery)
    for lossy in (False, True):
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
        probe.close()
        relay = Relay(port) if lossy else None
        label = "lossy" if lossy else "direct"
        with (root / "server.log").open("w") as server_log, (root / "client.log").open("w") as client_log:
            common = ["--headless", "--frames", "900", "--sync-frames", "120", "--rom-dir", str(root), "--save-dir", str(root), "--test-net-input"]
            server = subprocess.Popen([str(exe), str(root / "demo.nes"), *common,
                                       "--net-server", str(port), "--net-name", "Server",
                                       "--save-state", str(root / "server.sta")], stdout=server_log, stderr=server_log)
            client = None
            try:
                time.sleep(0.3)
                client = subprocess.Popen([str(exe), *common, "--net-connect", "127.0.0.1",
                                           str(relay.port if relay else port), "--net-name", "Client",
                                           "--save-state", str(root / "client.sta")], stdout=client_log, stderr=client_log)
                server_code = server.wait(timeout=25)
                client_code = client.wait(timeout=25)
            finally:
                for process in (server, client):
                    if process is not None and process.poll() is None:
                        process.kill()
                        process.wait()
                if relay:
                    relay.close()
        for name, code in (("server", server_code), ("client", client_code)):
            log = (root / f"{name}.log").read_text()
            if code or "net_frames=120" not in log:
                raise AssertionError(f"{label} {name} exit={code}\n{log}")
            results.append({"name": f"{label} {name}", "exit": code, "output": log})
        state = (root / "server.sta").read_bytes()
        assert state == (root / "client.sta").read_bytes(), "Two netplay peers diverged"
        assert state[0x2807] == 48, f"Player one input lost: sprite y={state[0x2807]}"
        assert state[0x2030] == 40, f"Player two input lost: count={state[0x2030]}"
        assert state[:8192] == bytes(8192), "Netplay loaded battery data"
        assert (root / "demo.sav").read_bytes() == battery, "Netplay wrote battery data on shutdown"
        if relay:
            assert relay.dropped > 5 and relay.duplicated > 0
            results.append({"name": "relay", "dropped": relay.dropped, "duplicated": relay.duplicated})
        print(f"PASS {label} UDP handshake, ROM load/reset, both controllers and 120 identical frames")
exe.with_name("netplay-validation.json").write_text(json.dumps(results, indent=2))
