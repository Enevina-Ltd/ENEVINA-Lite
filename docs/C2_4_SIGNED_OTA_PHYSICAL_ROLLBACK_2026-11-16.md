# Enevina Lite C2.4 – allekirjoitetun OTA:n ja automaattisen rollbackin fyysinen todentaminen (16.11.2026)

**Päätös:** OTA:n onnistunut A/B-päivitys, sovelluksen viivästetty terveysvahvistus ja automaattinen palautuminen viallisen päivityksen jälkeen on todistettu **fyysisellä ESP32-S3 N16R8 -testikortilla**. Tämä ei vielä tarkoita C2.4:n kokonaisvaltaista pihamökkikäyttöön hyväksymistä.

## Juurisyy ja korjaus

Arduino-ESP32 2.0.x -ytimen `initArduino()` kutsuu oletuksena heikkoa `verifyRollbackLater()`-funktiota (palauttaa `false`). Se johtaa `verifyOta()`-kutsuun ja `esp_ota_mark_app_valid_cancel_rollback()`-hyväksyntään **ennen** sovelluksen omaa `setup()`-funktiota. Tämän takia uusi sovellus näkyi heti tilassa `ESP_OTA_IMG_VALID` ilman Enevinan terveystarkistusta, vaikka Arduino-kehyksen `CONFIG_APP_ROLLBACK_ENABLE` oli asetettu.

Korjaus `firmware/cottage/src/main.cpp`:

- `extern "C" bool verifyRollbackLater() { return true; }` ohittaa Arduino-ytimen heikon oletuksen eikä hyväksy kuvaa käynnistyksen alussa.
- Uuden OTA-kuvan hyväksyntä tapahtuu vain `ESP_OTA_IMG_PENDING_VERIFY`-tilassa sovelluksen oman, vähintään 60 sekunnin terveystarkistuksen jälkeen. Terveysportti sisältää paikallisen Web UI:n, Wi-Fi-yhteyden, BLE:n alustuksen, PSRAMin, sisäisen RAMin, yhtenäisen vapaan RAM-lohkon ja kriittisten tehtävien käynnistyksen. Epäonnistuva terveystarkistus pyytää rollbackia yli 120 sekunnin jälkeen.
- Vain erillisessä testikäännöksessä käytettävä `ENEVINA_OTA_TEST_RESET_BEFORE_CONFIRM` aiheuttaa tarkoituksellisen uudelleenkäynnistyksen ennen hyväksyntää. **Tätä makroa ei aktivoida normaalissa firmwarekäännöksessä.**

Fyysisen kortin bootloader päivitettiin kerran USB:llä erityisesti **ESP-IDF v5.5.5** -ympäristössä käännettyyn versioon, jonka `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`. Vakio-Arduino/PlatformIO-bootloader ei ollut antanut oikeaa A/B-palautumiskäyttäytymistä. Rakennusohje ja alkuperäisen 16 Mt:n flash-osioinnin kanssa vastaava osiotaulu ovat hakemistossa `firmware/cottage/bootloader-rollback-idf55/`. Bootloaderkuva on 20 912 tavua ja mahtuu ennen 0x8000:n osiotaulua. USB-kirjoitus osoitteeseen 0x0 vahvistettiin tarkistussummalla; NVS-, otadata- ja sovellusosioita ei kirjoitettu tässä vaiheessa. Vanhan bootloaderin sekä flashin ensimmäisen 64 KiB:n paikalliset varmuuskopiot säilytettiin suojatusti.

## Fyysiset OTA-kokeet

| Koe | Todellinen havainto | Päätös |
| --- | --- | --- |
| Toimiva allekirjoitettu versio `2026111604` | HTTPS-lataus Wi-Fi:n yli, `app0 → app1`; uusi `app1` aloitti tilassa `PENDING_VERIFY (1)` | PASS |
| Viivästetty hyväksyntä | Noin 60 sekunnin terveysportin jälkeen `app1` muuttui tilaan `VALID (2)`; `ota_boot_confirmed=true`, NVS: `good_version=2026111604` | PASS |
| Tahallisesti viallinen, allekirjoitettu versio `2026111605` | Wi-Fi-lataus `app1 → app0`; `app0=PENDING_VERIFY (1)`, tarkoituksellinen reset ~15 sekunnin kohdalla ilman vahvistusta; bootloader käynnisti takaisin `app1=VALID (2)` | **Automaattinen rollback PASS** |
| OTA-metatiedot suoraan flashista | Viallinen `app0`: `ota_seq=5`, `state=ABORTED (4)`; hyvä `app1`: `ota_seq=4`, `state=VALID (2)`; molemmissa CRC32 tarkistettu ESP-IDF:n käyttämällä siemenarvolla `0xffffffff` | PASS |
| Väärä allekirjoitus | Manifestin versionumero muutettiin ilman uutta allekirjoitusta. HTTPS GET palautti 200, mutta fyysisen laitteen `ota_signed_status=4`, `ota_signed_candidate=0`; hyvä `app1` pysyi käytössä | PASS |
| Julkaisukanavan puhdistus | Viallinen ja virheellisen allekirjoituksen testijulkaisu poistettiin palvelusta, mutta testitiedot arkistoitiin. HTTPS GET `/manifest.txt` palauttaa 404 ilman aktiivista julkaisua | PASS |
| Fyysinen ohjaus | `physical_writes=false` kaikissa mitatuissa koeajoissa; Mitsubishiä tai Shellyn relettä ei ohjattu | PASS |

