# ENEVINA Lite C2 – riippumaton koodi- ja laitekatselmointi, 16.11.2026

**Lähtö-HEAD:** PR #5 `28e69b597c1ba72a3742715240cfe9fbbda23dcf`. Tämä raportti jatkaa aiempaa `PRE_CODEX_C2_REVIEW_2026-11-16.md`-katselmointia. PR #5 pidetään luonnoksena; **CI:tä ei käynnistetty, Codex-uusintakatselmointia ei pyydetty eikä PR:iä yhdistetty**.

**C2-kokonaisintegraatio EI ole hyväksytty.** Ohjelmisto- ja synteettinen laitevertailu on edennyt, mutta täysi samanaikainen Wi-Fi + CN105 + Shelly + Nord Pool + FMI + BLE + Web UI + MPC -koe sekä 24–48 tunnin vakausajot ovat edelleen tekemättä. Yhtään fyysistä HEAT/OFF- tai Shelly-relekomentoa ei ole lähetetty.

## Riippumattoman katselmoinnin löydökset ja korjaukset

1. **P1: CN105:n HTTP chunked -SSE:** aikaisempi suora `getStreamPtr()`-luku tulkitsi chunk-pituusrivit SSE-riveiksi ja saattoi menettää tapahtumia. Lisättiin rajattu HTTP chunk -purkaja ja rivikatkot sekä pitkät/katkeavat tapahtumat käsittelevä inkrementaalinen SSE-jäsennin `cottage/sse_transport.hpp`. Ohjain edelleen vain GET. Uusi PC-regressio kokeilee mielivaltaisia 1–511 tavun chunk-rajoja, pitkät rivit ja kesken jäävät tapahtumat.
2. **P1: epäkohdistettu lämpötilalähtötila:** aikaisempi MPC saattoi aloittaa seuraavan vartin alusta nykyhetken lämpötilalla. Ennusteen alku kohdistuu nyt nykyiseen UTC-varttiin, MPC sallitaan vain 60 sekunnin sisällä vartin alusta. Keskeltä jaksoa alkava syöte hylätään; virhetesti lisätty.
3. **P1: Release-assertit:** CMake -DNDEBUG poisti `assert()`-tarkistukset useista testeistä. Kaikille 14 C++-testiohjelmalle lisättiin `-UNDEBUG`, toimivuus vahvistettiin `flags.make`-tuloksesta.
4. **P2: Shelly HTTP-ruumis:** rajoitettu vastaus enintään 4096 tavuun ja vaadittu kelvollinen Content-Length ennen `getString()`-lukua; ei hyväksytä rajattomia paikallisen HTTP-palvelimen vastauksia.
5. **P1: vanhan suunnitelman näyttö:** verkkokatko, anturin tai hinnan/ennusteen katoaminen hylkää hyväksytyn suunnitelman; suunnitelma-API vaatii tuoreet anturit ja alle yhden vartin ikäisen suunnitelman.
6. **P1: PSRAM puuttui fyysisen täyden C2-firmwaren käytöstä:** käännös onnistui, mutta laiteloki näytti `psram=0`. PlatformIO sai `-DBOARD_HAS_PSRAM`, ja käynnistyksessä vaaditaan vähintään 7 MiB PSRAM. Fyysisen testikortin uusinta-ajossa 8 386 023 tavua havaittiin. Python-lähdetesti lisätty.
7. **P1: 192 jakson aikakatkaisu nykyisen yhdistelmä-MPC:n kanssa:** ennen korjausta fyysisellä ESP32:lla 192 jakson synteettisistä tapauksista 2/3 aikakatkaistiin 2000 ms kohdalla. `-O3` yksin ei riittänyt. MPC:n hakutilojen käynti rajattiin saavutettaviin tiloihin, ja hakunoodi/kustannuslaskenta siirrettiin ESP32:n laitteistotuetulle FP32:lle; **lopullinen simulaatio, turvallisuusrajat ja tuloksen hyväksyntä säilyivät FP64-fysiikkatarkistuksessa**. Aikaraja, keskeytyksen käsittely sekä täysi jälkisimulointi säilyvät. Ohjelmistotestit + fyysinen benchmark PASS.

## Paikalliset testit (CI:n sijasta)

| Testi | Näyttö |
| --- | --- |
| C++17 Release / GCC 16.1.0 | 14/14 CTest PASS, assertit päällä |
| C++17 Debug / GCC 16.1.0 | 14/14 CTest PASS |
| Python unittest | 10/10 PASS, myös write-path- ja PSRAM-lähdevalvonta |
| ESP32-S3 PlatformIO / Arduino 2.0.17 | Täysi C2 firmware kääntyi ja linkittyi; PSRAM mukana |
| Cppcheck 2.21 | Ei raportoitua turvallisuusvirhettä portable-ytimessä; 2 pienten POD-snapshotien palautukseen liittyvää suorituskykyhuomiota |
| Git diff --check | Läpäisty ennen julkaisua |

## Fyysisen ESP32-S3:n testit

Erillinen hyväksytty testikortti: ESP32-S3 rev 0.2, 240 MHz, 16 MiB flash, 8 MiB PSRAM, CH341 USB-/dev/ttyUSB1. Nämä eivät ole automaattisesti kehityskortin täydellisen mallin/pinoutin todisteita. Testikohteen CN105-sillan firmwarea ei flashattu.

