#!/usr/bin/env python3
"""Protocol-faithful virtual Millennium Beta/Alpha pair for QEMU."""

import argparse
import asyncio
import contextlib
import json
import os
import sys
import time

SOF, VERSION, MAX_PAYLOAD = 0x7E, 2, 240
ACK, HELLO = 0x01, 0x02
DISPLAY, COIN_CONTROL, COIN_PROGRAM, COIN_VERIFY = 0x10, 0x11, 0x12, 0x13
KEEPALIVE, IDENTITY = 0x14, 0x15
KEY, HOOK, CARD, COIN, DIAGNOSTIC, HEARTBEAT, OPERATION = range(0x20, 0x27)
CRITICAL = {DISPLAY, COIN_CONTROL, COIN_PROGRAM, COIN_VERIFY}


class PhoneHardware:
    """Behavioral Alpha/Beta and peripheral topology around the real wire protocol."""

    def __init__(self, display_file):
        self.display_file = display_file
        self.alpha_resets = 0
        self.beta_resets = 0
        self.i2c_available = True
        self.i2c_drops = 0
        self.hook = "down"
        self.last_key = None
        self.last_card = None
        self.vfd_text = ""
        self.vfd_control = 0
        self.coin_gate = "closed"
        self.coin_jammed = False
        self.coin_eeprom = bytearray(256)
        self.coin_programmed = False

    def snapshot(self):
        return {
            "arduinos": {
                "alpha": {"role": "keypad", "resets": self.alpha_resets,
                          "i2c_drops": self.i2c_drops},
                "beta": {"role": "display", "resets": self.beta_resets,
                         "i2c_available": self.i2c_available},
            },
            "peripherals": {
                "hook": self.hook,
                "keypad_last_key": self.last_key,
                "card_reader_last_token": self.last_card,
                "vfd": {"display": self.vfd_text, "control": self.vfd_control},
                "coin_validator": {"gate": self.coin_gate, "jammed": self.coin_jammed,
                                   "programmed": self.coin_programmed},
            },
            "updated": time.time(),
        }

    def persist(self):
        with open(self.display_file, "w", encoding="utf-8") as output:
            json.dump(self.snapshot(), output, indent=2, sort_keys=True)

    def alpha_event(self, command, argument):
        """Model physical input -> Alpha scan/decode -> I2C -> Beta forwarding."""
        if command == "key":
            if len(argument) != 1 or argument not in "0123456789*#ABCDEFGHIJKLMNOP":
                raise ValueError("key must be one key from the 4x7 matrix")
            self.last_key = argument
            event = (KEY, argument.encode())
        elif command == "hook":
            if argument.lower() not in ("up", "u", "down", "d"):
                raise ValueError("hook must be up or down")
            self.hook = "up" if argument.lower() in ("up", "u") else "down"
            event = (HOOK, b"U" if self.hook == "up" else b"D")
        elif command == "card":
            if not argument or len(argument) > 107:
                raise ValueError("card token must contain 1..107 characters")
            self.last_card = argument
            event = (CARD, argument.encode())
        elif command == "coin":
            if argument not in ("5", "10", "25"):
                raise ValueError("coin must be 5, 10, or 25 cents")
            if self.coin_jammed:
                raise ValueError("coin validator is jammed")
            event = (COIN, {"5": b"6", "10": b"7", "25": b"8"}[argument])
        else:
            raise ValueError("unknown peripheral")
        if not self.i2c_available and command != "coin":
            self.i2c_drops += 1
            self.persist()
            raise ValueError("Alpha-to-Beta I2C link is down; event counted as dropped")
        self.persist()
        return event

    def beta_command(self, message_type, payload):
        """Model Beta command routing to the VFD and coin validator."""
        if message_type == DISPLAY:
            self.vfd_text = payload.decode("utf-8", "replace")
            self.persist()
            print(f"VFD {self.vfd_text}", flush=True)
        elif message_type == COIN_CONTROL and payload:
            if payload[0] == ord("c"):
                self.coin_gate = "closed"
            elif payload[0] == ord("z"):
                self.coin_gate = "open"
            elif payload[0] == ord("@"):
                self.coin_jammed = False
            self.persist()
            print(f"COIN VALIDATOR control={payload.hex()}", flush=True)
        elif message_type == COIN_PROGRAM:
            self.coin_programmed = True
            self.persist()
        elif message_type == COIN_VERIFY:
            self.persist()

    def manage(self, command, argument):
        if command == "fault":
            fields = argument.lower().split()
            if fields == ["i2c", "down"]:
                self.i2c_available = False
            elif fields == ["i2c", "up"]:
                self.i2c_available = True
            elif fields == ["coin", "jam"]:
                self.coin_jammed = True
            elif fields == ["coin", "clear"]:
                self.coin_jammed = False
            else:
                raise ValueError("fault: i2c <up|down> | coin <jam|clear>")
        elif command == "reset":
            if argument.lower() == "alpha":
                self.alpha_resets += 1
                self.i2c_drops = 0
            elif argument.lower() == "beta":
                self.beta_resets += 1
                self.vfd_text = ""
                self.coin_gate = "closed"
            else:
                raise ValueError("reset must target alpha or beta")
        else:
            raise ValueError("unknown management command")
        self.persist()


def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode(message_type, sequence, payload=b""):
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload exceeds MCU protocol limit")
    body = bytes((VERSION, len(payload), message_type, sequence)) + payload
    checksum = crc16(body)
    return bytes((SOF,)) + body + checksum.to_bytes(2, "big")


