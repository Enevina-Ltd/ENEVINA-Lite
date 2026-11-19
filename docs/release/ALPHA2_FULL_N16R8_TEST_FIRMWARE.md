# Enevina Lite v0.1.0-alpha.2 – täysi ESP32-S3 N16R8 -testifirmware

**19.11.2026 · Kehittäjätesti · Sovelluskuva (app-only)**

Tämä on **täydestä Enevina Lite v0.1.0-alpha.2 -sovelluksesta** käännetty kuva. Ominaisuuksia ei ole karsittu eikä erillistä kevennettyä firmwareversiota tehty. Lähdekoodi vastaa täsmälleen julkaisutagia `v0.1.0-alpha.2` (Git SHA `95148822ec3385dbb13be2415fe04382e78c2d4b`).

**Sisältö:** paikallinen premium-dashboard, Wi-Fi-käyttöönotto ja asetukset, pörssisähkön maa- ja hintatiedot, MET Norway -sääennuste, RuuviTag BLE -anturit, paikallinen lämpömalli ja pörssisähköoptimoinnin toteutus, Mitsubishi Electric CN105 -kehitysadapteri, Midea V2/V3 -LAN-tilanluku, NVS-tallennus ja turvallisuusportit sekä **allekirjoitetun HTTPS-OTA:n koodi, luottamusankkuri ja palautusmekanismi**.

**OTA on mukana ja käännetty käyttöön.** Tähän testikuvaan on sidottu jo olemassa oleva Enevinan **paikallisverkossa toimiva testijulkaisukanava** ja sen julkinen varmenne/allekirjoituksen tarkistusavain. Yksityistä allekirjoitusavainta ei julkaista. **Kanava ei ole kaikille käyttäjille saavutettava julkinen OTA-palvelu**. Testaaja tarvitsee kyseiseen kanavaan pääsyn ja yhteensopivan A/B-bootloaderin, jotta dashboardin OTA-toiminto voi oikeasti ladata allekirjoitetun päivityksen.

**Midea V2/V3:n lämpötilan kirjoitusta ei ole vielä toteutettu Liten tähän julkaisuun.** Kumpaankaan ei ole tehty tähän binääriin keinotekoista rajausta: mukana on täsmälleen nykyiseen alpha.2-versioon toteutettu toiminnallisuus. CN105:n fyysiset kirjoitukset edellyttävät erillistä käyttöönottoa ja turvallisuustarkistuksia.

### Lataus

- `Enevina-Lite-v0.1.0-alpha.2-ESP32-S3-N16R8-FULL-TEST-app.bin` – **täysi sovelluskuva**, 1 865 408 tavua
- `Enevina-Lite-v0.1.0-alpha.2-ESP32-S3-N16R8-FULL-TEST-app.bin.sha256` – SHA-256-tarkistussumma
- `FULL-TEST-FIRMWARE-README.md` – tämä kuvaus ja asennusrajoitukset

SHA-256: `e8f5b8a5e5233cd106c3ae4bb36ac61049be09de026d53ffd2e2fe3c97bfe593`

### Asennus ja rajat

- Kohde **ESP32-S3 N16R8**, 16 MiB flash + 8 MiB OPI PSRAM; PlatformIO `esp32s3_cottage_shadow`.
- Tämä on **app-only**-kuva, **ei koko flashin factory-installeri**. Mukana ei ole bootloaderia, osiotaulua eikä `otadata`-osiota. Väärä osiointi tai bootloader voi aiheuttaa käynnistymättömän laitteen.
- Tiedostoa **ei voi ladata allekirjoitettuun OTA-kanavaan pelkkänä .bin-tiedostona**. Myöhemmät OTA-julkaisut pitää allekirjoittaa ja julkaista kanavalle erikseen.
- Älä flashaa tuntemattomalle kortille ilman oikean osiotaulun tarkistusta, koko flashin eheää varmuuskopiota ja toimivaa palautussuunnitelmaa. Aiemman F02-testikortin täyden flash-varmuuskopion lukeminen epäonnistui, joten sen firmwarea ei ole tällä julkaisulla vaihdettu.
- Fyysistä mallikohtaista lämpöpumppuohjauksen hyväksyntää ei väitetä, eikä tässä julkaisussa ole tehty kirjoituksia oikeisiin lämpöpumppuihin.

**English:** Full current alpha.2 application for ESP32-S3 N16R8, without feature stripping. The signed HTTPS OTA engine is compiled and provisioned for an existing **LAN-only test publisher**, not a universal public update service. Experimental app-only image; not a complete USB first-flash bundle or consumer-certified firmware. Verify partition map, backup and rollback before installing on your own hardware.
