"""Isolated fake-child tests; no WoWee login or game server."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('session_tool', Path(__file__).resolve().parents[1] / 'tools/wowee_session.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SupervisorTests(unittest.TestCase):
    def run_child(self, script, **options):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            binary = root / 'fake-client'
            binary.write_text('#!/usr/bin/env python3\n' + script)
            binary.chmod(0o700)
            output = root / 'evidence'
            limits = dict(startup_timeout=0.5, seconds=0.15, stop_timeout=0.15,
                          shutdown_grace=0.1, poll=0.01)
            limits.update(options)
            result = module.supervise(binary, 'fixture-profile', 'Fixture', output, **limits)
            self.assertEqual(json.loads((output / 'supervisor.json').read_text()), result)
            self.assertEqual(output.stat().st_mode & 0o777, 0o700)
            return result

    def test_exit_zero_without_receipt_is_unconfirmed(self):
        result = self.run_child('pass\n')
        self.assertEqual(result['exit_code'], 2)
        self.assertTrue(result['control_may_still_be_active'])

    def test_confirmed_receipt_and_clean_exit(self):
        result = self.run_child('''import sys,json,pathlib
p=pathlib.Path(sys.argv[sys.argv.index('--control-socket')+1]).parent
(p/'session.json').write_text(json.dumps(dict(phase='finished',exit_code=0,manual_confirmed=True,logout_confirmed=True,disconnected=True,client_shutdown_complete=True)))
''')
        self.assertEqual(result['exit_code'], 0)
        self.assertFalse(result['control_may_still_be_active'])

    def test_hung_client_is_killed_and_never_counted_successful(self):
        result = self.run_child('import signal,time\nsignal.signal(signal.SIGTERM,signal.SIG_IGN)\ntime.sleep(10)\n')
        self.assertEqual(result['exit_code'], 2)
        self.assertTrue(result['forced_kill'])
        self.assertLess(result['elapsed_seconds'], 2)

    def test_hang_after_logout_does_not_claim_clean_process_exit(self):
        result = self.run_child('''import sys,json,pathlib,signal,time
p=pathlib.Path(sys.argv[sys.argv.index('--control-socket')+1]).parent
(p/'session.json').write_text(json.dumps(dict(phase='finished',exit_code=0,manual_confirmed=True,logout_confirmed=True,disconnected=True,client_shutdown_complete=False)))
signal.signal(signal.SIGTERM,signal.SIG_IGN)
time.sleep(10)
''')
        self.assertEqual(result['exit_code'], 2)
        self.assertTrue(result['forced_kill'])
        self.assertFalse(result['session']['client_shutdown_complete'])

    def test_early_native_quit_is_bounded_without_waiting_for_play_duration(self):
        result = self.run_child('''import sys,json,pathlib,signal,time
p=pathlib.Path(sys.argv[sys.argv.index('--control-socket')+1]).parent
(p/'session.json').write_text(json.dumps(dict(phase='manual')))
signal.signal(signal.SIGTERM,signal.SIG_IGN)
time.sleep(10)
''', seconds=10)
        self.assertEqual(result['exit_code'], 2)
        self.assertTrue(result['forced_kill'])
        self.assertLess(result['elapsed_seconds'], 2)
        self.assertEqual(result['stop_reason'], 'native_shutdown')

    def test_incomplete_logout_cannot_satisfy_receipt(self):
        self.assertFalse(module.confirmed(dict(phase='finished',exit_code=0,manual_confirmed=True,logout_confirmed=False,disconnected=True,client_shutdown_complete=True)))


if __name__ == '__main__':
    unittest.main()
