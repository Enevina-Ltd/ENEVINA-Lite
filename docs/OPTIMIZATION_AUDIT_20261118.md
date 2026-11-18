# Enevina Lite – optimoinnin riippumaton täystodennus (18.11.2026)

**Näyttötaso: SIMULOITU + HOST-TESTATTU (x86).** Kaikki tapaukset ovat
synteettisiä. Parametrit eivät ole kalibroitu rakennus eivätkä Mitsubishin
spesifikaatio. Euroja ei saa esittää todennettuna säästönä.
Raakadata: [`benchmarks/cottage-optimization-audit.json`](benchmarks/cottage-optimization-audit.json),
taulukko [`benchmarks/cottage-optimization-audit.md`](benchmarks/cottage-optimization-audit.md),
hydroninen G0 [`benchmarks/esp32-optimization-audit.json`](benchmarks/esp32-optimization-audit.json).

## 1. Mitä laitteessa oikeasti ajetaan

| Polku | Rooli nyt | Huomio |
|---|---|---|
| `cottage/price_bootstrap.hpp` (PR #22) | **Ainoa polku, joka voi lähettää CN105-komennon** (armed-pilotti) | Malliton hintaheuristiikka, ei MPC. |
| `cottage/mpc.cpp` | Varjotila / dashboard | Ei käytössä ilman `COTTAGE_SYNTHETIC_TEST_MODEL=1`; rakennusmallia ei ole kalibroitu. |
| `optimizer/optimizer.cpp` (G0) | Hydroninen tutkimusydin | Ei mökin firmwaressa. |

Asuttu-tila (16–26 °C asetusarvo) toimii vain hintaheuristiikalla: HEAT16/OFF-
fysiikka on validoitu ≤ 16 °C:lle. Pumpun oman termostaatin mallia ei keksitty.

## 2. Menetelmä

`tools/cottage_optimization_audit.py` ajaa jokaisen tapauksen 24/48/96/192 jaksolla:

1. **Perusohjaus**: hystereesitermostaatti 10 ± 0,25 °C.
2. **Hintaraja**: olemassa oleva `bootstrap_price_decision()` sellaisenaan.
3. **Enevina MPC**: `cottage/mpc.cpp`.
4. **MILP**: riippumaton SciPy/HiGHS (`tools/cottage_reference.py`), 30 s aikaraja.

Kaikille sama fysiikka (`advance()`), samat kokonaishinnat (laskettu kerran
C++:ssa firmwaren omalla `finnish_day_transfer` + `calculate_total_price`
-säännöllä, esimerkkitariffi), sama alkutila, samat 60 min minimikäynti/-seisonta-
ajat ja sama 8–14,5 °C mukavuusalue. Jokainen aikataulu toistetaan riippumattomasti
Pythonissa; C++:n omaa kustannusta ei käytetä.

**Loppuehto (korjattu Codexin P1-havainnon jälkeen):** MPC ja MILP päättyvät
≥ alkulämpötilan. Perusohjaukset simuloidaan uudelleen lyhimmällä loppuikkunalla
(8…192 jaksoa), jossa OFF sallitaan vain, jos samalla fysiikalla horisontin
loppuun asti OFF-tilassa jäähtyminen vielä täyttää loppuehdon. Ehto täytetään siis
fyysisesti samoilla viipymä- ja tehorajoilla, ei laskennallisella
lisälämmityksellä. Jos loppuehto ei täyty tai 8 °C:n raja rikkoutuu, säästöä
**ei raportoida** (`NOT_COMPARABLE`). Kaikki prosentit lasketaan toistetuista
todellisista kustannuksista; `equalized_cost_eur` on vain tieto. MILP:n aikarajalla löytämää ratkaisua ei kutsuta optimiksi.
MPC:n ja MILP:n yhteinen tavoite on € + 0,03 €/kytkentä; siksi raportoidaan
sekä €-ero että tavoite-ero.

## 3. Tulokset (mökkimalli, 76 ajoa)

- **Rajarikkeet:** MPC 0 kaikissa täydellisen ennusteen tapauksissa. Perusohjaus
  ja hintaraja rikkovat 8 °C:n rajan nopeasti jäähtyvässä rakennuksessa ja
  nopeassa pakkasen tulossa (6 ajoa / 76, `NOT_COMPARABLE`).
- **Ero MILP:iin** (todistettu optimi, mip_gap ≤ 0,1 %): suurin €-ero **+2,84 %**
  (hidas rakennus, 192), suurin tavoite-ero +1,81 %. Kaikki ≤ 5 % tavoitteen sisällä.
  Kun MILP osui aikarajaan (10 ajoa), MPC:n ero oli −4,8 %…+1,2 %.
- **Säästö vs. perusohjaus** (67 vertailukelpoista ajoa): tyypillisesti 10–30 %,
  hintapiikeissä ~70 %, tasaisella hinnalla 2–6 %.
- **MPC häviää** perusohjaukselle täydellisellä ennusteella: `hard_frost` 24 ja
  `poor_cold_cop` 24 (−0,6 % kumpikin). Molemmissa MPC päättyy ~0,2 °C lämpimämpänä
  kuin vaadittu (24 jakson DP-ruudukon tarkkuus) ja tekee yhden käynnistyksen;
  MILP:n optimi on sama kuin MPC:n, joten kyse on 24 jakson ongelmasta, ei hakuvirheestä.
- **Ei toteuttamiskelpoista:** `fast_outdoor_drop` 192 – pumppu ei pysty pitämään
  8 °C:tta. MPC ja MILP palauttavat INFEASIBLE, ei suunnitelmaa. Perusohjaukset
  rikkovat rajaa ~100 jaksoa ilmoittamatta.
- **Väärä sääennuste** (+4 °C liian lämmin, suljettu silmukka, uudelleensuunnittelu
  joka jakso): katso kohta 5.
- **Laskenta-aika:** MPC x86:lla 192 jaksoa ≤ 16 ms.
  ESP32-S3:n aiempi fyysinen mittaus (`benchmarks/c2-current-mpc-fp32-esp32s3-bench.log`,
  ei tässä työssä): 192 jaksoa pahimmillaan 527 ms ilman samanaikaista kuormaa.

### Korjattu virhe 1 – hintaraja esilämmitti tasaisella hinnalla

`bootstrap_price_decision` piti jaksoa halpana, kun hinta ≤ 2 h mediaani. Tasaisella
hinnalla *jokainen* jakso oli "halpa", ja hintaraja hävisi termostaatille
(`flat_price` 96: 3,017 € vs 2,951 €). Korjaus: halpa/kallis vain, jos ero mediaaniin on
≥ max(10 % · |mediaani|, 0,5 snt/kWh) (asetettavissa). Neutraalissa ikkunassa
säilytetään nykyinen tila (termostaattikäytös). Tulos: 2,925 € < 2,951 €.
Regressiot: `tests/cottage_price_bootstrap_tests.cpp`.

### Korjattu virhe 1b – hintaraja vaihtoi tilaa anturikohinasta (#28, Codex P1)

Kuolleella alueella hintasignaali päätti joka kierroksella riippumatta siitä,
kummalla puolella tavoitetta huone oli: 11,49 → 11,51 → 11,49 °C tuotti
HEAT → OFF → HEAT. Nyt halpa jakso esilämmittää vain tavoitteen alapuolella ja
kallis sammuttaa vain tavoitteessa tai sen yläpuolella; muuten tila säilyy
(`HOLD_IN_DEADBAND`). **Avoin (omistajan päätös):** vuorottelevilla halpa/kallis-
jaksoilla aivan tavoitteessa tila voi edelleen vaihtua 15 min välein, koska
komentopolusta on poistettu vähimmäiskäynti-/seisonta-aika.

**Jäljellä:** hintaraja häviää termostaatille edelleen 20/70 vertailukelpoisessa ajossa (enimmäkseen
24 jakson horisontit ja leuto sää), koska se nostaa asetusarvoa halvalla
jaksolla ilman fysiikkamallia. MPC voittaa molemmat. Tämä on syy kalibroida
rakennusmalli ja siirtää ohjaus MPC:lle – ei vielä tehty.

### Korjattu virhe 2 – G0:n varastoidun lämmön arvo

`optimizer/optimizer.cpp` yhdisti samaan lokeroon osuvat tilat kiinteällä
0,05 €/kWh lämpöarvolla. Korkeilla hinnoilla / heikolla COP:lla se hävitti
lämpimämmät tilat. Uusi arvo = jäljellä olevan horisontin keskimääräinen
hinta/COP. Sama yksi ajo, O(n) esilaskenta, sama replay-tarkistus.

| G0 (hydroninen) | ennen | jälkeen |
|---|---:|---:|
| radiator 48 / 96 / 192 | 4,78 / 4,81 / 2,97 % | 2,42 / 1,25 / 0,66 % |
| mild 48, cold 48 / 96 | 0,42; 0,02 / 0,74 % | 1,10; 0,58 / 1,02 % |
| suurin ero MILP:iin | **4,81 %** | **2,42 %** |
| ero-summa 12 tapausta | 16,6 %-yks. | 9,8 %-yks. |

Kahden vaihtoehdon portfolio olisi poistanut pienet heikennykset, mutta G0:n
192 jakson ESP32-S3-aika on jo ~1,5 s (`G1-report.md`), joten 2× laskenta ei mahdu
2 s budjettiin. Regressio: `tests/test_g0_stored_value_regression.py`.

## 4. Kokonaishinnan oikeellisuus

- Yksi sääntö `site_total_price()` (`cottage/site_settings.hpp`) MPC-sillalle,
  hintarajalle, `/api/status`:lle ja `/api/prices`:lle. Aiemmat kolme
  `localtime_r`-kopiota poistettu.
- Sääpaikka hyväksytään vain sääennusteen lukijan rajoissa (lev 59–71, pit 19–32).
- Spot ALV 0 % + ALV kerran; marginaali/siirto/vero voidaan antaa ALV:llisina
  (kuluttajahinnastot) → ALV:ia ei lisätä niihin toiseen kertaan.
- Yleis-, aika- ja kausisiirto; päivä/yö-tunnit asetettavissa; arvioidaan
  jakson alussa Suomen ajassa, kesä-/talviaikamuutos testattu (25.10. ja 29.3.).
- Kuukausimaksuja ei lasketa, koska ne eivät muuta 15 min päätöstä.

## 5. Väärän sääennusteen riski

Suljettu silmukka: MPC suunnittelee joka jakso uudelleen ennusteella, joka on
4 °C liian lämmin, ja toteutus tapahtuu oikealla säällä.

| Jaksot | Ilman marginaalia: min °C / rikkeet / loppuehto | 1 °C marginaali: min °C / rikkeet / loppuehto | Marginaalin säästö vs termostaatti |
|---:|---|---|---:|
| 48 | 8,04 / 0 / ei täyty | 8,98 / 0 / täyttyy | 12,7 % |
| 96 | 8,06 / 0 / ei täyty | 8,97 / 0 / täyttyy | 5,5 % |
| 192 | **7,93 / 2** / ei täyty | 8,95 / 0 / täyttyy | 5,3 % |

- `planning_uncertainty_c = 0` ei riitä, jos sääennuste voi olla järjestelmällisesti
  väärä: 192 jakson ajossa 8 °C alittui eikä loppuehto täyttynyt (ei vertailukelpoinen).
  1 °C marginaali poisti rikkeet ja täytti loppuehdon, edelleen termostaattia halvempi.
- **Havaittu puute (ei korjattu):** kun huone on suunnittelurajan alapuolella,
  `optimize()` palauttaa `UNSAFE_START` eikä suunnittele palautusta. Silloin
  käytetään varatilaa (pumpun oma HEAT 16 °C). Turvallinen, mutta ei optimaalinen.
  Ehdotus: palautustila, joka sallii alkutilan rajan alla ja vaatii nousun.
- 24 jaksoa: MPC häviää (−6,4 % / −30,1 %), koska lyhyt horisontti + väärä
  ennuste johtaa varmuuslämmitykseen. Ei piiloteta.
- Suositus ennen MPC-ohjausta: aseta `planning_uncertainty_c` ennustevirheen
  mukaan ja mittaa FMI-ennusteen todellinen virhe kohteessa.

## 6. Vikatilanteet (`tests/cottage_optimization_audit_tests.cpp`)

| Tilanne | Tulos |
|---|---|
| Puuttuva hintajakso | `BridgeStatus::Gap`, ei suunnitelmaa, ei interpoloitua hintaa |
| Vanhentunut sää > 6 h / hinnat > 36 h | `StaleWeather` / `StalePrices` |
| Anturikatkos | hintaraja `UNAVAILABLE`, ohjausportti ei ehdota komentoa |
| Pumpun yhteyskatkos | `INPUTS_UNVERIFIED`, ei komentoa |
| Aikakatkaisu / peruutus / muistibudjetti | `Timeout` / `Cancelled` / `ResourceLimit`, `count=0`, ei osittaista suunnitelmaa |
| Ei toteuttamiskelpoista suunnitelmaa | `UnsafeStart` / `Infeasible`, ei suunnitelmaa |
| Horisontti 24/48/96/192; 193 | OK ja replay = raportoitu €; 193 → `InvalidInput` |
| Kesä-/talviaika | 192 peräkkäistä UTC-varttia, siirtohinta vaihtuu paikallisaikaan |

Menoveden COP-haitta ei koske ilma–ilmapumppua (`NOT_APPLICABLE`); se on G0:n
hydronisessa mallissa ja -vertailussa.

## 7. Mitä EI ole todennettu

ESP32-S3:lla ei ajettu mitään tämän työn aikana (ei lupaa flashata). Ei 24–48 h
kuormakoetta, ei Wi-Fi+BLE+HTTPS+MPC-yhteiskäytön mittausta, ei oikean rakennuksen
kalibrointia, ei toteutuneita euroja.