class Decoder:
    def __init__(self):
        self.buffer = bytearray()

    def feed(self, data):
        self.buffer.extend(data)
        frames = []
        while self.buffer:
            try:
                start = self.buffer.index(SOF)
            except ValueError:
                self.buffer.clear()
                break
            del self.buffer[:start]
            if len(self.buffer) < 3:
                break
            length = self.buffer[2]
            total = length + 7
            if self.buffer[1] != VERSION or length > MAX_PAYLOAD:
                del self.buffer[0]
                continue
            if len(self.buffer) < total:
                break
            raw = bytes(self.buffer[:total])
            del self.buffer[:total]
            if crc16(raw[1:-2]) != int.from_bytes(raw[-2:], "big"):
                continue
            frames.append((raw[3], raw[4], raw[5:-2]))
        return frames


class VirtualMCU:
    def __init__(self, serial_socket, control_socket, display_file):
        self.serial_socket = serial_socket
        self.control_socket = control_socket
        self.display_file = display_file
        self.writer = None
        self.sequence = 0
        self.connected = asyncio.Event()
        self.hardware = PhoneHardware(display_file)
        self.hardware.persist()

    async def send(self, message_type, payload=b""):
        await self.connected.wait()
        self.writer.write(encode(message_type, self.sequence, payload))
        self.sequence = (self.sequence + 1) & 0xFF
        await self.writer.drain()

    async def serial_loop(self):
        decoder = Decoder()
        while True:
            try:
                reader, writer = await asyncio.open_unix_connection(self.serial_socket)
                self.writer = writer
                self.connected.set()
                print("virtual MCU connected", flush=True)
                while data := await reader.read(4096):
                    for message_type, sequence, payload in decoder.feed(data):
                        if message_type == HELLO:
                            writer.write(encode(HELLO, self.sequence, bytes((VERSION, VERSION))))
                            self.sequence = (self.sequence + 1) & 0xFF
                        elif message_type in CRITICAL:
                            writer.write(encode(ACK, self.sequence, bytes((sequence, 0))))
                            self.sequence = (self.sequence + 1) & 0xFF
                        self.hardware.beta_command(message_type, payload)
                        await writer.drain()
            except (FileNotFoundError, ConnectionRefusedError, ConnectionResetError, BrokenPipeError):
                pass
            finally:
                self.connected.clear()
                if self.writer:
                    self.writer.close()
                    with contextlib.suppress(Exception):
                        await self.writer.wait_closed()
                self.writer = None
            await asyncio.sleep(0.5)

    async def heartbeat_loop(self):
        while True:
            await asyncio.sleep(5)
            if self.connected.is_set():
                with contextlib.suppress(ConnectionError):
                    await self.send(HEARTBEAT)

    async def control_client(self, reader, writer):
        try:
            while line := await reader.readline():
                try:
                    fields = line.decode().strip().split(maxsplit=1)
                    command = fields[0].lower()
                    argument = fields[1] if len(fields) == 2 else ""
                    if command in ("key", "hook", "card", "coin"):
                        await self.send(*self.hardware.alpha_event(command, argument))
                        writer.write(b"ok\n")
                    elif command in ("fault", "reset"):
                        self.hardware.manage(command, argument)
                        writer.write(b"ok\n")
                    elif command == "status":
                        writer.write((json.dumps(self.hardware.snapshot(), sort_keys=True) + "\n").encode())
                    else:
                        raise ValueError("use: key|hook|coin|card|fault|reset|status")
                except Exception as error:
                    writer.write(f"error: {error}\n".encode())
                await writer.drain()
        finally:
            writer.close()
            await writer.wait_closed()

    async def run(self):
        with contextlib.suppress(FileNotFoundError):
            os.unlink(self.control_socket)
        server = await asyncio.start_unix_server(self.control_client, path=self.control_socket)
        os.chmod(self.control_socket, 0o600)
        async with server:
            await asyncio.gather(server.serve_forever(), self.serial_loop(), self.heartbeat_loop())


async def send_control(path, command):
    reader, writer = await asyncio.open_unix_connection(path)
    writer.write((command + "\n").encode())
    await writer.drain()
    response = await reader.readline()
    writer.close()
    await writer.wait_closed()
    print(response.decode().strip())
    return 0 if response == b"ok\n" else 1


def main():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="action", required=True)
    serve = subparsers.add_parser("serve")
    serve.add_argument("--serial", required=True)
    serve.add_argument("--control", required=True)
    serve.add_argument("--display", required=True)
    serve.add_argument("--daemonize", action="store_true")
    serve.add_argument("--pid-file")
    serve.add_argument("--log-file")
    send = subparsers.add_parser("send")
    send.add_argument("--control", required=True)
    send.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.action == "send":
        return asyncio.run(send_control(args.control, " ".join(args.command)))
    if args.daemonize:
        if not args.pid_file or not args.log_file:
            parser.error("--daemonize requires --pid-file and --log-file")
        args.serial = os.path.abspath(args.serial)
        args.control = os.path.abspath(args.control)
        args.display = os.path.abspath(args.display)
        args.pid_file = os.path.abspath(args.pid_file)
        args.log_file = os.path.abspath(args.log_file)
        if os.fork() != 0:
            return 0
        os.setsid()
        if os.fork() != 0:
            os._exit(0)
        os.chdir("/")
        os.umask(0o077)
        devnull = os.open(os.devnull, os.O_RDONLY)
        logfile = os.open(args.log_file, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        os.dup2(devnull, 0)
        os.dup2(logfile, 1)
        os.dup2(logfile, 2)
        if args.pid_file:
            with open(args.pid_file, "w", encoding="ascii") as output:
                output.write(str(os.getpid()))
    try:
        asyncio.run(VirtualMCU(args.serial, args.control, args.display).run())
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
