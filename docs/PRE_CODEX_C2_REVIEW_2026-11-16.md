# Enevina Lite — riippumaton pre-Codex-katselmointi, 16.11.2026

**Kohde:** PR #1 (shared core) -> PR #4 (Mitsubishi Cottage MPC) -> PR #5 (ESP32 Wi-Fi C2 shadow).
**Tila:** Lähdekoodin yhdistäminen, katselmointikorjaukset ja paikalliset testit tehty. **Ei C2-laitehyväksyntää eikä C3-ohjauslupaa.**
**CI:** GitHub Actions ei ollut käytettävissä; mitään CI-PASS-tulosta ei väitetä.

## Alkuperäinen tilanne

- PR #1: `3305627` (Core/forecast-korjaukset), PR #4: `d77a60d` (Mitsubishi MPC -korjaukset).
- PR #5: `4f6a398`, perustui PR #4:n vanhempaan `159685b`-versioon.
- PR #5:n avoin P2-katselmointihavainto: optimointi asetti jokaisen uudelleenlaskennan yhteydessä pumpun nykyisen tilan keston nollaksi.
- Tämän version viimeisten ESP32-laitemittausten 630–799 ms luvut kuuluvat **aiempaan erilliseen benchmarkiin**, eivät tämän yhdistetyn koodin täyteen C2-firmwareen.

## Katselmointi ja tehdyt korjaukset

1. **Päivitetty PR #5:n pohja PR #4:n ajantasaiseen versioon** (sisältää PR #1:n korjaukset). Ratkaistu CMakeLists- ja CottageMPC-laskennan merge-konfliktit. Säilytetty molemmat uuden MPC:n parannukset: kaksi termistä edustajaa kutakin tilabiniä varten, rajattu aika/keskeytys, tarkka jälkitarkistus sekä esilasketut tilasta riippumattomat fysiikkakertoimet.
2. **CN105:n havaitun HEAT/OFF-tilan kesto** lasketaan ESP32:n 64-bittisestä monotonisesta kellosta ensimmäisestä hyväksytystä tilatiedosta. Vastaava toistuva mittaus ei nollaa aikaa. Muu tila, vanhentunut lukema, streamin katkeaminen, uudelleenkäynnistys tai epävarma jatkuvuus palauttaa varovaisen keston nollaan. Poistettu kiinteä 30 sekunnin SSE-yhteyden pakotettu sulkeminen; pitkän katkoksen jälkeen yhdistetään uudelleen.
3. **Ohjausmallin toimintatilaturva:** COOL/FAN/DRY/Other ei saa tulla käsitellyksi OFF-tilana CottageMPC:ssä tai Supervisorissa. C2-firmware on edelleen vain lukutilassa.
4. **ESP32:n laskennan aikabudjetti oikeasti käytössä:** CottageMPC saa 1 800 ms aikarajan, 64-bittisen `esp_timer_get_time()`-kellon sekä Wi-Fi-/web-loop-katkosta laukeavan keskeytyssignaalin. Aikakatkaisu/Cancelled ei oikeuta keskeneräisen suunnitelman käyttöön.
5. **Suomen siirtohintojen DST-/aikavyöhykevirheriskin poisto:** Ennusteyhdistin laskee Suomen paikallisen tunnin suoraan UTC:stä ja EU:n maaliskuun/lokakuun siirtymäsäännöistä. Se ei riipu kehityskoneen paikallisesta TZ-asetuksesta tai alustakohtaisesta `localtime_r`-funktiosta.
6. **Regressiot:** monotonic mode age (ml. 180 min jatkuva OFF ja 8,2 °C/-10 °C:n `Infeasible`-toistotapaus), streamin menetyksen aiheuttama nollaus, unsupported-pump-mode, 07/22-siirtohinta sekä kesä-/talviajan vaihdot.

## Paikallinen todennus (CI:n sijasta)

Erillinen Git-klooni Windows-kehityskoneen tilapäishakemistossa; ei tuotantojärjestelmien muutoksia.
- GCC **16.1.0**, CMake **4.4.1**, `-DCMAKE_BUILD_TYPE=Release`: **13/13 CTest PASS**.
- Python **3.15.0**, `unittest discover -s tests`: **5/5 PASS**.
- PlatformIO `espressif32` **7.0.1**, Arduino-ESP32 **2.0.17**, `pio run -e esp32s3_cottage_shadow`: **PASS**, 60 116 tavua staattista RAMia, 982 473 tavua flashia (rakennuskoko ei ole mitattu heap/stack).
- `git diff --check` tarkastetaan ennen julkaisemista.
- Riippumaton SciPy/HiGHS 96 jakson vertailu, 8 s MILP-aikaraja per tapaus:

| Tapaus | CottageMPC € | MILP best found € | MILP status | Simulaatiorikkomuksia |
| --- | ---: | ---: | --- | --- |
| Leuto | 3.050351 | 3.050351 | aikaraja, 2,15 % gap | 0 |
| Kylmä | 6.158475 | 6.038643 | optimaalisuusraja täyttyi (0,095 % gap) | 0 |
| Hintapiikki | 5.090182 | 5.089550 | optimaalisuusraja täyttyi (0,080 % gap) | 0 |
| Sulatus | 3.161869 | 3.161049 | aikaraja, 2,10 % gap | 0 |

Vertailun parametrit ovat **synteettisiä**, eivät rakennuksen todellisia lämpö- tai säästötuloksia. Uuden yhdistelmän 192 jakson fyysistä ajoa ei ole mitattu.

## P0/P1-käyttöönottoesteet, joita tämä katselmointi EI poista

- **C2-kokonaislaiteajo puuttuu:** täyttä Wi-Fi+CN105 SSE+Shelly GET+Ruuvi BLE+Nord Pool+FMI+Web UI+MPC -firmwarea ei ole flashattu erilliselle testikortille tai ajettu samanaikaisesti oikeilla datalähteillä. Vanha toimiva CN105-silta on jätetty koskemattomaksi.
- CN105-sillan HTTP/SSE-lukeman pumpun oman sarjaväyläpäivityksen tuoreutta ei ole vielä vahvistettu riippumattomalla laskurilla. Pelkkä yhteys palvelimeen ei riitä C3-ohjauksen tuoreustodisteeksi.
- Riippumaton rakennuksen lämpötilamittaus ja erillinen pakkassuoja puuttuvat/ovat vahvistamatta. Rakennuksen lämpökapasiteetti, pumpun HEAT 16 °C -termostaattivaste, COP ja sulatukset ovat edelleen kalibroimatta.
- Kustannusoptimointi on oletusarvoisesti estetty synteettisen fysiikan vuoksi. Lämpöpumpun CN105- tai Shelly-kirjoituspolkua ei lisätä tähän PR:ään.
- Pitkäkestoiset virta-/verkko-/watchdog-/muistivuototestit ja uuden yhdistelmäalgoritmin fyysinen 192 jakson aikamittaus puuttuvat.

**Seuraava portti:** arvioi katselmointikoodi ja testitulokset ensin tässä raportissa. Pyydä vasta sitten erillinen Codex-uusintakatselmointi PR #5:een. PR #1:n ja PR #4:n aiemmat tarkistukset jäävät omiin PR-ketjuihinsa. Älä yhdistä vielä tuotantohaaraan.
