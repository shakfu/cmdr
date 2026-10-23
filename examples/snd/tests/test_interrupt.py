"""Ctrl-C during snd.run is not caught by try, and cmdr exits by SIGINT.

CTest runs this as example_sox_interrupt, with CMDR_SND_BIN set to a cmdr built with snd.
"""
import os
import signal
import subprocess
import tempfile
import time
from pathlib import Path

SCRIPT = """import _snd
try { () _snd.run -n -n synth 100000 sine 440 } caught
text unreachable
"""


def test_interrupt_not_caught():
    with tempfile.TemporaryDirectory() as directory:
        script = Path(directory) / 'interrupt.cmdr'
        script.write_text(SCRIPT)
        # A new session, so SIGINT is not ignored as in a background job.
        proc = subprocess.Popen([os.environ['CMDR_SND_BIN'], str(script)], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True, start_new_session=True)
        time.sleep(1)
        assert proc.poll() is None, "snd.run ended before the interrupt"
        proc.send_signal(signal.SIGINT)
        out, err = proc.communicate(timeout=10)
        assert proc.returncode == -signal.SIGINT, err
        assert 'snd.run: interrupted' in err
        assert 'caught' not in out and 'unreachable' not in out



def test_ignored_interrupt_stays_ignored():
    # A shell starts background jobs with SIGINT ignored; snd.run keeps that.
    proc = subprocess.Popen([os.environ['CMDR_SND_BIN'], '-c',
                             'import _snd\n_snd.run -n -n synth 1000 sine 440 | text done'],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                            preexec_fn=lambda: signal.signal(signal.SIGINT, signal.SIG_IGN))
    time.sleep(0.3)
    assert proc.poll() is None, "snd.run ended before the interrupt"
    proc.send_signal(signal.SIGINT)
    out, err = proc.communicate(timeout=30)
    assert proc.returncode == 0, err
    assert out.endswith('done\n')


if __name__ == '__main__':
    test_interrupt_not_caught()
    test_ignored_interrupt_stays_ignored()
