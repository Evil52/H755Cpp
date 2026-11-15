import argparse
import ipaddress
import socket
import time


def receive_exact(peer, expected):
    received = bytearray()
    while len(received) < len(expected):
        part = peer.recv(len(expected) - len(received))
        if not part:
            raise RuntimeError('TCP closed before complete echo')
        received.extend(part)
    if received != expected:
        raise RuntimeError('TCP payload mismatch')


def main():
    parser = argparse.ArgumentParser(description='Bounded lwIP Ethernet HIL load probe')
    parser.add_argument('address', type=ipaddress.IPv4Address)
    args = parser.parse_args()
    address = str(args.address)
    started = time.monotonic()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
        peer.settimeout(2)
        for sequence in range(256):
            length = (4, 64, 512)[sequence % 3]
            payload = sequence.to_bytes(4, 'big') + bytes(
                (sequence + offset) & 255 for offset in range(length - 4)
            )
            peer.sendto(payload, (address, 7777))
            reply, source = peer.recvfrom(1536)
            if reply != payload or source != (address, 7777):
                raise RuntimeError(f'UDP echo mismatch at sequence {sequence}')
    for sequence in range(16):
        payload = sequence.to_bytes(4, 'big') + bytes(
            (sequence + offset) & 255 for offset in range(508)
        )
        with socket.create_connection((address, 7777), timeout=3) as peer:
            peer.settimeout(3)
            peer.sendall(payload)
            receive_exact(peer, payload)
    print(f'256 UDP and 16 TCP echoes passed in {time.monotonic() - started:.2f}s')


if __name__ == '__main__':
    main()
