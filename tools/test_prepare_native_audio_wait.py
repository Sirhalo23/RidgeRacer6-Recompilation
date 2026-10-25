import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from prepare_native_audio_wait import prepare_audio, prepare_threading

SDK = Path(os.environ.get('RR6_NATIVE_AUDIO_TEST_SDK', Path(__file__).resolve().parents[2] / 'rexglue-sdk'))


class AudioSeamTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (SDK / 'src/core/threading_posix.cpp').exists():
            raise unittest.SkipTest('Requires the reviewed SDK source checkout')
        cls.threading = (SDK / 'src/core/threading_posix.cpp').read_text()
        cls.audio = (SDK / 'src/audio/audio_system.cpp').read_text()

    def test_reviewed_sources_are_accepted_without_modifying_them(self):
        for relative, original, prepare in [('core/threading_posix.cpp', self.threading, prepare_threading),
                                             ('audio/audio_system.cpp', self.audio, prepare_audio)]:
            self.assertNotEqual(prepare(original), original)
            self.assertEqual((SDK / 'src' / relative).read_text(), original)

    def test_changed_revisions_are_rejected(self):
        for source, prepare in [(self.threading, prepare_threading), (self.audio, prepare_audio)]:
            with self.subTest(prepare=prepare.__name__):
                with self.assertRaises(ValueError):
                    prepare(source + '\n// SDK revision changed\n')

    def test_no_partial_output_when_second_revision_fails(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'src/core').mkdir(parents=True)
            (root / 'src/audio').mkdir(parents=True)
            (root / 'src/core/threading_posix.cpp').write_text(self.threading)
            (root / 'src/audio/audio_system.cpp').write_text(self.audio + '\n// changed\n')
            output = root / 'generated'
            result = subprocess.run([sys.executable, str(Path(__file__).with_name('prepare_native_audio_wait.py')),
                                     str(root), str(output)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
