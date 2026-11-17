# F02 ESP32-S3 testikortti – ei-tuhoavat tarkistukset ja palautussuunnitelma (17.11.2026)

**Tämä on testikortin valmistelu, ei lupa fläshätä.** Pihamökin ESP32/CN105/Shelly ja muut tuotantolaitteet pysyvät muuttumattomina.

## Varmennetut lähtötiedot

- Core-koneen `/dev/ttyUSB1` on tällä hetkellä CH340/CH341 USB-sarjasovitin (USB ID `1a86:7523`). Sovitin **ei todenna** sen takana olevan ESP32:n mallia, flash-kapasiteettia tai kytkentää.
- Vanha, yksityiseen hakemistoon tallennettu **16.11.2026 klo 21:52** esptool v5.3.1 -lukuloki vahvistaa, että samalla portilla oli silloin **ESP32-S3 QFN56 revision v0.2, sulautettu 8 Mt PSRAM, 40 MHz kide**. Luku päättyi merkintään `Hard resetting via RTS pin`. Tämä on **historiallinen laitetunnistus**, ei uusi 17.11. laitehandshake tai todiste siitä, ettei laitetta olisi vaihdettu.
- Alkuperäisen flash-prefiksin ESP32-kuvaotsake ilmoittaa ohjelmiston **konfiguroiduksi flash-kooksi 16 Mt** (kokokoodi 4). Todellista nykyistä JEDEC-flash-tunnistetta ja piirin fyysistä kokoa ei ole vielä luettu ilman laitteen resetointia.
- USB:n by-id-polku `/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0` on yleisniminen eikä sisällä yksilöivää sarjanumeroa. Portin käyttäjää ei löytynyt `fuser`-tarkistuksessa, mutta saman Core-koneen verkon yli lukevat Ruuvi/CN105-kerääjät ja OTA-palvelu olivat käynnissä ja jätettiin ennalleen.
- Yksityisessä testikortin varmuuskopiohakemistossa on 64 KiB:n `factory_flash_prefix_64k.bin`, SHA-256 `09fd37f9d5e89b8d25680f5cd31ba26fdfd65f92172bb6fa04967f45809aa2d1`.
- Alkuperäisen prefiksin osoitteen `0x8000` osiotaulussa ovat mm. NVS `0x9000` / `0x4000`, otadata `0xd000` / `0x2000`, `ota_0` `0x100000` / `0x600000` ja `ota_1` `0x700000` / `0x600000`.
- Erillisessä C2.4:n 64 KiB:n prefiksissä `flash_prefix_64k.bin` on **eri** osiotaulu: NVS `0x9000` / `0x5000`, otadata `0xe000` / `0x2000`, `app0` `0x10000` / `0x640000`, `app1` `0x650000` / `0x640000`. Sen SHA-256 on `e96cbfa3ca38dbb778d32205e28f413a2108cf2ec8f2a14d65de180cc17c6c1f`.
- Nämä eivät ole täysiä flash-varmuuskopioita. Osiotaulujen ja NVS:n yhteensopivuutta nykyisen kortin kanssa ei ole todistettu. Edellä mainitut paikalliset polut/varmuuskopiot **eivät** kuulu GitHubiin.

## Ennen yhtäkään laiteoperaatiota

1. Varmista **fyysinen kortti ja USB-portti uudelleen**: udev-tiedot, kortin merkinnät, piiriversio ja johdotus. Tarkista, ettei portti ole tuotantokäytössä tai seuranta kesken.
2. Tallenna alkuperäinen laitteesta luettu **koko flash** hyväksyttyyn yksityiseen tallennuspaikkaan, tarkista koko ja SHA-256 sekä jäsennä osiotaulu/NVS/otadata/bootloader. ROM-download-tilaan siirtyminen voi resetoida kortin: pyydä siihen erillinen lupa.
3. Vertaa todellista osiotaulua suunniteltuun ESP32-S3 N16R8 -buildiin. Jos NVS:n sijainti tai bootloader/ota-osiot eivät täsmää, **älä fläshää**. Suunnittele erillinen turvallinen, palautettava app-only-päivitys.
4. Määritä palautuspolku ja varmista sen käytettävyys ennen päivitystä. 64 KiB:n prefix yksin **ei riitä** palauttamaan sovellusta, asetuksia ja molempia OTA-osioita.
5. Vasta nimenomaisen hyväksynnän jälkeen saadaan aloittaa laitekohtainen F02-testitoteutus. Testikortti ei saa olla kytkettynä oikean pumpun kirjoittavaan ohjauspolkuun.

