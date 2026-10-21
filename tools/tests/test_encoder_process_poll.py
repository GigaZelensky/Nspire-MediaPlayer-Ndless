"""A short-lived probe must not hide a still-running native encoding pass."""
from pathlib import Path
from types import SimpleNamespace
import sys,unittest
from unittest.mock import Mock,patch
import psutil

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from check_encoder_gui import encoding_children


class EncodingChildrenTests(unittest.TestCase):
    def test_probe_exits_during_enumeration(self):
        probe=Mock();probe.cmdline.side_effect=psutil.NoSuchProcess(1)
        encoder=Mock();encoder.cmdline.return_value=['ffmpeg','-progress','pipe:1']
        parent=SimpleNamespace(children=lambda recursive:[probe,encoder])
        with patch('psutil.Process',return_value=parent):
            self.assertEqual(encoding_children(123),[encoder])

    def test_parent_already_exited(self):
        with patch('psutil.Process',side_effect=psutil.NoSuchProcess(123)):
            self.assertEqual(encoding_children(123),[])

    def test_probe_is_not_an_encoding_pass(self):
        probe=Mock();probe.cmdline.return_value=['ffmpeg','-i','input.mkv']
        parent=SimpleNamespace(children=lambda recursive:[probe])
        with patch('psutil.Process',return_value=parent):
            self.assertEqual(encoding_children(123),[])


if __name__=='__main__':unittest.main()
