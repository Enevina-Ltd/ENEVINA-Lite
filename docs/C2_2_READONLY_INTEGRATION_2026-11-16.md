# Enevina Lite – C2.2 Wi-Fi-varjotilan integraation ja vakauden todentaminen

**Päivämäärä:** 16.11.2026. **PR:** #5, luonnos, kohde `main`. **Lähtö-HEAD:** `67b5b85adf306cd6b09059823859cf058c52a3c2`.

**Tila: C2.2 OSITTAIN TOTEUTETTU, EI HYVÄKSYTTY.** Koodia, laiteinstrumentointia, read-only-kenttäkeruuta ja fysikaalista USB-laitetestiä on parannettu ja todettu. Todellista samanaikaista Wi-Fi + CN105 + Shelly + Nord Pool + FMI + BLE + paikallinen Web UI + CottageMPC -ajoa **ei ole todennettu ESP32:lla**, koska paikalliset Wi-Fi-tunnukset ja testikohteen riippumattoman lämpöanturin identiteetti eivät ole käytettävissä. Yhtään oikean Mitsubishin tai Shellyn ohjauskomentoa ei ole lähetetty.

## 1. Muutokset

- `firmware/cottage/src/main.cpp`: erilliset mittaustapahtumien, aktiivisen SSE-yhteyden, yhteyskatkosten, Shelly GET -lukujen ja vasteaikojen laskurit; UTC-aikaleimat pumpun klimaatti-/kompressorimittauksille **erillään CN105-sillan yhteys-uptimesta**. Näkyvissä paikallisessa `/api/status`-rajapinnassa ja suomenkielisessä UI:ssa. Linkkiuptime **ei nollaa mittausten ikärajoja**.
- Status paljastaa FMI-/hintadatan todelliset viimeisimmät päivitysajat, jakso-/pistemäärät, Supervisorin turvallisuustilan, mallin kalibrointi- ja riippumattoman pakkassuojan portit, ESP32-sisäisen heapin/PSRAMin sekä valittujen tehtävien vapaan pinon alarajat. Salasanoja, MAC-osoitteita tai LAN-osoitteita ei julkaista.
- `tools/cottage_field_capture.py`: riippumaton, vain GET-yhteyksiä käyttävä paikallinen kenttäkeräin; CN105:n todelliset SSE-tapahtumat, Shellyn koko rakennuksen kulutus ja releen havainnointi, Nord Poolin senhetkinen spot-hinta, UTC-tapahtumat ja vasteajat. Noudattaa LAN-osoite-/polkurajausta, estää redirectit, rajoittaa JSON-syötteet ja säilyttää tulokset **vain paikallisessa käyttäjäkohtaisessa lokissa** (0600). Ei ohjauskutsuja.
- `tools/cottage_usb_stability.py`: todelliseen monotoniseen aikaan perustuva, read-only USB-sarjaporttiloki reboot-, heartbeat-, watchdog-/fatal- ja yli 90 sekunnin tilapäivityskatkoille. Tämä ei simuloi tuntien kulumista.
- `tests/test_cottage_field_capture.py` ja `tests/test_cottage_usb_stability.py`: 6 Python-regressiotestiä. Puhdistettu aiempien C++-testien todelliset LAN-osoitteet ja korvattu benchmarkin kiinteä AP-host arvolla `WiFi.softAPIP()`.
- Ei muutosta yhdistettyihin PR #1/#4 -toimintoihin, ei tuotantojärjestelmien muokkauksia, ei CI:tä eikä Codex-uusintakatselmointia.

## 2. Fyysinen ESP32

Erillinen, aiempaan testiin varattu **ESP32-S3 rev 0.2 N16R8**: 16 MiB flash ja 8 MiB PSRAM, USB-UART `/dev/ttyUSB1`. Testikohteen käytössä olevaan CN105-siltaan ei koskettu.

- Ennen C2.2-muutoksia: täysi varjotilafirmware käynnistyi ilman Wi-Fiä 68 sekunnin seurantajakson aikana, kaksi 30 sekunnin tilariviä, ei kaatumisia.
- C2.2-versio: `esp32s3_cottage_shadow` rakennettiin erillisellä kehityskoneella ja flashattiin **vain tähän korttiin**. Flash-kirjoitus 1 010 320 tavua, tarkistushash hyväksytty. Uudet `C2_SHADOW`-tilarivit vahvistavat käynnissä olevan täyden varjotilan.
- Alustava uusi fyysinen seurantamittaus (ei pitkäaikaisen testin tulos): sisäinen vapaa heap **234 088 B**, vapaa PSRAM **8 358 071 B**, kokonais-PSRAM **8 386 023 B**; sisäisen heapin siihenastinen alaraja **233 864 B**. Wi-Fi, NTP, CN105, Shelly, BLE ja hyväksytty MPC-suunnitelma 0, kuten puuttuvilla asetuksilla kuuluukin.
- Fyysisen MPC:n aiemmat hyväksytyt **synteettiset benchmarkit** 96 jaksoa 175–246 ms (3/3), 192 jaksoa 437–527 ms (3/3). **Näitä ei uusittu C2.2:ssa eivätkä ne ole todellisen verkkokuorman mittauksia.** Täysi C2.2-firmware ei ole vielä laskenut 192 jakson MPC:tä oikeilla lähteillä.

