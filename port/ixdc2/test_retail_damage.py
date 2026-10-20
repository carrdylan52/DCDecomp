"""Project explicitly private additions away; every remaining byte is retail."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]

def function(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[opening:end]

class RetailDamage(unittest.TestCase):
    def test_default_body_is_retail(self):
        retail = function((ROOT / "ps2/src/monstorunit.cpp").read_text(), "int CMonstorUnit::CheckDmg()")
        private = function((ROOT / "port/src/ridepod_damage.cpp").read_text(), "int CMonstorUnit::CheckDmg()")
        private = re.sub(r"^[^\n]*// RIDEPOD_BEGIN\n.*?^[^\n]*// RIDEPOD_END\n", "", private, flags=re.M | re.S)
        private = private.replace("!ridepod && ", "")
        self.assertEqual(private, retail)

    def test_allocator_body_is_retail(self):
        retail = function((ROOT / "ps2/src/collisiondata.cpp").read_text(), "int CCollisionData::Set(")
        private = function((ROOT / "port/src/ridepod_records.cpp").read_text(), "int CCollisionData::Set(")
        private = re.sub(r"^[^\n]*// RIDEPOD_BEGIN\n.*?^[^\n]*// RIDEPOD_END\n", "", private, flags=re.M | re.S)
        self.assertEqual(private, retail)

    def test_actor_creation_body_is_retail(self):
        retail = function((ROOT / "ps2/src/monstorunit.cpp").read_text(), "int CMonstorUnit::SetupViewMonstor(")
        private = function((ROOT / "port/src/ridepod_scene.cpp").read_text(), "int CMonstorUnit::SetupViewMonstor(")
        private = re.sub(r"^[^\n]*// RIDEPOD_BEGIN\n.*?^[^\n]*// RIDEPOD_END\n", "", private, flags=re.M | re.S)
        self.assertEqual(private, retail)

if __name__ == "__main__":
    unittest.main()
