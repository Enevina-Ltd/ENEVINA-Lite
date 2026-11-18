# Enevina Lite – käyttöönotto: Wi-Fi ensin, muut asetukset kotiverkossa (18.11.2026)

**Tila: toteutettu ja HOST-TESTATTU. Ei FYYSISESTI TODENNETTU.** Firmware kääntyy
(`esp32s3_cottage_shadow`), mutta yhtään ESP32-S3-korttia ei flashattu eikä
Android- tai iPhone-puhelimella ole tehty käyttöönottokoetta tämän muutoksen
yhteydessä. Pihamökin korttia ei ole koskettu.

## Vaihe 1 – vain Wi-Fi

| Tilanne | Laitteen toiminta |
|---|---|
| Ensimmäinen käynnistys, ei tallennettua verkkoa | Avaa WPA2-suojatun asennusverkon `Enevina-Lite-XXXX` (MAC:n kaksi viimeistä tavua). |
| Puhelin liittyy verkkoon | Captive portal -tunnistus (`/generate_204`, `/hotspot-detect.html`, `/connecttest.txt`, …) ja DNS-ohjaus avaavat `/setup`-sivun. Sivulla kysytään **vain** kotiverkko ja salasana. |
| Verkkoluettelo | Asynkroninen haku, vahvin merkintä per SSID, enintään 20, piilotetut pois. Nimen voi kirjoittaa myös käsin. |
| Yhdistä | Ehdokasverkkoa testataan asennusverkon pysyessä päällä. **NVS:ään tallennetaan vasta, kun laite saa IP-osoitteen.** |
| Väärä salasana / verkkoa ei löydy / aikakatkaisu (25 s) | Selkeä suomenkielinen virhe (`WRONG_PASSWORD`, `NETWORK_NOT_FOUND`, `TIMEOUT`), vanha toimiva verkko palautetaan, asennusverkko pysyy auki uutta yritystä varten. |
| Onnistui | Sivu näyttää `http://enevina-lite.local/` (jos mDNS käynnistyi) **ja aina** numeerisen `http://<IP>/`-osoitteen. Asennusverkko sulkeutuu 2 min kuluttua. |
| Kotiverkko katkeaa yli 5 min | Asennusverkko avautuu uudelleen palautusta varten (ei USB:tä), STA yrittää samalla rinnakkain. Verkon palattua asennusverkko sulkeutuu 2 min kuluttua. **Ei uudelleenkäynnistystä, ei watchdogia, ei vaikutusta pumpun ohjaukseen.** |
| Wi-Fi-verkon vaihto myöhemmin | Dashboard → Asetukset → *Vaihda Wi-Fi-verkko* (`/setup`). Epäonnistunut vaihto palauttaa vanhan verkon. |

Logiikka on alustariippumattomassa tilakoneessa `cottage/wifi_provisioning.hpp`,
joka on testattu tiedostossa `tests/wifi_provisioning_tests.cpp`
(ensikäynnistys, onnistuminen, väärä salasana, verkkoa ei löydy, aikakatkaisu,
epäonnistunut vaihto, katkon jälkeinen palautus, mDNS:n puuttuminen).
Firmware-kytkentä: `firmware/cottage/src/cottage_wifi.cpp`, sivu
`cottage_wifi_page.cpp`, lähdetestit `tests/test_wifi_onboarding_source.py`.

### Turvallisuusmuutos, joka vaatii omistajan tietoisen hyväksynnän

Tämä muutos **lisää yhden laitekohtaisen salasanan**: asennusverkon WPA2-avaimen.
Se on toteutettu tehtävänannon nimenomaisen vaatimuksen *"Suojattu,
laitekohtainen käyttöönotto"* ja *"Älä jätä käyttöönottoverkkoa turhaan
avoimeksi"* perusteella. Se ei ole kirjautuminen eikä hallintasalasana:

- 10 merkkiä laitteistosatunnaislukugeneraattorista (`esp_random`), ~49 bittiä,
  luodaan kerran ja säilytetään NVS:ssä (`prov/ap_pw`).
- Tarkoitettu laitteen tarraan (tehdasvaihe, **ei vielä toteutettu**). Sarjaporttiin
  se tulostetaan vain, kun laitteessa ei ole tallennettua verkkoa.
- Kotiverkon salasanaa ei koskaan lokiteta eikä palauteta API:ssa.
- Asetusten tallennus käyttää **olemassa olevaa** käynnistyskohtaista
  CSRF-tunnistetta (`c23_authorize_local_setting`); uutta porttia ei lisätty.

Vaihtoehto, jos omistaja ei hyväksy avainta: avoin asennusverkko lyhyeksi ajaksi
virran kytkennän jälkeen. Sitä **ei** toteutettu, koska tehtävä kielsi
tarpeettoman avoimen verkon.

### Tunnetut rajat (ei testattu oikealla laitteella)

- ESP32-S3 voi vaihtaa asennusverkon kanavaa STA-yrityksen aikana; puhelin voi
  hetkeksi pudota asennusverkosta. Sivu ohjeistaa yhdistämään uudelleen.
- NVS:ää ei ole salattu (vaatisi flash-salauksen = uusi turvamuutos, omistajan päätös).
- Yhdessä lähiverkossa usea Lite: mDNS-nimi `enevina-lite` voi törmätä; ESP-IDF:n
  mDNS nimeää törmäyksessä uudelleen. Laitekohtainen DHCP-nimi
  `enevina-lite-xxxx` ja numeerinen osoite näkyvät aina.
- Tehdasasetusten palautus (esim. BOOT-painike) puuttuu tarkoituksella; palautus
  tapahtuu katkon jälkeen avautuvalla asennusverkolla.
- Pilottikortti, jonka verkko on käännetty `cottage_local_config.hpp`:hen, toimii
  kuten ennen, mutta yli 5 min verkkokatkon jälkeen se avaa nyt suojatun
  asennusverkon. Tämä on uusi käyttäytyminen.

## Vaihe 2 – asetukset kotiverkossa

`http://enevina-lite.local/` tai `http://<IP>/` → Asetukset:

- **Sähkösopimus ja siirto:** marginaali, siirto päivä/yö (tai talviarkipäivä /
  muu aika), sähkövero, ALV, sisältävätkö hinnat ALV:n, siirtotuote (yleis-, aika-,
  kausisiirto), päivä/yö-tuntirajat, esilämmityksen vähimmäishintaetu.
- **Lämpöpumppu ja yhteydet:** sovitin (vain tällä firmwarella todennetut ovat
  valittavissa), CN105-sillan `/events`-osoite, Shelly-mittari, FMI-sääpaikka.
- Tallennus: koko lomake validoidaan, NVS:ään kirjoitetaan ensin, RAM vasta
  onnistuneen kirjoituksen jälkeen. CN105-/Shelly-osoitteet ja hinnat tulevat
  voimaan heti; sääpaikka seuraavassa virran kytkennässä.
- Kääntöaikaiset `COTTAGE_*`-arvot ovat vain oletuksia konfiguroimattomalle laitteelle.

Lämpötilatavoitteet ja toimintatilat olivat jo dashboardissa (PR #26–#28).
