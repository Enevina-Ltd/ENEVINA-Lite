# Kalibroitu MPC elävään ohjausketjuun (20.11.2026)

**Näyttötaso: OHJELMISTO VALMIS + HOST-TESTATTU + SIMULOITU.** Mitään tästä
ei ole ajettu oikealla lämpöpumpulla, eikä ESP32-korttia ole flashattu.
Fyysiset komennot kulkevat edelleen vain erikseen aktivoidun CN105-pilotin
ja kaikkien nykyisten porttien kautta. Säästöjä ei väitetä.

*English summary at the end.*

## 1. Mitä muuttui

| Osa | Ennen | Nyt |
|---|---|---|
| Rakennusmalli | Ei mallia; MPC vain `COTTAGE_SYNTHETIC_TEST_MODEL`-testimallilla | `cottage/building_identification.*` oppii mallin omista mittauksista (RLS) ja testaa sen ennustavasti |
| `model_calibrated` / `mpc_ready` | Aina `false` | Todellinen tila: `NO_DATA` → `LEARNING` → `VALIDATING` → `CALIBRATED` (tai `DEGRADED`) |
| Ennuste | Vaadittiin täysi 48 h tai 24 h, muuten ei mitään | `join_forecast_horizon()`: pisin todennettu yhtenäinen hinta- ja säähorisontti, 8–48 h |
| Ohjaus | `bootstrap_preview()` (hintaohjaus) | `active_control_proposal()`: kalibroitu ja voimassa oleva MPC → hintaohjaus → termostaatti |
| Komennon tila | – | MPC-päätös näytetään toteutuneena vasta, kun pumpun takaisinluku näyttää suunnitellun tilan |
| Käyttöliittymä | MPC-taulukko vain testimallilla | MPC-kortti: tila, kalibrointi, mitattu vs. ennustettu, sää, hinnat, suunnitelma; energia vain luotettavana |

MPC-algoritmia (`cottage/mpc.cpp`, rajattu DP + tarkka uudelleensimulointi)
ei kirjoitettu uudelleen. Siihen lisättiin kaksi parametria, joiden
oletusarvo säilyttää vanhan toiminnan bittitarkasti (kaikki vanhat testit ja
vertailut ennallaan):

1. `Parameters::stop_afterheat_kwh` (oletus 0): HEAT→OFF-siirtymän jälkilämpö,
   sama termi `advance()`:ssa ja haussa.
2. `MpcRunControls::terminal_min_c` (oletus NaN = vanha ehto *loppu ≥ alku*).
   Liukuvassa horisontissa vanha ehto nostaa huonetta jokaisen esilämmityksen
   jälkeen (seuraava suunnitelma vaatii loppumaan yhtä lämpimänä) ja tekee
   ylärajan lähellä suunnitelmasta ratkeamattoman. Suljetun silmukan
   simulaatiossa tämä näkyi ennen korjausta 8–51 `INFEASIBLE`-ratkaisuna
   skenaariota kohti ja keskimäärin 0,4 °C liian lämpimänä huoneena.
   Elävä ohjaus käyttää loppuehtona huonetavoitetta.

## 2. Mitä mittauksista voidaan tunnistaa – ja mitä ei

Malli on täsmälleen sama diskreetti lämpötase kuin `advance()`:

`T[k+1] − To = a·(T[k] − To) + θ1·u + θ2·u·To + θ3·käynnistys + θ4·pysäytys + θ5`

missä `u` on HEAT 16 °C -pyyntö (0/1) koko varttitunnin ajan, `a = exp(−0,25 h/τ)`.
Kuusi parametria sovitetaan rekursiivisella pienimmän neliösumman menetelmällä
(unohduskerroin 0,9995 ≈ 21 vrk muisti, kovarianssin jäljen yläraja,
symmetrisointi, poikkeavien mittausten hylkäys > 6σ).