**Fyysisen testin jälkeen:** hyväksytty firmware `2026111604` on käytössä `app1`-osiosta, ja valvonta-API vastaa. Kaelion-koneen yksityisessä hakemistossa on allekirjoitetut julkaisuartefaktit, testilokit ja USB-palautuskuvat. Älä julkaise yksityisiä avaimia, Wi-Fi-tunnuksia, RuuviTagin yksilöllistä tunnistetta tai fyysisiä verkkolaitteiden osoitteita.

## Julkaisun ja päivityksen turvamalli

Firmware **hakee** allekirjoitetun julkaisun varmennetun HTTPS:n kautta paikalliselta palvelimelta. Kortilla **ei ole HTTP-firmware-uploadia**, joten `ota_upload_enabled=false` on tarkoituksellinen. `ota_signed_pull_enabled=true` osoittaa, että allekirjoitettu pull-OTA on käytössä.

Julkaisua ei voi luoda laitteen Web UI:sta. Kaelion-koneella tunnistettu ylläpitäjä luo nousevan versionumeron julkaisun ja allekirjoittaa manifestin paikallisella **ECDSA P-256** -yksityisavaimella. Laitteeseen tallennetaan vain luotettu CA-varmenne ja allekirjoituksen **julkinen** avain. Manifestti sitoo laitemallin, versionumeron, pituuden sekä firmwaretiedoston SHA-256:n allekirjoitukseen. Laite ei ota uutta kuvaa käyttöön ilman varmenteen, allekirjoituksen, mallin, versionumeron, koon, SHA-256:n ja ESP-IDF:n sovelluskuvan tarkistusta. HTTPS-palvelin tarjoaa vain GET-tiedostoja hyväksytystä paikallisverkon osoitealueesta; POST/PUT/DELETE on estetty. Kortin verkko-OTA ei edellytä eikä sisällä Wi-Fi-tunnusten julkaisemista.

**Julkaisun eksplisiittinen hyväksyntä** on ylläpitäjän autentikoitu pääsy julkaisu- ja allekirjoituskoneelle. Julkaisukanavassa ei ole automaattisesti päivitettäviä allekirjoittamattomia firmwarepaketteja. Julkaisunumeroiden monotoniaportti huomioi myös arkistoidut ja karanteeniin siirretyt testiversiot.

Uuden testikortin OTA-käyttöönottoon kuuluu **yksi USB-provisiointi**: käännä rollbackilla varustettu bootloader (`sdkconfig.defaults`), varmista 16 Mt:n osiointi, ota flashvarmuuskopio ennen bootloaderin vaihtoa, luo avaimet paikallisesti ja muodosta `cottage_ota_private.hpp` -tiedosto `tools/cottage_ota_generate_header.py` -aputyökalulla. Älä koskaan commitoi paikallista `cottage_ota_private.hpp`-tiedostoa. Myöhemmät firmwarepäivitykset kulkevat tämän jälkeen vain Wi-Fi:n kautta.

## 17.11.2026 – julkaisukanavan yhteensopivuus (P1-korjaus)

Uusi OTA-julkaisun tarkistus sitoo yhteen saman julkaisukanavan **publisher-public.pem**-julkisen avaimen, **ca-public.pem**-varmenteen sekä täsmälliset HTTPS-manifesti- ja firmwareosoitteet. `tools/cottage_ota_generate_header.py` lisää paikalliseen `cottage_ota_private.hpp`-otsakkeeseen `COTTAGE_OTA_CHANNEL_MARKER`-tunnisteen. Se käännetään sovelluskuvaan, ja `cottage_ota_publish.py` vertaa tunnistetta julkaisukoneen luottamustietoihin. Väärän CA:n, julkaisijan tai HTTPS-osoitteen sisältävää kuvaa ei saa allekirjoittaa tälle kanavalle.

**Julkaisutyökalulle on nyt pakko antaa myös `--manifest-url` ja `--firmware-url`.** Niiden on oltava samat kuin luottamusankkuriotsakkeen generoinnissa. Esimerkki vain paikallisessa eristetyssä testiympäristössä:

```bash
python3 tools/cottage_ota_publish.py \
  --root /polku/eristettyyn/ota-juureen \
  --firmware /polku/testi-firmware.bin \
  --version 2026111701 \
  --manifest-url https://OTA_HOST/manifest.txt \
  --firmware-url https://OTA_HOST/firmware.bin
```