- Täysi C2-firmware asennettiin testikortille ja käynnistettiin ilman WLAN-tunnuksia. Lokeissa `wifi=0, clock=0, cn105=0, shelly=0, ble=0, plan_slots=0, bad_reads=0`. Kahta 30 sekunnin tilariviä verrattiin; ei kaatumisia. Fyysisen PSRAM-korjauksen jälkeen vapaa PSRAM 8 358 071 B, yhteensä 8 386 023 B, vapaa sisäinen heap noin 234 180 B, minimi sisäinen 225 612 B.
- Nykyisen C++ MPC -ytimen **erillisessä** ESP32 benchmark-firmwaressa oli samanaikainen Wi-Fi AP + paikallinen HTTP-testiliikenne. Lopullinen FP32/FP64-versio: **96 jaksoa 175, 237, 246 ms (3/3 OK)**; **192 jaksoa 437, 526, 527 ms (3/3 OK)**. Pahin 527 ms, HTTP 2 onnistunutta, 0 epäonnistunutta; sisäisen heapin alaraja 244 048 B, PSRAMin alaraja 7 496 379 B. Mitatut testitapaukset pysyivät fysiikan ja minimi-ON/OFF:n jälkitarkistusrajoissa. `docs/benchmarks/c2-current-mpc-fp32-esp32s3-bench.log`.
- **Tämä 527 ms ei ole täyden verkkopalvelu-C2-firmwaren kokonaiskuormamittaus.** Todelliset SSE/GET, FMI/NordPool HTTPS, Ruuvi BLE ja paikallinen browser eivät olleet samassa benchmarkissa mukana.
- Testikortille palautettiin lopuksi täysi C2-varjotilafirmware. Testikohteen CN105 ja Shelly jäivät muuttumattomiksi.

## Riippumaton MILP-vertailu

`docs/benchmarks/c2-independent-fp32-milp.json`: SciPy/HiGHS, 8 synteettistä 96/192-jakson tapausta; 5 sekunnin aikaraja/MILP-ajo. Exact-replay-varmistetut CottageMPC-ajot eivät rikkoneet lämpötila-/toimintarajoja. Vertailun esimerkkilukuja:

| Tapaus | Jaksoja | Cottage MPC € | MILP best found € | Tila |
| --- | ---: | ---: | ---: | --- |
| Leuto | 96 | 3.05035 | 3.05035 | MILP aikaraja |
| Kylmä | 96 | 6.15848 | 6.03864 | MILP tavoite täyttyi |
| Hintapiikki | 96 | 5.09018 | 5.08955 | MILP aikaraja |
| Sulatus | 96 | 3.16187 | 3.16105 | MILP aikaraja |
| Leuto | 192 | 5.97877 | 5.98273 | MILP aikaraja |
| Kylmä | 192 | 12.04424 | 12.06526 | MILP aikaraja |
| Hintapiikki | 192 | 9.90210 | 9.99369 | MILP aikaraja |
| Sulatus | 192 | 6.21378 | 6.29002 | MILP aikaraja |

**Aikarajassa löydetty MILP-ratkaisu ei välttämättä ole globaali optimi.** MPC:n parempi kustannus joissakin 192-jakson tapauksissa ei todista globaalia optimaalisuutta. Säästöjä ei voi päätellä; rakennus-, COP- ja sulatusparametrit ovat synteettisiä, eivät rakennuksen mittauksista estimoituja.

## Fyysiset lukuyhteydet ja puuttuvat portit

- **Todellisella LAN:lla PC:llä testattu:** nykyisen Mitsubishin CN105 `GET /events` ilmoitti OFF, 12.0 °C, 0 Hz. Shelly Plus 1PM:n `GET /rpc/Switch.GetStatus?id=0` ilmoitti noin 166 W koko rakennuksen tehon, rele päällä. Kumpaankaan laitteeseen ei lähetetty kirjoituskomentoja.
- **Täydessä C2 ESP32:ssa ei vielä verkkotodennettu:** laite oli testikäynnistyksessä tarkoituksella ilman SSID/salasanaa. Nord Pool -hinnat, FMI HARMONIE, CN105/Shelly sekä BLE eivät ole yhdessä todetusti lukukäytössä ESP32:lla.
- **Riippumaton rakennuksen sisälämpötila-anturi ja erillinen jäätymissuoja** on yhä vahvistamatta. Pumpun oma lämpötila ei korvaa näitä, eikä Shellyn kokonaistehoa saa käyttää COP-mittauksena.
- **24–48 tunnin vakauskoe, NVS:n katko-/palautustesti, keskeytys/kalibrointi ja todellisen rinnakkaisverkon suorituskyky** ovat auki. Kääntynyt firmware ja 65 sekunnin offline-loki eivät riitä.
- C3-ohjaus edelleen kokonaan pois käytöstä. **Uutta Codex-katselmointia ei pyydetty.**

Tämä dokumentti erottaa tarkoituksella lähdekoodista tarkistetut, PC:llä testatut, erillisellä ESP32:lla mitatut sekä todellisista testikohteen laite-LAN-lukemista vahvistetut asiat.
