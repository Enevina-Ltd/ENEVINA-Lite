# Enevina Lite C2 — Wi-Fi/LAN/HTTPS-laitepilotoinnin savutesti, 16.11.2026

**Tila:** FYYSINEN C2 READ-ONLY -SAVUTESTI LÄPÄISTY; 24–48 h vakaus ja C2/C3-tuotantohyväksyntä edelleen AUKI. GitHub CI tai Codex-katselmointi ei ollut käytössä.

## Laite ja turvallisuusehdot

- Ainoa erillinen ESP32-S3 N16R8 -testikortti; 16 Mt flash, 8 Mt PSRAM. **Ei** muutettu tuotannon CN105-siltaa, Mitsubishi-lämpöpumppua, Shelly-relettä tai Home Assistantia.
- Ennen Wi-Fi-testausta käynnissä ollut 24 h **offline-laitteen** vakauskoe keskeytettiin käyttöönottotestin vuoksi; siihen asti kertyneet lokit säilyvät vain paikallisesti. Erillinen, vain luku -CN105/Shelly/Nord Pool -kenttäkeräin jäi käyntiin. **Keskeytetty koe ei ole 24 h PASS.**
- Paikallinen Wi-Fi-konfiguraatio tallennettiin käyttäjän koneelle 0600-oikeuksin lähdekoodin ulkopuoliseen Gitistä pois jätettyyn tiedostoon. **Tunnuksia, SSID:tä, MACia tai LAN-osoitteita ei julkaistu.**
- `COTTAGE_SYNTHETIC_TEST_MODEL=0`, `physical_writes=false`: ei fyysistä aktuaattoripolkua; kalibroimaton MPC-suunnitelma pysyy estettynä.

## HTTPS-virhelöytö ja korjaus

Ensimmäinen Wi-Fi-versio latasi CN105:n ja Shellyn mutta ei Nord Poolin/FMI:n HTTPS-lähteitä. Fyysisen kortin lokeissa havaittiin `arduino_esp_crt_bundle_attach(): Failed to attach bundle` ja TLS-kättelyn virheitä. Arduino-ympäristön cert bundle ei ollut alustettu.

Korjaus PR #5:een:
- `firmware/main/tls_roots.hpp`: kaksi julkista, erikseen validoitua CA-juurivarmennetta (Nord Pool: GTS Root R4; FMI: HARICA TLS ECC Root CA 2021).
- `firmware/main/market.cpp` ja `weather.cpp`: Arduino-haarassa `esp_http_client_config_t.cert_pem` käyttää kohdepalvelun CA:ta. ESP-IDF-haaran aikaisempi `esp_crt_bundle_attach` säilyy.
- OpenSSL:n palvelinnimi- ja CA-ketjuvarmennus läpäisi kummallekin palvelulle (`Verify return code: 0`). **Varmenteiden tarkistusta ei ohitettu**, `setInsecure`-polkua ei lisätty.

## Fyysisen kortin tulokset luotetun HTTPS-korjauksen jälkeen

| Tarkistus | Havainto |
| --- | --- |
| Firmware `esp32s3_cottage_shadow` | PlatformIO BUILD PASS; flash/upload PASS, SHA256-kirjoitustarkistus hyväksytty |
| Flashattujen ohjelmabittien koko | 1 019 008 tavua |
| Wi-Fi | `wifi=1`; kello synkronoitu `clock=1` |
| CN105 | `cn105=1`, aktiivinen SSE; Mitsubishin tila OFF, 12,0 °C, kompressori 0 Hz |
| Shelly Plus 1PM | `shelly=1`; GET-palautus noin 166 W, releen tila havainnoitu, ei ohjausta |
| Nord Pool | `market_slots=192`, hyväksytty tuore hintadata |
| FMI | `weather_points=50`, hyväksytty tuore säädata |
| Paikallinen Web UI | `GET /` HTTP 200; suomenkielinen varjotila |
| Status- ja suunnitelma-API | `GET /api/status` HTTP 200; `GET /api/plan` HTTP 200 |
| Suojaukset | `physical_writes=false`, `model_calibrated=false`, `independent_freeze_protection_verified=false`; ei hyväksyttyä lämmityssuunnitelmaa |
| Laitemuisti | Sisäinen vapaa heap noin 217–220 kt, vapaa PSRAM noin 8,36 Mt; HTTPS-yhteyksien hetkellinen sisäisen heapin alaraja noin 112 kt |

### Uusin seurantahavainto: CN105:n mittaus vanheni

Alkukäynnistyksessä `cn105=true`, mutta noin kuuden minuutin yhtäjaksoisessa seurannassa `cn105=false`, vaikka SSE-yhteys pysyi auki. Näin 180 sekunnin mittaustuoreusportti toimii oikein: linkin aktiivisuus ei ole todiste uudesta pumpun mittausvastauksesta. Tätä ei saa kiertää keksityllä aikaleimalla eikä pelkällä SSE-uudelleenyhdistämisellä. CN105:n varsinaisen sarjaväylämittauksen tuoreus on avoin tiedonlaatuongelma. Samaan aikaan Shelly, Nord Pool ja FMI säilyivät saatavilla. Käyttöliittymän `reason`-kenttä voi myös näyttää vanhentunutta alkuperäistä Wi-Fi-tekstiä seuraavaan neljännestunnin suunnittelusyklin alkuun; tämä ei tarkoita Wi-Fi-yhteyden katkeamista.

## Auki olevat esteet

1. Riippumaton testikohteen lämpöanturi ja erillinen jäätymissuoja eivät ole vahvistettuja; BLE-lukija ei vielä mittaa rakennuksen anturia.
2. Käytetty FMI-koordinaatti on **väliaikainen paikallinen testi-/aluekoordinaatti**, ei todistettu testikohteen sijainti; tarkennettava ennen ennusteen käyttöä päätöksissä.
3. Testissä käytetty siirto- ja verotariffikonfiguraatio on tarkistettava testikohteen todellista sähkösopimusta vasten.
4. CN105:n mittauskohtainen tuoreus on edelleen osoitettava; SSE-linkin uptime ei vastaa pumpun uusia mittauksia.
5. 24–48 tunnin **Wi-Fi- ja laiteyhteisvakaus**, web/USB-valvonta, sähkökatkojen palautuminen, muistin pitkäaikaiskäyttäytyminen ja todellinen 192 jakson MPC-kokonaiskuorma ovat tekemättä.
6. Malli ja COP/sulatus eivät ole rakennuskohtaisesti kalibroituja. Varjotilan dashboardin toiminta ei hyväksy C3-automaattiohjausta.

**Päätös:** erillistä korttia voidaan käyttää testikohteessa seuraamaan oikeita mittauksia read-only C2 -varjotilassa; ei automaattiohjausta eikä PR #5:n yhdistämistä mainiin.