## F02-laitehyväksyntä, kun laiteoperaatio on erikseen hyväksytty

- Testaa oikealla, BLE-kantamalla olevalla RuuviTagilla.
- Mittaa **todelliset** skannauskierrokset ja näytteiden ikä, `low_heap_scan_skips`, sisäisen RAMin vapaa/suurin lohko, PSRAM, TLS+Wi-Fi, paikallisen HTTP:n latenssit, reset-reason ja uptime.
- Erottele anturin poissaolo, BLE-starvation, Wi-Fi-katko ja firmware-reset; älä selitä niitä samoilla oireilla.
- Ei uusia watchdogeja, reset-silmukoita eikä automaattisia palautuksia.
- Lämmityspumpulle tai Shellyn releelle ei lähetetä komentoja.

## Tämän työn havainto

Vanhan C2.3:n kerätyssä lokissa näkyi 14 uptime-pudotusta 16.11.2026 klo 16:03–18:33 UTC. Näytteissä ei ole `esp_reset_reason()`-arvoa tai ESP32:n boot-tekstiä; syy on **vahvistamatta**. Core-koneen Linux-kernel kirjasi CH341:n USB-irrotuksen 16.11.2026 klo 20:08:54 paikallista aikaa ja uuden kytkennän 21:48:44. Ne eivät todista 14 resetin juurisyytä.

## Täyden flash-varmuuskopion valmistelu (EI SUORITETTU)

**Tarvittava erikseen hyväksyttävä operaatio:** testikortin vieminen ESP32-S3:n ROM-download-tilaan, koko flashin lukeminen 0x000000–0xFFFFFF yksityiseen tiedostoon ja laitteen palauttaminen käynnistyvään tilaan. `esptool`-handshake ja DTR/RTS-ohjaus voivat käynnistää laitteen uudelleen. Mitään näistä ei suoriteta ilman nimenomaista lupaa.

Kun luvan lisäksi sama fyysinen kortti, USB-reitti ja flashin todellinen koko on varmennettu, alustava **vasta silloin ajettava** luku olisi:

```bash
# EI AJETTU — vaatii erillisen hyväksynnän ja oikean kortin tunnistamisen.
PORT=/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
BACKUP=/home/kaelion/.secure/enevina-lite-new-esp-20261116/full_flash_16m_after_approval.bin
esptool --chip esp32s3 --port "$PORT" read_flash 0x000000 0x1000000 "$BACKUP"
test "$(stat -c %s "$BACKUP")" -eq 16777216
sha256sum "$BACKUP"
```

Tämän jälkeen **offline-työkalulla** puretaan koko talteen otetun flashin todellinen osiotaulu, verrataan sitä 64 KiB:n prefiksiin, tunnistetaan bootloader, NVS, otadata ja molemmat OTA-osiot sekä tarkistetaan, että täydellinen palautus on mahdollinen. Älä käytä C2.4:n toista osiotaulua vanhan NVS:n tai OTA-tilan päälle.

## Käyttöajan nollaantumisten jatkodiagnostiikka

14 pudotusta tallentui **16.11. klo 16:03–18:33 UTC**, eli **18:03–20:33 Suomen aikaa**. Linuxin USB-irrotus klo **20:08 Suomen aikaa** (18:08 UTC) osuu tämän aikavälin sisään, mutta ei yksilöi kaikkien 14 pudotuksen syytä. Vanha näyteloki ei sisältänyt ESP32:n reset reason -tietoa tai alkuperäistä boot-tekstiä. Uuteen Lite-ehdokkaaseen lisätään vain kerran käynnistyessä luettava `esp_reset_reason()` ja read-only `boot_reset_reason_code`-API-kenttä; se ei muuta käynnistymistä, OTA:a tai watchdogia. Historiallista syytä tämä muutos ei voi jälkikäteen todistaa.

**Tulos:** valmiustarkastus tehty, fyysinen F02-testi BLOCKED kunnes laiteyksilö/osiotaulu ja turvallinen palautus on vahvistettu sekä laiteoperaatiolle saatu lupa.
