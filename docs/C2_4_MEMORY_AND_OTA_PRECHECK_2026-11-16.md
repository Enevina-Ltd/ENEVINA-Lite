# Enevina Lite C2.4 – RAM-korjauksen laitenäyttö ja OTA-esitarkistus (16.11.2026)

## Status

**Osittainen toteutus. Ei C2.4-laitehyväksyntää eikä lupaa siirtää korttia pihamökkiin.**
PR #7 perustuu PR #6:n HEAD-committiin `dd46ccb`; PR #5 ja PR #6 pidetään luonnoksina.
Käytetty vain erillistä ESP32-S3 N16R8 -testikorttia sisätiloissa. Mitsubishi CN105 -sillan firmwarea, Mitsubishin ohjausta, Shellyn relettä, Home Assistantia tai Enevina Corea ei muutettu.

## Tehdyt lähdekoodimuutokset

1. `cottage_network_budget`: yhteinen mutex rajaa Nord Poolin HTTPS:n, FMI:n HTTPS:n ja BLE-skannauksen päällekkäisyyden. Toiminto hylätään, jos sisäinen 8-bittinen RAM tai sen suurin yhtenäinen vapaa lohko jää alle rajojen.
2. BLE-skannaus: vanha `esp_get_free_heap_size()>70000` ei erottanut sisäistä RAMia PSRAMista. Nyt käyttöönottoraja on 96 KiB sisäistä RAMia ja 24 KiB yhtenäistä varausta.
3. HTTPS-lataus: aloitusraja on 78 KiB sisäistä RAMia ja 16 KiB yhtenäistä varausta. Kaksi HTTPS-keräintä eivät kuormita samanaikaisesti TLS-kättelyllä. Vanhentunutta välimuistitietoa ei hyväksytä uutena.
4. `cJSON_InitHooks`: cJSON-puun ja JSON-serialisoinnin allokoinnit menevät PSRAMiin, eikä niissä sallita hiljaista sisäisen RAMin varakäyttöä.
5. BLE alustetaan ennen HTTPS-keräimiä; rajapintaan lisätty hetkellinen ja minimivapaa sisäinen RAM, suurin yhtenäinen vapaa lohko, sen näytteistetty minimi, PSRAM, kuormituseston laskuri ja OTA-käynnistystila.
6. OTA-appin `PENDING_VERIFY`-tilan itsenäinen käynnistysvahvistus hyväksyy uuden ohjelman aikaisintaan 60 sekunnin jälkeen vain, kun paikallinen UI, BLE, PSRAM, keskeiset tehtävät ja muistireservi täyttävät turvarajat; muuten 120 sekunnin jälkeen pyydetään rollbackia. **Tätä ei ole vielä testattu todellisella OTA-latauksella.**
7. `cottage/memory_policy.hpp` + `tests/test_c24_memory_policy.cpp`: host-kelpoinen rajaarvotesti, liitetty CTestiin Debug/Release-assertioilla.

## Fyysinen ESP32: USB-varmuuskopio ja päivitys

- Oikea testikortti vahvistettiin `/dev/ttyUSB1`-portin aiemmasta onnistuneesta ohjelmointilokista.
- 64 KiB:n flash-prefix (sis. NVS ja otadata) luettu talteen suojattuun paikalliseen hakemistoon. Tiedosto 600, hakemisto 700.
- Ensimmäinen aktiivisen firmwareosion luku nopeudella 460800 epäonnistui sarjaliikennevirheeseen. **Tästä ei päätelty palautuskelpoisuutta.**
- Uusinta nopeudella 115200: 1 615 968 tavun aktiivisen app0-alueen luku onnistui ja vastasi tavulleen alkuperäistä C2.3-firmwarekuvaa.
- C2.4 testiversio flashattiin USB:llä pelkästään osoitteeseen `0x10000`; NVS-, otadata- ja osiotaulualueita ei kirjoitettu. Esptoolin `Hash of data verified` onnistui.
- Uudempi muistidiagnostiikkaversio vaatii oman flash-/savukokeensa; aikaisempaa 240 sekunnin testiä ei saa tulkita sen ajoksi.
- Tämä oli **USB-päivitys, ei OTA-testi**.