## 3. Oikeat read-only-laitelukemat kehityskoneella

Kehityskoneen erillinen LAN-testi, **ei väitetty ESP32:n verkkoluvuksi**:

| Lähde | Havainto |
|---|---|
| CN105 SSE | HTTP 200, `Content-Type: text/event-stream`, `Transfer-Encoding: chunked` |
| Mitsubishin tila | `OFF`, pumpun sisäilma 12,0 °C, tallennettu asetus 21,0 °C (ei annettu komento) |
| Kompressori | 0 Hz, vastaanotettu SSE-tapahtumana |
| CN105-yhteyden uptime | Uusia tapahtumia 38 / 105 s; päivitysvälin mediaani 2,83 s |
| Pumpun ilma-/kompressorilukema | Kumpikin tuli **vain kerran SSE-yhteyden alkuhetkellä** 105 s aikana. Pidempää aktiivista uudelleenmittaussykliä ei ole todistettu |
| Shelly Plus 1PM | 11/11 GET onnistui, rele koko ajan ON; koko rakennuksen teho 165,4–166,4 W, laskurin kasvu 4,606 Wh / 105 s; GET-viive 29,0–116,4 ms |
| Nord Pool | HTTPS GET 200, kyseiselle päivälle 96 suomalaista varttijaksoa; vertailuhetken spot 0,13987 €/kWh |
| FMI HARMONIE | HTTPS WFS GET 200, 50 ennustepistettä. Tarkka kohdekohtainen koordinaatti ja ESP32:n oma lataus odottavat |

Yhteyden uptime todistaa vain sillan ilmoittaman yhteystilan, **ei automaattisesti uutta lämpötilamittausta tai UART-vastausta**. `valid_cn105()` säilyttää nykyisen tiukan mittauskohtaisen 180 s:n tuoreusportin. Tasaisessa OFF-tilassa se saattaa siksi estää optimoinnin. Tämä on turvallinen mutta toiminnallinen este: C2 ei saa hiljaisesti kiertää sitä siirtämällä vanhan mittauksen aikaleimaa.

Raaka, aikaleimattu kenttäloki pidetään vain kehityskoneen omistajalle näkyvässä `private-telemetry/`-hakemistossa; GitHubiin tallennetaan vain tämä osoitteeton yhteenveto.

## 4. Paikallinen testaus ilman CI:tä

| Koe | Tulos |
|---|---|
| GCC/CMake C++17 Release CTest (assertit aktivoitu) | **14/14 PASS** |
| GCC/CMake C++17 Debug CTest | **14/14 PASS** |
| Python unittest, mukana 6 uutta read-only-/logger-testiä ja 2 mittaustuoreusportin lähdetestiä | **18/18 PASS** |
| Cppcheck 2.21 portable-ydin | **Ei raportoituja varoitus- tai virhehavaintoja** |
| ESP32-S3 PlatformIO `esp32s3_cottage_shadow` | **BUILD/LINK PASS**. Staattinen RAM 60 228 B, koodi noin 1 009 953 B |
| ESP32-S3 `esp32s3_cottage_bench` | Vain käännöstarkistus; fyysisiä benchmark-arvoja ei kerätty tällä muutoksella |
| Git diff --check | PASS |

Uusinta-MILP (`tools/cottage_reference.py`, SciPy/HiGHS, 5 s MILP-aikaraja per tapaus): kahdeksan synteettistä 96/192-jakson tapausta. **CottageMPC:n exact-replay ei raportoinut yhtään rajoitusrikkomusta.** MILP saavutti hyväksymistilan 0 vain kylmässä 96-jakson tapauksessa; seitsemän muuta päättyivät aikarajaan tilalla 1. C++/MILP-kustannukset (€):

| Skenaario | 96 jaksoa C++ / MILP | 192 jaksoa C++ / MILP |
|---|---:|---:|
| Leuto | 3,05035 / 3,05035 | 5,97877 / 5,98273 |
| Kylmä | 6,15848 / 6,03864 | 12,04424 / 12,06526 |
| Hintapiikki | 5,09018 / 5,08955 | 9,90210 / 9,99369 |
| Sulatus | 3,16187 / 3,16105 | 6,21378 / 6,29002 |

