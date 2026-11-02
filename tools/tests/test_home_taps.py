"""Compatibility entry point: Today gestures now live in the dedicated worker."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).with_name('test_home_worker.py')),run_name='__main__')
