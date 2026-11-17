# F02 ESP32-S3 testikortti – ei-tuhoavat tarkistukset ja palautussuunnitelma (17.11.2026)

**Tämä on testikortin valmistelu, ei lupa fläshätä.** Pihamökin ESP32/CN105/Shelly ja muut tuotantolaitteet pysyvät muuttumattomina.

## Varmennetut lähtötiedot

- Core-koneen `/dev/ttyUSB1` on tällä hetkellä CH340/CH341 USB-sarjasovitin (USB ID `1a86:7523`). Sovitin **ei todenna** sen takana olevan ESP32:n mallia, flash-kapasiteettia tai kytkentää.
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

**Tulos:** valmiustarkastus tehty, fyysinen F02-testi BLOCKED kunnes laiteyksilö/osiotaulu ja turvallinen palautus on vahvistettu sekä laiteoperaatiolle saatu lupa.
