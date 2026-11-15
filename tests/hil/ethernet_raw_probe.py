import ctypes
import os
import re
import subprocess
import sys
import time


class Timeval(ctypes.Structure):
    _fields_ = [('seconds', ctypes.c_long), ('microseconds', ctypes.c_long)]


class PcapHeader(ctypes.Structure):
    _fields_ = [('time', Timeval), ('captured', ctypes.c_uint32), ('length', ctypes.c_uint32)]


def main():
    if len(sys.argv) != 2:
        raise SystemExit('usage: sudo python3 raw_probe.py INTERFACE')
    interface = sys.argv[1]
    if not re.fullmatch(r'en[0-9]+', interface):
        raise SystemExit('Only a selected macOS Ethernet interface is accepted')
    output = subprocess.check_output(['ifconfig', interface], text=True)
    match = re.search(r'^\s*ether ([0-9a-f:]{17})$', output, re.MULTILINE)
    if not match:
        raise SystemExit('Interface MAC not found')
    source = bytes.fromhex(match.group(1).replace(':', ''))
    pcap = ctypes.CDLL('/usr/lib/libpcap.A.dylib')
    pcap.pcap_open_live.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int,
                                    ctypes.c_int, ctypes.c_char_p]
    pcap.pcap_open_live.restype = ctypes.c_void_p
    pcap.pcap_datalink.argtypes = [ctypes.c_void_p]
    pcap.pcap_datalink.restype = ctypes.c_int
    pcap.pcap_inject.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
    pcap.pcap_inject.restype = ctypes.c_int
    pcap.pcap_next_ex.argtypes = [ctypes.c_void_p,
                                  ctypes.POINTER(ctypes.POINTER(PcapHeader)),
                                  ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8))]
    pcap.pcap_next_ex.restype = ctypes.c_int
    pcap.pcap_geterr.argtypes = [ctypes.c_void_p]
    pcap.pcap_geterr.restype = ctypes.c_char_p
    pcap.pcap_close.argtypes = [ctypes.c_void_p]
    error = ctypes.create_string_buffer(256)
    handle = pcap.pcap_open_live(interface.encode(), 1600, 1, 1000, error)
    if not handle:
        raise SystemExit(error.value.decode(errors='replace'))
    try:
        if pcap.pcap_datalink(handle) != 1:
            raise SystemExit('Interface capture is not Ethernet (DLT_EN10MB)')
        sequence = os.urandom(4)
        packet = bytearray(b'\xff' * 6 + source + b'\x88\xb5' + b'H755PING' + sequence)
        packet.extend(b'\x00' * (60 - len(packet)))
        raw = (ctypes.c_uint8 * len(packet)).from_buffer(packet)
        sent = pcap.pcap_inject(handle, raw, len(packet))
        if sent != len(packet):
            raise SystemExit(f'pcap_inject failed: {pcap.pcap_geterr(handle).decode()}')
        print(f'Sent H755PING on {interface}, source={source.hex(":")}, seq={sequence.hex()}', flush=True)
        deadline = time.monotonic() + 12
        header = ctypes.POINTER(PcapHeader)()
        data = ctypes.POINTER(ctypes.c_uint8)()
        while time.monotonic() < deadline:
            result = pcap.pcap_next_ex(handle, ctypes.byref(header), ctypes.byref(data))
            if result < 0:
                raise SystemExit(f'pcap capture failed: {pcap.pcap_geterr(handle).decode()}')
            if result == 0 or header.contents.captured < 26:
                continue
            frame = ctypes.string_at(data, header.contents.captured)
            if (frame[:6] == source and frame[12:14] == b'\x88\xb5'
                    and frame[14:22] == b'H755PONG' and frame[22:26] == sequence):
                print(f'Received matching H755PONG from {frame[6:12].hex(":")}', flush=True)
                return
        raise SystemExit('No matching H755PONG within 12 seconds')
    finally:
        pcap.pcap_close(handle)


if __name__ == '__main__':
    main()
