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
          "MET Norwayn ennuste haettu: ":"MET Norway forecast fetched: ",
          "Automaattiohjaus pois":"Automatic control off",
        }
        for fi,en in expected.items():
            self.assertEqual(STRINGS[fi],en)
    def test_midea_provisioning_security_and_dynamic_messages_are_translated(self):
        expected={
          "Midea · paikallinen V2 / V3 Wi-Fi -yhteys":
              "Midea · local V2 / V3 Wi-Fi connection",
          "V3 token (128 heksamerkkiä)":
              "V3 token (128 hex characters)",
          "V3 key (64 heksamerkkiä)":
              "V3 key (64 hex characters)",
          "Midean automaattiohjaus edellyttää hyväksyttyä mallikohtaista ohjauskoetta ja tuoretta tilanlukua.":
              "Midea automatic control needs an approved model-specific pilot and a fresh state read.",
          "Midea-yhteys tallennettu. Ensimmäinen luku tapahtuu verkossa.":
              "Midea connection saved. The first status poll will take place over the network.",
          "Tarkista device ID, V3 token (128) ja key (64 heksamerkkiä).":
              "Check device ID, V3 token (128) and key (64 hex characters).",
        }
        for fi,en in expected.items():
            self.assertEqual(STRINGS.get(fi),en)
        security=STRINGS["V2- tai V3-tilanluku tapahtuu suoraan lämpöpumpun Wi-Fi-moduulista ilman Home Assistantia tai jatkuvaa pilviyhteyttä. Midean käsiohjaus ja automaattinen hinta- ja termostaattiohjaus ovat kokeellisia. Yhdistä vain omassa luotetussa lähiverkossa; kehitysvaiheen HTTP-asetussivu ei salaa tunnisteiden siirtoa."]
        self.assertIn("trusted local network",security)
        self.assertIn("does not encrypt the credentials",security)
        self.assertIn("Manual Midea",security)
        self.assertIn("V3-tunnisteet tallennetaan paikalliseen NVS-muistiin, eikä niitä palauteta selaimeen. V2 ei tarvitse tokenia tai keytä, mutta sen yhteys käyttää yhteistä kiinteää salausavainta eikä todenna pumppua samalla tavalla kuin V3. Valitse protokolla laitteen todetun version mukaan. Käytä vain luotetussa lähiverkossa. Päivitä yhteysasetukset, jos laitteen IP-osoite vaihtuu.",STRINGS)

    def test_midea_test_diagnostics_are_translated(self):
        for key in ("Testaa yhteys nyt", "Ei vielä testattu.",
                    "V3-tunnistautuminen epäonnistui: tarkista token ja key.",
                    "Yhteystesti käynnistetty. Pumppua ei ohjata."):
            self.assertIn(key,STRINGS)
            self.assertNotEqual(key,STRINGS[key])
        self.assertIn("'/api/midea/test'",SRC)
        self.assertIn("mideaReadErrors",SRC)

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