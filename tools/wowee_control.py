#!/usr/bin/env python3
"""Local control client. No credentials or game execution; stop requests Manual only."""
import argparse
import json
import os
from pathlib import Path
import socket
import stat
import sys
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--socket', required=True, type=Path)
    parser.add_argument('--timeout', type=int, choices=range(1, 121), default=None, metavar='1..120')
    parser.add_argument('command', choices=('status', 'manual', 'bot', 'stop', 'quit'))
    args = parser.parse_args()
    if not hasattr(os, "getuid") or not hasattr(socket, "AF_UNIX"):
        parser.error("Local control currently requires desktop macOS or Linux")
    args.timeout = args.timeout if args.timeout is not None else (60 if args.command == "quit" else 15)
    if args.command != "quit" and args.timeout > 30:
        parser.error("Control command timeout must be at most 30 seconds")
    identity = uuid.uuid4().hex
    try:
        parent = args.socket.parent
        info = parent.stat()
        endpoint = args.socket.lstat()
        if (not args.socket.is_absolute() or parent.resolve() != parent or
                info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) & 0o077 or
                endpoint.st_uid != os.getuid() or not stat.S_ISSOCK(endpoint.st_mode)):
            raise ValueError('Socket must belong to you in an owned private directory')
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.settimeout(args.timeout + 2)
            connection.connect(str(args.socket))
            request = {'id': identity, 'command': args.command, 'timeout_seconds': args.timeout}
            connection.sendall((json.dumps(request) + '\n').encode())
            data = bytearray()
            while b'\n' not in data:
                chunk = connection.recv(4096)
                if not chunk:
                    raise ConnectionError('Client disconnected before confirmation')
                data.extend(chunk)
                if len(data) > 16384:
                    raise ValueError('Oversized response')
        result = json.loads(data)
        if not isinstance(result, dict):
            raise ValueError('Invalid response')
        if result.get('version') != 1 or result.get('id') != identity or result.get('command') != args.command:
            raise ValueError('Response identity mismatch')
        if result.get('outcome') == 'confirmed':
            control = result.get('control', {})
            desired = 'bot' if args.command == 'bot' else 'manual'
            if args.command == 'quit':
                if (control.get('request_id') != identity or control.get('phase') != 'finished' or
                        not all(control.get(k) for k in ('manual_confirmed', 'logout_confirmed', 'disconnected'))):
                    raise ValueError('Shutdown is not confirmed')
            elif (control.get('request_id') != identity or not control.get('fresh') or
                    control.get('mode') != desired or control.get('pending_mode') or control.get('failure')):
                raise ValueError('Control confirmation does not match request')
        print(json.dumps(result))
        return 0 if result.get('outcome') in ('observed', 'confirmed') else 2
    except (OSError, ValueError, ConnectionError) as error:
        print(json.dumps({'id': identity, 'command': args.command, 'outcome': 'unconfirmed',
                          'error': str(error), 'control_may_still_be_active': True}))
        return 2


if __name__ == '__main__':
    sys.exit(main())