## Fyysinen C2.4-versio: 240 sekunnin read-only kuormakoe

39 onnistunutta noin kuuden sekunnin välein tehtyä mittauskierrosta; **0 epäonnistunutta**. Kukin kierros luki `/api/status`, `/api/air/history` ja `/api/plan`. Wi-Fi, BLE, CN105 SSE, Shelly GET, Nord Pool, FMI, kosteudenhallinta, HTTP UI ja varjotila olivat yhtä aikaa käytössä.

| Suure | Todellinen mitattu arvo |
|---|---:|
| C2.3:n raportoitu sisäisen RAMin vanha minimi | 248 B (eri ajon aiempi mittaus) |
| C2.4 fyysisen 240 s kokeen sisäinen RAM, minimi | **44 912 B** |
| C2.4 näytteistetty normaali vapaa sisäinen RAM | 56 248 – 109 196 B |
| C2.4 näytteistetty suurin yhtenäinen RAM-lohko, minimi | 45 044 B |
| C2.4 PSRAMin vapaa minimi | 8 251 775 B |
| Kuormitusportin estolaskuri | 5 → 7 |
| Shellyn GET OK | 10 → 26 |
| Shellyn GET virheet | 0 |
| CN105 heartbeat-tapahtumat | 55 → 140 |
| CN105 oikeat uudet ilmastointi-/kompressoritapahtumat | **0 / 0** |
| FMI-tiedon ikä kokeen lopussa | 322 s |
| Nord Pool -tiedon ikä kokeen lopussa | 118 s |

**Rajoitukset:** yhden käynnistyksen lyhyt ajokoe ei osoita muistin pitkäaikaista vuotamattomuutta, eikä todista TLS/BLE:n absoluuttista pahinta piikkiä. 248 B ja 44 912 B eivät ole identtisten kuormien kontrolloitu A/B-koe. cJSON:n sisäisen allokoinnin ja TLS/BLE-huippujen osuus on uskottava tekninen selitys, mutta **248 B:n yksittäinen juurisyy ei ole todistettu**.

## RuuviTag ja NVS

USB-päivityksen jälkeen `air.bound=true`, `sensor_fresh=true`; BLE-mainokset vastaanotetaan, `rejected_count=0` tarkistushetkellä, `physical_writes_allowed=false`. NVS-sidonta säilyi tämän USB-päivityksen yli. 168 tunnin historian pitkä testi, CRC-checkpointin power-cycle ja säilyvyys **OTA-päivityksessä** ovat vielä avoimia.

## CN105:n tuoreusongelma

Yhteyden heartbeat eteni 55:stä 140:een, mutta ilmastointitapahtumien ja kompressoritaajuuden laskurit eivät nousseet kokeen aikana. SSE:n elossaolo ei siten todista pumpulta tulleen uuden lukeman ikää. Nykyinen 180 sekunnin fail-closed-tarkistus säilytettiin. Ei keksitty uusia mittausaikaleimoja eikä muutettu sillan firmwarea. Jatkossa vaaditaan pumpun todellisen päivityksen lähdelaskurin/aikaleiman selvitys lukurajapinnasta; ilman sitä hyväksyntäeste jää voimaan.

## OTA:n tekninen tila ja turvaeste

`default_16MB.csv`: NVS `0x9000/0x5000`, otadata `0xe000/0x2000`, `app0` `0x10000/0x640000`, `app1` `0x650000/0x640000`. Molemmat firmwarepaikat ovat 6,25 MiB. Käytössä olevan Arduino/ESP-IDF-kehyksen ESP32-S3 SDK:ssa `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1`, mutta **fyysisen käynnistysvahvistuksen, viallisen imagen ja rollbackin kokeet puuttuvat**. USB-flashatun app0:n OTA-tila API:ssa `-1`; se ei ole todiste onnistuneesta A/B-palautuksesta.

