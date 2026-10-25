"""Revision guards for the experimental progress source-copy adapter."""
from pathlib import Path
import os
import shutil
import tempfile
import unittest
from prepare_native_gpu_progress import prepare, sdk_copy

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get('RR6_NATIVE_TEST_SDK', ROOT.parent / 'rexglue-sdk'))
GENERATED = ROOT / 'generated/default'


class ProgressPreparationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (SDK / 'src/graphics/command_processor.cpp').exists():
            raise unittest.SkipTest('Requires the reviewed SDK checkout')
        cls.common = (SDK / 'src/graphics/command_processor.cpp').read_text()

    def test_reviewed_sources_and_idempotent_output(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / 'output'
            prepare(SDK, GENERATED, output)
            before = {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in output.iterdir()}
            prepare(SDK, GENERATED, output)
            self.assertEqual(before, {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in output.iterdir()})
            self.assertIn('progress_wait.Park', (output / 'gpu_progress_fixture.cpp').read_text())
            self.assertEqual(self.common, (SDK / 'src/graphics/command_processor.cpp').read_text())

    def test_sdk_revision_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Unsupported SDK'):
            sdk_copy(self.common + '\n// changed\n')

    def test_codegen_revision_is_rejected_before_output(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            generated = root / 'generated'
            shutil.copytree(GENERATED, generated, ignore=shutil.ignore_patterns('*.h'))
            source = generated / 'rr6_recomp_recomp.8.cpp'
            source.write_text(source.read_text().replace('DEFINE_REX_FUNC(sub_8225B038) {',
                                                       'DEFINE_REX_FUNC(sub_8225B038) {\n// revision changed'))
            with self.assertRaisesRegex(ValueError, 'Unsupported translated function'):
                prepare(SDK, generated, root / 'output')
            self.assertFalse((root / 'output').exists())


if __name__ == '__main__':
    unittest.main()