Vanha paikallinen header on päivitettävä tarkistetulla generatorilla ennen uuden OTA-firmwaren kääntämistä. Generaattori kieltäytyy ylikirjoittamasta olemassa olevaa headeria: tee uusi versio erilliseen tiedostoon, vertaile julkiset luottamusankkurit ja siirrä se käyttöön vain hyväksytyllä kehitystyömenettelyllä. **Älä julkaise, vaihda tuotannon OTA-avaimia, flashaa testikorttia tai muuta fyysistä lämpöpumppua tämän dokumenttimuutoksen perusteella.** Merkki estää vahinkojulkaisuja, ei korvaa kryptografista allekirjoituksen tarkistusta tai laitteen attestaatiota.

Core-koneen ohjelmistotestissä 18/18 kohdennettua OTA-regressiota ja ESP32-S3 PlatformIO BUILD+LINK läpäisivät; uudessa firmwarekuvassa kanavatunniste löytyi kerran. **Fyysistä uutta OTA- tai rollback-hyväksyntää ei tehty.** PR #7:n sähkökatko-/NVS-tilakoneen hyväksyntä ja Codexin lopullinen katselmointi ovat edelleen avoinna.

## Paikalliset testit

- ESP32-S3: tavallinen OTA-versio PlatformIO **BUILD PASS**, viallisen käynnistyksen fyysinen testiversio erillisessä PlatformIO-ympäristössä **BUILD PASS**. Tavallisen ELF-kuvan `verifyRollbackLater` on vahva (ei weak) symboli.
- Allekirjoitus- ja rollback-lähdekoodin regressiot: **8/8 PASS** erillisessä todennetussa työtilassa.
- Yleiskäyttöinen avainten luonti, paikallisen luottamusankkurin generointi, allekirjoitus ja HTTPS-pakettijakelu testattiin eristetyssä väliaikaisessa ympäristössä: **PASS**. TLS-varmenteen tarkistus onnistui; HTTP POST hylättiin 405-vastauksella.
- Fyysisen rollbackin otadata-sektorit luettiin suoraan USB:llä ja molempien tietueiden CRC:t varmistettiin: **PASS**.

## Avoimet hyväksyntäportit

OTA:n yllä kuvattu positiivinen, automaattisen rollbackin ja virheellisen allekirjoituksen fyysinen testaus on valmis. **Koko C2.4:n hyväksyntä ei silti ole valmis:**

1. Pitkä 24/48 h vakauskoe viimeisellä firmwaren, bootloaderin, Wi-Fi:n ja BLE:n yhdistelmällä. Vanhempi C2.4-24h koe keskeytyi OTA-testien vuoksi, eikä sitä saa kutsua läpäistyksi.
2. BLE-anturi sijaitsee jo pihamökissä ja ESP32 sisällä: sisätiloista puuttuva `sensor_fresh` ei itsessään osoita firmwarevirhettä. Fyysinen BLE-kenttätesti on vielä tekemättä.
3. CN105 SSE-heartbeat voi pysyä aktiivisena ilman varsinaisen pumpun mittauksen päivitystä. **180 sekunnin fail-closed-raja pysyy**. Ei tehty kirjoituksia.
4. RAMin pahimman kuorman ja varauspiikkien toistettavuus sekä USB-virtakatkon testi kesken OTA-kirjoituksen ovat edelleen avoimia. Allekirjoitetun version monotoniaportti ja hylättyjen testiversioiden karanteeni on toteutettu, mutta laitteistotason anti-rollback / eFuse-käsittelyä ei ole hyväksytty.
5. Ei laitteistotason Secure Boot / eFuse -hyväksyntää. Yksityisiä avaimia ei ollut laitteessa eikä eFuseihin tehty muutoksia.
6. PR #7 pysyy draftina, eikä C3-lämpöpumppuohjausta sallita.

**Päätös:** paikallisen allekirjoitetun OTA:n ja automaattisen palautuksen toimivuus on fyysisesti osoitettu. Kortti pysyy kehityksessä sisällä, kunnes muut C2.4:n kenttähyväksymisehdot täyttyvät.

## GitHub-commitin riippumaton uudelleentestaus

Julkaistu `074097d` haettiin erilliseen kehitystyötilaan ja testattiin ilman GitHub CI:tä: **C++ Release CTest 16/16 PASS, C++ Debug CTest 16/16 PASS, Python unittest 32/32 PASS, ESP32-S3 PlatformIO firmware BUILD/LINK PASS.** Python-testit ajettiin riippuvuudet erilliseen väliaikaiseen hakemistoon asentaen; eivät käyttäjän maailmanlaajuisia Python-paketteja muuttaen. Valinnainen `cottage_ota_private.hpp` ei sisälly GitHubiin, joten yleisbuildin signed-pull pysyy luonnollisesti estettynä ilman paikallista provisiointia; fyysinen OTA-onnistuminen on todistettu Kaelion-koneen paikallisella luottamusankkurilla käännetystä firmwaresta.