**`ota_upload_enabled=false`: OTA:n lataus ei ole valmis eikä avattu käyttöliittymään.** Allekirjoitettu manifesti, allekirjoituksen validointi, käyttäjän todentaminen, CSRF, TLS-varmenteen varmennus, version/hardware-rajoitus, keskeytyneen latauksen käsittely ja fyysinen rollback-testi **ovat vielä toteuttamatta/todentamatta**. Nykyistä HTTP/WebServerin paikallista asetustokenia ei saa sellaisenaan käsitellä firmwarepäivityksen käyttäjätodennuksena. OTA:n avaaminen ennen turvallisia portteja olisi vaarallista. Salaisia allekirjoitusavaimia tai yksilöllisiä laitetunnisteita ei tuotu GitHubiin, eikä eFuseja ohjelmoitu.

## Testit

- Host `g++ -std=c++17 -Wall -Wextra -Werror` memory-policy -testi: **PASS**.
- ESP32-S3 PlatformIO Arduino täysfirmwaren käännös: **PASS** myös viimeisillä diagnostiikkamuutoksilla. Viimeinen paikalliskonfiguraation käännös: staattinen RAM **84 788 B**, flash **1 617 705 B**.
- Fyysinen 240 s C2.4 USB-version koe: **39/39 PASS**, ei muistiloppua; kuitenkin kaikki hyväksyntäportit eivät täyty.
- Täydelliset Debug/Release CTest sekä Python-regressiot: **ei ajettu tässä C2.4-työkopiossa** (työkopio sisältää firmwaren ja valikoidut lähteet, ei koko repositoryn testiympäristöä). CMakeen on lisätty uusi testikohde, mutta sitä ei saa merkitä täyden CTest-ajon tulokseksi.
- Staattinen analyysi, väärän allekirjoituksen 10 fyysistä testiä, CN105-lähdetuoreuden todentaminen, watchdog-/power-cycle-/rollback-kokeet: **auki**.
- 24 h vakaustesti ja mahdollinen 48 h testi: **ei suoritettu**. Toisen C2.2-mittauskeräimen prosessiin ei koskettu.

## Julkaisupäätös

**PR #7 pysyy draftina. PR #5, #6 ja #7 eivät yhdisty mainiin.** Kortti pysyy sisällä. Ei automaattista C3-ohjausta, ei alle 16 asteen Mitsubishin HEAT/OFF-pulssitusta, eikä mitään CN105:n tai Shellyn fyysistä kirjoitusta. OTA:n tuotantohyväksyntä, koko 24 h vakausnäyttö, CN105-lähdemittauksen tuoreus ja erillinen pihamökin pakkassuoja ovat avoimia.

## Uusimman muistidiagnostiikan toinen USB-flash ja jälkitesti

- Uusin paikalliskonfiguraatiolla käännetty diagnostiikkaversio kirjoitettiin **vain app0-osioon** USB:n kautta; esptool vahvisti `Hash of data verified` ja resetin. Flashatun kuvan SHA-256 paikallisessa testilokissa: `194d8987da470b23f3a51d5a8e83e507aa892f295fdd17a7ac1bf9c68e62ba1f`.
- Uusi `observed_min_largest_internal` -kenttä näkyy fyysisessä HTTP-rajapinnassa. Käynnistysjakson näytteistetty suurimman yhtenäisen sisäisen muistilohkon minimi **49 140 B** ja sisäisen RAMin ilmoitettu minimi **54 836 B**. BLE, CN105 SSE, Shelly GET, Nord Pool, FMI ja käyttöliittymä toimivat statusmittauksissa; toiminnallista pitkää toistoa **ei vielä ajettu tästä uusimmasta buildista**.
- RuuviTagin sidonta säilyi NVS:ssä ja uusia Ruuvi-mainoksia löydetään, mutta noin 145–185 s mittaisessa käynnistysjaksossa **sidottua anturia ei ollut saatu luettua**: `bound=true`, `sensor_fresh=false`, `sample_count=0`. Viimeksi havaittu mainostava anturi ei täsmännyt sidotun anturin tunnisteeseen (tunnisteita ei tallennettu raporttiin). Tämä on **avoin uusi laitehyväksynnän este** kunnes sidottu anturi nähdään ja oikea mittaus palautuu. Sidontaa ei poistettu eikä vaihdettu.
- `ota_upload_enabled=false`, `ota_image_state=-1`: tämä on USB-flash eikä OTA-hyväksyntä. Ei fyysistä rollover/rollback-koetta, ei pidemmän muistitestin tulosta.