**MILP:n aikarajan löytämä kustannus ei todista optimaalista alarajaa.** Synteettiset COP-/lämmönjohtavuus-/sulatusparametrit eivät ole rakennuksen mitattua fysiikkaa. Ei todellisia säästölupauksia.

## 5. Käynnistetty todellinen vakauden ja kenttäaineiston keruu

Kaksi pitkäkestoista, **aikaan sidottua** prosessia käynnistettiin kehityskoneella 16.11.2026:

- USB-sarjaportin ESP32-vakauskeruu alkoi **13:33:47 UTC** (15:33:47 Suomen aikaa), tavoitteena 86 400 s todellista seurantaa.
- Todellisen CN105/Shelly/Nord Pool -kenttäaineiston read-only-keruu alkoi **13:33:52 UTC**, myös 86 400 s todellista seurantaa.

Lähtöhetken tarkistuksessa molemmat prosessit olivat käynnissä, 0600-lokit kirjoittuivat ja C2-laitteen heartbeat uusiutui 30 sekunnin välein. **24 h ei ole tässä raportissa läpäisty eikä 48 h vakausajo ole käynnistetty.** Testit voidaan hyväksyä vasta kellon mukaan valmistuneista lokeista, katkoksista ja todellisesta kestoajasta, ei tämän asiakirjan perusteella.

## 6. Avoimet P0/P1-portit ja riippuvuudet

1. **Wi-Fi-tunnukset puuttuvat** erillisen kortin paikallisesta `cottage_local_config.hpp`-konfiguraatiosta. Niitä ei arvata, haeta luvatta tai julkaista. Näin ollen täyttä ESP32:n Wi-Fi/CN105/Shelly/Nord Pool/FMI/UI-rinnakkaistestiä ei voitu aloittaa.
2. **Riippumaton testikohteen lämpöanturi:** mitään rakennuksen anturin MACia ja sijaintia ei ole vahvistettu. Talon muut RuuviTagit eivät korvaa sitä. BLE-live-portti auki.
3. **CN105:n pumpun data ei tuoreennu varmasti** pysyvässä tilassa. Yhteysuptime ei ole sama kuin uusi ilmamittaus. Vanhentunutta dataa ei anneta MPC:lle.
4. **Mallin kalibrointi:** luotettavia jäähtymisjaksoja, riippumatonta sisäilmaa, yksilöityä lämpöpumppusähköä eikä todellista lämpökapasiteettia ole kertynyt. Ei kalibroitua rakennus-/COP-/sulatusmallia; `COTTAGE_SYNTHETIC_TEST_MODEL=0` säilyy.
5. **Todellinen 192 jakson aika samalla ESP32:lla kaikkien verkkolukijoiden käydessä** puuttuu. Edellisen synteettisen vertailun tulos ei korvaa tätä.
6. **Fyysiset vikatestit** (Wi-Fi-/CN105-/Shelly-/BLE-katko, NTP puuttuu, NVS palautus, heap-alaraja, watchdog) tarvitsevat full-integration-laitetestin; PC-regressiot eivät korvaa niitä.
7. **Erillinen jäätymissuoja** ei ole todennettu. Tämä on C3:n ehdoton esto. Olemassa olevan pumpun ja Shellyn asetuksia ei muuteta.
8. **Selain-UI:n toiminta oikeassa Wi-Fi-verkossa** ja sen verkkoeristys eivät ole vielä laitetestattu. C2:n GET-only-tila ei ole vielä C3:n hyväksytty tuotantohallinta.

### Yhteenveto

C2.2 on edelleen **read-only/shadow** eikä ohjaa lämpöpumppua tai Shellyä. Sen kenttämittausten erillisyys, paikallinen diagnostiikka, vikatilojen näkyvyys ja toistettava testi-infrastruktuuri paranivat. PR #5 pidetään luonnoksena. **Ei CI:tä, ei Codex-katselmointia, ei main-mergeä eikä C3-käyttöönottoa.**

Todellinen Wi-Fi-yhteistesti odottaa paikallisia verkkoasetuksia. Pitkäaikaisesta vakaudesta voidaan tehdä hyväksyntäpäätös vasta aikaleimallisten lokien täytyttyä.

## Julkaistut, osoitteettomat testitiedostot

- [105 sekunnin todellisen laitekeruun yhteenveto](benchmarks/c2_2_field_105s_summary.json)
- [Fyysisen ESP32:n osittainen sarjaporttinäyte](benchmarks/c2_2_esp32_partial_serial_summary.json)
- [MILP-uusinta: 8 synteettistä skenaariota](benchmarks/c2_2_milp_recheck_20261116.json)
