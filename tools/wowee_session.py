#!/usr/bin/env python3
"""Launch a saved-profile session with independent wall-clock and receipt checks.

No automatic Bot request. Use wowee_control.py against OUTPUT/control.sock.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def receipt(path):
    try:
        value = json.loads(path.read_text())
        return value if isinstance(value, dict) else {}
    except (OSError, ValueError):
        return {}


def confirmed(value):
    return (value.get('phase') == 'finished' and value.get('exit_code') == 0 and
            all(value.get(k) is True for k in
                ('manual_confirmed', 'logout_confirmed', 'disconnected', 'client_shutdown_complete')))


def supervise(binary, profile, character, output, *, realm=None, seconds=1200,
              startup_timeout=180, stop_timeout=45, shutdown_grace=30, poll=0.2):
    output = Path(output)
    binary = Path(binary).resolve()
    if not output.is_absolute() or output.parent.resolve() != output.parent:
        raise ValueError('Output must be an absolute path with a canonical parent')
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise ValueError('Client binary is missing or not executable')
    previous_umask = os.umask(0o077)
    child = None
    created = False
    try:
        output.mkdir(mode=0o700, exist_ok=False)
        created = True
        command = [str(binary), '--login-profile', profile, '--character', character,
                   '--control-socket', str(output / 'control.sock'), '--session-seconds', str(seconds),
                   '--startup-timeout', str(startup_timeout), '--stop-timeout', str(stop_timeout)]
        if realm:
            command += ['--realm', realm]
        started = time.monotonic()
        with (output / 'client.log').open('x') as log:
            child = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=log,
                                     stderr=subprocess.STDOUT, start_new_session=True)
            state = {'pid': child.pid, 'outcome': 'running', 'forced_kill': False,
                     'automatic_bot_request': False,
                     'requested': {'profile': profile, 'character': character, 'realm': realm,
                                   'seconds': seconds, 'startup_timeout': startup_timeout,
                                   'stop_timeout': stop_timeout, 'shutdown_grace': shutdown_grace}}
            (output / 'supervisor-start.json').write_text(json.dumps(state, indent=2) + '\n')
            entered = stop_started = cleanup_started = None
            while child.poll() is None:
                now = time.monotonic()
                observed = receipt(output / 'session.json')
                if entered is None and observed.get('phase') in ('running', 'manual', 'logout', 'disconnect', 'finished'):
                    entered = now
                if stop_started is None and observed.get('phase') in ('manual', 'logout', 'disconnect'):
                    stop_started = now
                    state['stop_reason'] = 'native_shutdown'
                if observed.get('phase') in ('finished', 'failed') and cleanup_started is None:
                    cleanup_started = now
                if cleanup_started is not None and now - cleanup_started >= shutdown_grace:
                    child.kill()
                    state['forced_kill'] = True
                    state['stop_reason'] = 'client_cleanup_deadline'
                    break
                if stop_started is None and cleanup_started is None:
                    expired = (entered is None and now - started >= startup_timeout) or (
                        entered is not None and now - entered >= seconds)
                    if expired:
                        child.send_signal(signal.SIGTERM)
                        stop_started = now
                        state['stop_reason'] = 'startup_deadline' if entered is None else 'duration'
                elif stop_started is not None and now - stop_started >= stop_timeout + shutdown_grace:
                    child.kill()
                    state['forced_kill'] = True
                    break
                try:
                    time.sleep(poll)
                except KeyboardInterrupt:
                    if stop_started is None:
                        child.send_signal(signal.SIGTERM)
                        stop_started = time.monotonic()
                        state['stop_reason'] = 'supervisor_interrupt'
                    else:
                        child.kill()
                        state['forced_kill'] = True
                        break
            child.wait()
        observed = receipt(output / 'session.json')
        success = child.returncode == 0 and confirmed(observed) and not state['forced_kill']
        state.update(outcome='confirmed' if success else 'unconfirmed', exit_code=0 if success else 2,
                     client_exit=child.returncode, elapsed_seconds=round(time.monotonic() - started, 3),
                     session=observed, control_may_still_be_active=not all(
                         observed.get(k) is True for k in ('manual_confirmed', 'logout_confirmed', 'disconnected')))
        temporary = output / 'supervisor.json.tmp'
        temporary.write_text(json.dumps(state, indent=2) + '\n')
        temporary.replace(output / 'supervisor.json')
        return state
    except BaseException as error:
        if child is not None and child.poll() is None:
            child.kill()
            child.wait()
        if created:
            failure = {'outcome': 'unconfirmed', 'exit_code': 2, 'error': type(error).__name__,
                       'client_exit': child.returncode if child is not None else None,
                       'control_may_still_be_active': True}
            (output / 'supervisor.json').write_text(json.dumps(failure, indent=2) + '\n')
        raise
    finally:
        # A supervisor exception must not orphan a new client process.
        if child is not None and child.poll() is None:
            child.kill()
            child.wait()
        os.umask(previous_umask)


def bounded(lower, upper):
    def parse(text):
        value = int(text)
        if not lower <= value <= upper:
            raise argparse.ArgumentTypeError(f'Value must be {lower}..{upper}')
        return value
    return parse


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path(__file__).resolve().parents[1] / 'build/bin/wowee')
    parser.add_argument('--profile', required=True)
    parser.add_argument('--character', required=True)
    parser.add_argument('--realm')
    parser.add_argument('--output', type=Path, required=True, help='New private per-run directory')
    parser.add_argument('--seconds', type=bounded(1, 86400), default=1200)
    parser.add_argument('--startup-timeout', type=bounded(1, 600), default=180)
    parser.add_argument('--stop-timeout', type=bounded(1, 120), default=45)
    parser.add_argument('--shutdown-grace', type=bounded(1, 120), default=30)
    args = parser.parse_args()
    if not hasattr(os, 'getuid'):
        parser.error('Session supervision currently requires desktop macOS or Linux')
    try:
        result = supervise(args.binary, args.profile, args.character, args.output, realm=args.realm,
                           seconds=args.seconds, startup_timeout=args.startup_timeout,
                           stop_timeout=args.stop_timeout, shutdown_grace=args.shutdown_grace)
    except (OSError, ValueError) as error:
        print(json.dumps({'outcome': 'unconfirmed', 'error': str(error), 'control_may_still_be_active': True}))
        return 2
    print(json.dumps(result))
    return result['exit_code']


if __name__ == '__main__':
    sys.exit(main())