**Tunnistettavissa sisä-, ulko- ja HEAT/OFF-tiedosta:**
- aikavakio τ = C/UA (h) ja sen epävarmuus,
- lämmityksen nosto G(To) = Q_on(To)/UA (K) ja epävarmuus mitatun ulkolämpöalueen keskellä,
- käynnistysviive (min) ja jälkilämpö (K),
- vakio lisälämpö (sisäiset ja keskimääräiset aurinkokuormat).

Nämä määrittävät kaikki sisälämpötilan ennusteet, joita MPC tarvitsee.

**Ei tunnistettavissa ilman lämpöenergian mittausta:** absoluuttinen
lämpökapasiteetti C (kWh/K), häviökerroin UA (kW/K) ja COP. Jos kaikki
lämpövirrat kerrotaan samalla vakiolla, mittaukset eivät muutu. Siksi:
- COP:ta **ei koskaan arvioida** (`cop_identified=false`).
- Kun omistaja on vahvistanut Shellyn mittaavan **vain lämpöpumppua**,
  sähköteho P_on(To), valmiustila ja käynnistysenergia sovitetaan erikseen,
  ja fysikaalisesta ehdosta COP ≥ 1 saadaan **alarajat** UA ≥ max P/G ja
  C ≥ τ·UA. Raja ei ole arvio, eikä sitä esitetä arviona.
