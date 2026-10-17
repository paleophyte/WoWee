import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('updater', Path(__file__).resolve().parents[1] / 'tools/update_bot_addon.py')
updater = importlib.util.module_from_spec(spec)
spec.loader.exec_module(updater)


class AddonUpdateTests(unittest.TestCase):
    def test_code_refresh_preserves_character_settings_and_other_addons(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, target = root / 'source/WoweeBotStatus', root / 'installed/WoweeBotStatus'
            source.mkdir(parents=True)
            target.mkdir(parents=True)
            (source / 'WoweeBotStatus.toc').write_text('Activity.lua\n')
            (source / 'Activity.lua').write_text('new code')
            (source / 'WoweeBotStatus.Jizzle.lua.saved').write_text('source settings must never ship')
            saved = target / 'WoweeBotStatus.Jizzle.lua.saved'
            saved.write_text('my settings')
            (target / 'Activity.lua').write_text('old code')
            other = target.parent / 'OtherAddon'
            other.mkdir()
            (other / 'Keep.lua').write_text('other addon')
            self.assertEqual(updater.update(target, source), ['Activity.lua', 'WoweeBotStatus.toc'])
            self.assertEqual((target / 'Activity.lua').read_text(), 'new code')
            self.assertEqual(saved.read_text(), 'my settings')
            self.assertEqual((other / 'Keep.lua').read_text(), 'other addon')
            with self.assertRaises(ValueError):
                updater.update(source, source)
            with self.assertRaises(ValueError):
                updater.update(root / 'installed', source)
            (source / 'WoweeBotStatus.toc').write_text('Missing.lua\n')
            with self.assertRaises(ValueError):
                updater.update(target, source)
            self.assertEqual(saved.read_text(), 'my settings')
            self.assertEqual((target / 'Activity.lua').read_text(), 'new code')


if __name__ == '__main__':
    unittest.main()
