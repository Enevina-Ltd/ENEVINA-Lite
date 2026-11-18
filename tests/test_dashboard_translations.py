"""Ensure the flash-backed local FI/EN dictionary covers visible Finnish labels."""
from html.parser import HTMLParser
from pathlib import Path
import json
import unittest

SRC=(Path(__file__).resolve().parents[1]/
     "firmware/cottage/include/cottage_premium_dashboard.hpp").read_text(encoding="utf-8")
START="const UI_EN=Object.freeze("
END=");\nconst UI_EN_FRAGMENTS"
assert SRC.count(START)==1 and SRC.count(END)==1
RAW=SRC.split(START,1)[1].split(END,1)[0]
STRINGS=json.loads(RAW)

class Captions(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.skip=0
        self.words=[]
    def handle_starttag(self,tag,attrs):
        if tag in ("script","style"):self.skip+=1
        for key,value in attrs:
            if key in ("aria-label","placeholder","title") and value:
                self.words.append(value.strip())
    def handle_endtag(self,tag):
        if tag in ("script","style"):self.skip=max(0,self.skip-1)
    def handle_data(self,data):
        if not self.skip and data.strip():self.words.append(data.strip())

class DashboardTranslations(unittest.TestCase):
    def test_finnish_captions_have_english_equivalent(self):
        parser=Captions()
        parser.feed(SRC[SRC.index("<!doctype html>"):SRC.index("</html>")+7])
        missing=[value for value in parser.words
                 if any(ch in value for ch in ("ä","ö","Ä","Ö","å","Å"))
                 and value not in STRINGS]
        self.assertEqual(missing,[])
        self.assertGreaterEqual(len(STRINGS),320)
    def test_static_and_dynamic_key_translations(self):
        expected={
          "Sääennuste":"Weather forecast",
          "Ylläpitolämpö":"Maintenance",
          "Käyttöliittymän kieli":"Interface language",
          "FMI-ennuste haettu: ":"FMI forecast fetched: ",
          "Automaattiohjaus pois":"Automatic control off",
        }
        for fi,en in expected.items():
            self.assertEqual(STRINGS[fi],en)
    def test_language_never_writes_to_device(self):
        control=SRC.split("function setDashboardLanguage(language){",1)[1].split(
            "document.querySelectorAll('[data-lang]')",1)[0]
        self.assertIn("uiLanguage=language",control)
        self.assertIn("localStorage.setItem",control)
        self.assertNotIn("/api/control",control)
        self.assertNotIn("POST",control)
        self.assertIn("web.on(\"/api/weather\",HTTP_GET,weather_forecast_endpoint);",
                      (Path(__file__).resolve().parents[1]/
                      "firmware/cottage/src/main.cpp").read_text(encoding="utf-8"))

if __name__=="__main__": unittest.main()