- Koko talon tai vahvistamattoman mittarin dataa ei käytetä mihinkään
  malliin. Mainissa ei ole vielä `shelly_scope`-asetusta (tulee PR #58:ssa),
  joten `shelly_meter_pump_only()` palauttaa `false`. Mittari näytetään
  käyttöliittymässä erillisenä mitattuna kulutuksena ja merkintänä
  ”mittauskohde vahvistamaton”.

Ilman lämpöpumppukohtaista mittaria MPC käyttää normalisoitua yksikköä: yksi
HEAT-varttitunti on yksi energiayksikkö. Kustannus on hinnalla painotettu
käyntiaika. Ennustettua energiaa ja kustannusta **ei näytetä**
(`energy_valid=false`).

## 3. Kalibroinnin tila ja kynnysarvot

Kalibrointi ei synny oletusarvoista eikä sovituksen omasta residuaalista.
Se edellyttää ennustavaa testiä: jokainen 4 h ikkuna ennustetaan avoimena
silmukkana sen alussa jäädytetyillä parametreilla ja verrataan myöhempiin
mittauksiin.

| Siirtymä | Ehto (`CalibrationLimits`) |
|---|---|
| `LEARNING` → `VALIDATING` | ≥ 192 kelvollista varttia, ≥ 32 HEAT ja ≥ 32 OFF, ≥ 4 käynnistystä ja ≥ 4 pysäytystä, τ 2–500 h, G 0,5–80 K koko mitatulla alueella, σ(τ)/τ ≤ 0,30, σ(G)/G ≤ 0,20 |
| `VALIDATING` → `CALIBRATED` | Viimeiset 12 ikkunaa: RMS ≤ 0,50 °C, harha ≤ 0,25 °C, suurin virhe ≤ 1,5 °C, ≥ 4 ikkunassa tilanvaihto |
| `CALIBRATED` → `DEGRADED` | Viimeisten 4 ikkunan RMS > 1,0 °C tai 12 ikkunan raja ylittyy |
| `CALIBRATED` → `VALIDATING` | Ei uutta testi-ikkunaa 14 vrk:ssa tai kello on viimeisen testin takana |

Rajat ovat dokumentoituja suunnitteluvalintoja, eivät kenttätodennettuja.
MPC:n turvamarginaali on 2 × testi-RMS (0,2–2,0 °C). Suunniteltu minimi
(lattia + marginaali) on sama `tavoite − 0,5 °C`, jossa nykyinen
termostaatti/hintaohjaus käynnistää lämmityksen. Opittavat varttitunnit
hylätään, jos HEAT/OFF muuttui kesken jakson, tila ei ole tiedossa,
asetusarvo on yli 16 °C (HEAT 16 °C -mallin ulkopuolella), sisälämpö on yli
15,3 °C lämmitettäessä, mittaus puuttuu tai jatkuvuus katkesi. MPC ei
myöskään ekstrapoloi: jos ennustettu ulkolämpö on yli 3 K opitun alueen
ulkopuolella, käytetään varatilaa `OUTDOOR_OUTSIDE_LEARNED_RANGE`.

Ilman historiaa hintaohjaus ja termostaatti ohjaavat normaalisti, ja malli
oppii passiivisesti. Tilaa voi seurata `/api/mpc`-rajapinnasta ja MPC-kortista.
Malli tallennetaan NVS:ään (`cottage_mdl/rls1`, noin 1,3 KiB, CRC, versio)
enintään 6 tunnin välein ja vain, jos NVS:ssä on runsaasti vapaata tilaa.
Wi-Fi-, OTA- ja aktivointitietueita ei siis voida syrjäyttää. Malli on
sidottu pumpun sovittimeen, CN105-osoitteeseen ja sääpaikkaan, ja muutos
aloittaa oppimisen alusta. `POST /api/model/reset` (sama asetustoken kuin
muissa asetuksissa) nollaa mallin.

## 4. Ennuste, hinnat ja liukuva horisontti

`join_forecast_horizon()` palauttaa pisimmän todennetun horisontin, enintään
192 varttia (48 h) ja vähintään 32 varttia (8 h):
- **Hinnat**: tiukasti yhtenäiset UTC-vartit nykyisestä jaksosta. Ensimmäinen
  puuttuva, kaksoiskappaleena tai hinnoittelukelvottomana oleva vartti
  päättää horisontin. Kokonaishinta lasketaan samalla `site_slot_total_price()`
  -säännöllä kuin hintaohjauksessa (tariffi, ALV, valuutta, paikallinen
  aikavyöhyke). Negatiiviset hinnat hyväksytään. Kesäaikapäivinä (92/100
  varttia) mikään ei oleta 96 jaksoa.
- **Sää (MET Norway)**: tunnin välein yhtenäinen pisterivi nykyhetkestä.
  Puuttuva tunti päättää horisontin, eikä aukkoja täytetä.
- **Vanhentunut** data on käyttökelvotonta: hinnat yli 36 h tai sää yli 6 h
  vanhoja, tai haku tulevaisuudessa. Silloin MPC ei suunnittele
  (`FORECAST_UNAVAILABLE`) ja hintaohjaus tai termostaatti jatkaa.

MPC lasketaan jokaisen varttitunnin alussa (≤ 60 s rajasta), ja pumpulle
viedään vain ensimmäinen päätös. Suunnitelma kelpaa vain oman varttinsa
ajan ja vain, jos tila-, asetus- ja tavoitegeneraatiot ovat ennallaan ja
pumpun tila on suunnitelman lähtötila tai sen ensimmäinen päätös.
Käynnistykset, 30 minuutin minimikäynti- ja seisonta-aika (sama kuin
ohjausportin `ModeDwell`) sekä kytkentäsakko ovat mukana. HEAT tarkoittaa
pumpun omaa 16 °C:n asetusarvoa, jota Mitsubishin sovitin tukee todennetusti
(`mitsubishi_valid_setpoint`).

## 5. Ohjausketju ja turvaportit

```
optimizer-tehtävä (varttitunnin raja)
  ├─ edellisen vartin havainto → BuildingIdentifier.add()
  ├─ join_forecast_horizon() → plan_mpc() → MpcPlanRecord (julkaistaan lock_datan alla)
  └─ cottage_control_tick()
        active_control_proposal(): arbitrate(kalibroitu MPC → hinta → termostaatti)
        └─ choose_mitsubishi_control()  ← ennallaan: aktivointi, tuoreus, asetusarvo, 30 min viive
              └─ cn105_bridge_post()     ← ennallaan + sama arbitroitu ehdotus tarkistetaan 2× ennen POSTia
                    └─ takaisinluku: MPC-päätös "vahvistettu" vain jos pumppu näyttää suunnitellun tilan
```

- Midea ja muut vain luku -sovittimet eivät koskaan pääse CN105-kirjoitukseen
  (`if(!cn105_selected())` ennen ehdotusta). Vain luku -tilassa tehdään
  korkeintaan noin 3 minuutin välein yksi CN105-GET, jotta tilahistorian
  5 minuutin jatkuvuus säilyy ja malli voi oppia.
- Uusia watchdogeja, uudelleenkäynnistyksiä tai tehtäviä ei lisätty.
  MPC:n ajoaikaraja on 1 800 ms, ja sen peruu Wi-Fi-katko tai web-silmukan
  pysähtyminen (`cottage_cancel`). Aikakatkaisu johtaa varatilaan, ei
  osittaiseen suunnitelmaan.
- Varatilan syy näkyy aina (`/api/status`: `mpc_state`, `mpc_fallback`,
  `control_source`; `/api/mpc`).

## 6. ESP32-S3-resurssit

| Mitattu | Missä | Tulos |
|---|---|---|
| `plan_mpc()` 48 h (192 varttia), pahin / keskiarvo | x86 PC, Release, 4 tapausta × 20 | 2,0 ms / 1,5 ms |
| `plan_mpc()` 24 h (96 varttia) | x86 PC, Release | 0,83 ms / 0,7 ms |
| Yhden suunnitelman todellinen kekohuippu (laskettu `operator new`) | x86 PC | 448 224 B (192), 236 256 B (96); budjetti 3 MiB |
| Mallin päivitys / parametrien muodostus | x86 PC, Release | 0,14 µs / 0,16 µs |
| Firmwaren koko `esp32s3_cottage_shadow` | `pio run` | Flash 2 062 757 B (main 1 963 693 B, +99 064 B), staattinen RAM 91 148 B (main 90 620 B, +528 B) |
| Sama yhdistettynä PR-pinon #62 kärkeen | `pio run` | Flash 2 310 561 B (35,3 %), RAM 100 460 B (30,7 %) |

Ei mitattu (vaatii laitteen ja omistajan luvan flashaukseen): ESP32-S3:n
ajoaika, todellinen PSRAM/heap-käyttö ja fragmentoituminen, pinon
korkeusmerkit sekä watchdogin käytös kuormassa. Arvio, ei mittaus:
aiempi laitemittaus (`benchmarks/c2-current-mpc-fp32-esp32s3-bench.log`)
antoi samalle hakualgoritmille noin 33× x86-ajan, eli 48 h suunnitelma vie
arviolta 70 ms ja jää selvästi alle 1 800 ms:n budjetin.

Kehyksen asetuksista (Arduino-ESP32 2.0.17, `sdkconfig`) tarkistettu:
yli 4 KiB:n varaukset menevät PSRAMiin (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`),
joten DP-taulukot ovat PSRAMissa. Tehtävä-watchdog on 5 s ja tarkkailee vain
CPU0:n idle-tehtävää, kun taas optimizer toimii ytimellä 1. Suuret puskurit
(horisontti 6,2 KiB, suunnitelmarivit 10,7 KiB, malli 3,6 KiB, trendi)
varataan PSRAMista, eivät 16 KiB:n tehtäväpinosta. `/api/status` raportoi
nyt myös `loop_stack_free` laitetodennusta varten.

## 7. Käyttöliittymä ja rajapinta

- `GET /api/mpc`: tila ja syy, ohjauksen lähde, kalibrointi (otokset,
  testit, RMS/harha, marginaali), tunnistettu malli epävarmuuksineen
  (`null`, kun ei mallia), mittarin rooli, suunnitelma (laskenta-aika,
  horisontti ja sen päättymissyy, rivit: HEAT/OFF, ennustettu sisälämpö,
  ulkolämpö, hinta; kWh/kustannus vain luotettavana) sekä 24 h mitattu vs.
  ennustettu -trendi.
- `GET /api/plan`: kelvollinen vain kalibroidun mallin kuluvan vartin
  suunnitelmalle. JSON ei sisällä `NaN`-arvoja, ja kWh/€ ovat `null`, ellei
  energiamalli ole luotettava.
- Optimointinäkymän MPC-kortti (FI/EN, mobiiliasettelu, yöteema
  CSS-muuttujista) näyttää tilan (käytössä / kalibroidaan / varatila ja syy),
  kalibroinnin, mallin, kaavion (mitattu, ennustettu, suunniteltu, ulkolämpö,
  hinnat, lämmitysjaksot, mukavuusalue), seuraavan päätöksen ja sen
  vahvistuksen, päivitysajan sekä mitatun kulutuksen erikseen. Säästöjä ei
  lasketa.

## 8. Riippumaton suljetun silmukan vertailu (SIMULAATIO)

`tools/cottage_closed_loop_cli.cpp` ja `tests/cottage_closed_loop.hpp`: ”tosi”
rakennus (oma parametrinsa, jälkilämpö, käynnistysviive, mallille tuntematon
päivittäinen aurinkokuorma 0,3 kW, anturikohina 0,03 °C, ulkolämpöennusteen
virhe kasvaa ennustepituuden mukana, päivänhinnat julkaistaan klo 14
paikallista aikaa). Kolme ohjainta saavat **samat** rakennukset, sään,
hinnat ja alkutilan, ja kaikilla on 30 minuutin tilaviive:

- termostaatti (tavoite ± 0,5 °C),
- Liten nykyinen hintaohjaus `bootstrap_price_decision()`,
- kalibroitu MPC: malli opitaan 14 vrk:n hintaohjaushistoriasta rakennuksen
  omista mittauksista, minkä jälkeen ajetaan täsmälleen firmwaren
  `plan_mpc()` → `arbitrate()` -ketju joka vartti.

Arviointijakso on 3 vrk, tavoite 12 °C ja alue 11,5–13,0 °C.
**Yhtä suuri loppuenergia:** ”Equal-energy €” lisää kustannukseen sähkön,
joka tarvitaan palaamaan yhteiseen alkulämpötilaan (tosi COP
loppuulkolämmössä, jakson keskihinta). Raakadata:
[`benchmarks/mpc-closed-loop-20261120.json`](benchmarks/mpc-closed-loop-20261120.json),
taulukko [`benchmarks/mpc-closed-loop-20261120.md`](benchmarks/mpc-closed-loop-20261120.md).

| Skenaario | Termostaatti € | Hintaohjaus € | MPC € | Alueen alla K·h (T / H / MPC) | Käynnistykset (T / H / MPC) | MPC aktiivinen |
|---|---:|---:|---:|---|---|---|
| kylmä jakso (−4 → −11 °C) | 11,35 | 10,89 | 10,85 | 0,57 / 0,65 / 0,03 | 13 / 17 / 15 | 229/288 |
| nopea lämmitys (9 → 12 °C) | 7,29 | 6,85 | 6,74 | 7,61 / 7,62 / 7,53 | 20 / 23 / 23 | 260/288 |
| hintapiikki 1,2 €/kWh | 9,84 | 9,11 | **8,05** | 0,26 / 0,14 / 0,00 | 20 / 26 / 28 | 285/288 |
| halpa yö, kallis aamu | 10,91 | 10,46 | **10,00** | 0,26 / 0,10 / 0,00 | 20 / 22 / 26 | 285/288 |
| negatiiviset hinnat | 5,90 | 5,50 | **5,07** | 0,26 / 0,16 / 0,00 | 20 / 25 / 25 | 285/288 |
| tasainen hinta | 9,26 | 9,26 | 9,35 | 0,26 / 0,26 / 0,00 | 20 / 20 / 21 | 285/288 |
| alimitoitettu pumppu | 7,36 | 7,38 | 7,38 | 464 / 457 / 457 | 1 / 1 / 1 | 0/288 |
| anturikatko 6 h | 7,57 | 7,32 | 7,09 | 0,20 / 0,06 / 0,00 | 17 / 22 / 20 | 243/288 |
| sää- ja hintakatko | 7,37 | 7,00 | 6,85 | 0,26 / 0,15 / 0,01 | 20 / 25 / 26 | 190/288 |
| väärä alkuarvaus (4×τ, 3×nosto, 5 vrk) | 7,83 | 7,29 | 7,18 | 0,16 / 0,12 / 0,00 | 20 / 25 / 24 | 285/288 |
| hintapiikki + lämpöpumppumittari | 9,84 | 9,11 | 8,05 | 0,26 / 0,14 / 0,00 | 20 / 26 / 28 | 287/288 |

**Missä MPC ei auta tai epäonnistuu (raportoitu, ei piilotettu):**
- **Tasainen hinta:** MPC on 1,0 % hintaohjausta kalliimpi. Hintasignaalia ei
  ole, joten mallivirheen marginaali ja tiukempi mukavuusalueen pito maksavat
  energiaa.
- **Alimitoitettu pumppu:** pumppu ei pysty pitämään tavoitetta. Malli ei
  kalibroidu, koska pumppu käy koko ajan eikä tilanvaihtoja synny, ja MPC on
  koko ajan varatilassa `MODEL_LEARNING`. Tulos on sama kuin hintaohjauksella,
  ja kaikki ohjaimet rikkovat mukavuusalueen.
- **Kylmä jakso:** MPC säästää vain 0,4 %, mutta pitää mukavuuden selvästi
  paremmin (0,03 vs. 0,65 K·h). Kun ennuste menee opitun alueen alle, MPC
  siirtyy varatilaan (`OUTDOOR_OUTSIDE_LEARNED_RANGE`, 40 varttia), ja pumpun
  rajalla 15 suunnitelmaa ei löydy (`INFEASIBLE` → varatila).
- **Nopea lämmitys:** jakson alussa MPC ei suunnittele mukavuusalueen
  alapuolelta (`BELOW_COMFORT_FLOOR`, 27 varttia), vaan hintaohjaus lämmittää.
- **Kompressorikäynnistyksiä** on hintapiikki- ja hintamuotoskenaarioissa
  MPC:llä 0–4 enemmän kuin hintaohjauksella.
- Ratkaisijavirheitä (ei `UNSAFE_START`): 0–15 per skenaario, kaikki
  hallittuja varatiloja eikä yksikään osittainen suunnitelma.

Nämä ovat yhden synteettisen rakennuksen simulaatioita, eivät
kenttäsäästöjä.

## 9. Yhteensopivuus PR-pinon #57→#62 kanssa

Haara on leikattu `origin/main`ista (51d242f, sisältää #55 ja #56). Se
yhdistettiin kertakäyttöisessä worktreessä #62:n kärkeen (b91aacb).
Tekstuaaliset ristiriidat syntyivät vain `CMakeLists.txt`:n loppuun ja
`/api/plan`-reitin perään lisättyihin riveihin, ja molemmat pidetään.
Yhdistetty puu: CMake 46/46, pytest samat 6 perustason virhettä, node 9/9
ja `pio run` onnistui. Yhdistämisen jälkeiset pienet jatkotyöt:

1. #58: `shelly_meter_pump_only()` → `site_settings_snapshot().shelly_scope==ShellyScope::HeatPump`
   (yhden rivin muutos, kun molemmat ovat mainissa).
2. #57: profiilin `comfort_min_c` / `comfort_max_c` -rajojen vienti MPC:n `ComfortBand`iin.
3. #59: `/api/timeline` näyttää nyt `bootstrap_preview()`-päätöksen; sen pitää näyttää arbitroitu ehdotus.
4. #60: historiaan voi tallentaa vartin ohjauslähteen (MPC / hinta / termostaatti).

## 10. Testikomennot

```
cmake -S . -B build-debug -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug && cmake --build build-debug -j 8 && ctest --test-dir build-debug
cmake -S . -B build-release -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release && cmake --build build-release -j 8 && ctest --test-dir build-release
python -m pytest -q tests --ignore=tests/test_ota_signing.py --ignore=tests/test_alpha2_release_workflow.py
for f in tests/*.js; do node "$f"; done
cd firmware/cottage && pio run -e esp32s3_cottage_shadow
build-release/cottage_closed_loop_cli docs/benchmarks/mpc-closed-loop-20261120.json docs/benchmarks/mpc-closed-loop-20261120.md
build-release/cottage_mpc_resource_bench docs/benchmarks/mpc-host-resources-20261120.json
```

## 11. Seuraava hyväksymisportti

1. Varjotila oikeassa kohteessa (ei komentoja): kalibroinnin eteneminen,
   ennusteiden RMS ja fallback-syyt `/api/mpc`:stä vähintään 2 viikon ajalta.
2. ESP32-S3-mittaus testikortilla vain omistajan luvalla (`esp32s3_cottage_bench`):
   ajoaika, PSRAM, `loop_stack_free`, `optimizer_stack_free`.
3. Laitekohtainen, erikseen hyväksytty koesuunnitelma ennen kuin MPC-päätös
   saa ohjata aktivoitua pumppua.

---

## English summary

**Evidence level: software complete, host-tested and simulated. Nothing has
been run on a real heat pump and no board was flashed.**

- **Identification** (`cottage/building_identification.*`): a 6-parameter
  RLS model of the same heat balance as `advance()`. It identifies the time
  constant, the heating lift, the start delay, the after-heat and a constant
  gain, each with its uncertainty. The calibration state is honest and is
  backed by walk-forward 4 h validation windows. **COP and absolute C/UA are
  not identifiable** from these measurements. With an owner-confirmed
  heat-pump-only meter it gives only lower bounds (COP ≥ 1). A whole-house or
  unconfirmed meter is never used. On `main` the meter is always treated as
  unconfirmed (the setting arrives with #58).
- **Forecast** (`join_forecast_horizon`): the longest verified contiguous
  price and MET Norway run, 8–48 h. Stale data is unusable; gaps end the
  horizon; DST and negative prices are handled through the same price rule as
  the live controller.
- **Receding horizon**: the plan is recomputed every quarter-hour and only the
  first decision is used, and only inside its own slot with unchanged inputs.
  The planning dwell is 30 min, the same as the control gate. Two backward-
  compatible MPC parameters were added: the after-heat term, and a terminal
  reference that avoids the "end ≥ now" ratchet.
- **Control chain**: a calibrated, current MPC plan, then price control, then
  the thermostat. Every existing gate is unchanged: arming, adapter
  capability (Midea and read-only adapters stay read-only), freshness, the
  30-min dwell and readback. An MPC decision is reported executed only after
  the pump readback shows the planned state.
- **Resources** (host only): 48 h plan in 2.0 ms worst case with a 448 KB
  heap peak (budgets: 1.8 s and 3 MiB). Firmware is +99 KB flash and +528 B
  static RAM compared with main. On-device timing and memory are not
  measured.
- **Closed loop (simulation)**: at equal final energy the MPC is cheaper than
  price control on price spikes (−11.7 %), a cheap night followed by an
  expensive morning (−4.5 %) and negative prices (−7.7 %). It is 1.0 % worse
  with flat prices. It never calibrates with an undersized pump, and it falls
  back under outages and outside the learned outdoor range. Its comfort
  violations are lower than or equal to the baselines in every scenario.
