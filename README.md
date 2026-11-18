# Enevina Lite

**A local-first, source-available heat pump controller by Enevina Oy.**

Enevina Lite is an ESP32-S3-based controller for **one heat pump per device**, designed for local operation without a mandatory cloud service or Home Assistant. Its goal is to reduce heating costs using electricity prices, weather forecasts and deterministic, physics-based optimization (MPC). **Lite does not contain AI/ML.** Manufacturer/model support depends on separately developed and verified adapters.

**Development status:** This is an evolving prototype, not a general claim of compatibility, certified safety or production readiness. Check the model-specific documentation and tests before connecting to a real heat pump. Enevina Core and Adaptive Intelligence are separate products.

## License and commercial use

- **License:** [PolyForm Noncommercial 1.0.0](LICENSE) — publicly readable source code for permitted non-commercial uses. This is **source-available**, **not OSI-approved open source**.
- **Commercial use, resale or commercial distribution** of the software or products built around it requires a separate written license from **Enevina Oy**.
- Enevina names, product branding and logos are not granted for third-party commercial branding under the software license.
- Third-party components remain subject to their own licenses. Publication of this repository is not a license grant for any separately owned third-party code.

For commercial licensing requests, contact Enevina Oy.

## Tekninen kehitysdokumentaatio

Alla oleva tekninen sisältö kuvaa kehityksen eri vaiheita ja aikaisempia prototyyppejä. Erityisesti vanhat *shadow/read-only*-tilaa tai fyysisten kokeiden puuttumista kuvaavat merkinnät ovat aikansa tilannekuvia, eivät koko projektin tämänhetkisen valmistumisasteen vahvistus. Tarkista aina uusin koodi, testit ja mallikohtaiset raportit.

## Nykyinen toteutus

- `physics/`: rakennuksen ja käyttövesivaraajan energiatase, parametrinen COP, ulkolämpötilasta riippuva pumpun enimmäisteho ja rakennuksen deterministinen kalibrointi.
- `optimizer/`: 15 minuutin, enintään 192 jakson yhteisoptimointi. Rajattu eteenpäinhaku simuloi löydetyn aikataulun uudelleen ennen hyväksyntää.
- `forecast/`: kokonaishinnan verokäsittely sekä UTC-jaksojen jatkuvuuden ja tuoreuden tarkistus. Laiteohjelmisto hakee Suomen 15 minuutin spot-hinnat Nord Poolin Data Portalista ja tuntikohtaisen ulkolämpötilaennusteen FMI:stä sekä tallentaa vahvistetut sarjat paikallisesti. [Hintalähde](docs/price-source.md) ja [sääennuste](docs/weather-source.md).
- `safety/`: erillinen kirjoitusportin päätöslogiikka. Todellisia pumpun kirjoituksia ei ole toteutettu.
- `firmware/`: Waveshare ESP32-S3-ETH:n W5500-kytkentään valmisteltu Ethernet, HeishaMonin HTTP JSON -lukeminen, Nord Pool -hintojen ja FMI:n tuntisään luku sekä suomenkielinen paikallinen tilasivu. Käynnistyy aina shadow-tilaan, eikä sisällä kirjoitusrajapintaa. Nykyisen fyysisen kortin Ethernet-pinout on vielä tuntematon.
- `firmware/bench/`: sarjaporttiin tulostava, pumppuun ja verkkoon kytkeytymätön ESP32-S3-mittausohjelma. [Mittaustulokset ja G1:n puutteet](docs/G1-report.md).
- `tools/reference_benchmark.py`: riippumaton PC-MILP, sääkompensoitu perusohjaus ja mitattu vertailu. [Tulokset ja rajaukset](docs/G0-report.md).

### PC-testit

```text
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
python -m unittest discover -s tests -p test_reference.py -v
python tools/reference_benchmark.py build/enevina_cli --output docs/benchmarks/g0-local.json
```

Windowsissa viimeisen komennon ohjelmapolku on `build/enevina_cli.exe`. MILP-vertailu tarvitsee SciPy 1.9+ ja NumPy. Raportin taulukko on ajettu Windowsissa Python 3.14 / SciPy 1.17.1 -ympäristössä.

### Varjotilan firmware

Asenna ESP-IDF 5.2.7, avaa sen ympäristö ja suorita `idf.py set-target esp32s3` sekä `idf.py build` hakemistossa `firmware/`. HeishaMonin osoite asetetaan ESP-IDF-valikossa `Enevina Lite shadow → HeishaMon base URL`; tyhjä arvo estää kyselyt. Osoite on paikallinen `http://`-osoite. FMI:n sijainti asetetaan samassa valikossa muodossa `leveysaste,pituusaste`; tyhjä arvo estää säähaun. Ohjelma hakee spot-hinnat ja sääennusteen ilman API-tunnuksia, mutta sopimuskohtaiset verot/kulut ja sääsarjan kytkentä optimointiin puuttuvat vielä laiteohjelmistosta. Paikallista sivua ei ole tarkoitettu Internetiin avattavaksi. **Älä flashaa tätä W5500-versiota tunnistamattomalle kortille tai kytke sitä todelliseen pumppuun tässä kehitysvaiheessa.**

Valittu kortti, pinout ja lähteet on kuvattu [laitemuistiossa](docs/hardware.md). Käännös ei todista Ethernetin, HeishaMonin, watchdogin tai käyttöliittymän toimintaa fyysisellä laitteella.

## V1:n rajaus

- **Yksi lämpöpumppu per laite**.
- Ensisijainen laitealusta: **ESP32-S3 N16R8**, pihamökin pilotissa **2,4 GHz Wi-Fi**, muissa sovittimissa tarvittaessa Ethernet.
- C++ / ESP-IDF; mahdollisimman paljon optimointilogiikasta alustariippumatonta C++-koodia.
- 15 minuutin sähkönhintajakso; ennakointihorisontin tavoite 24–48 tuntia.
- Fysiikkapohjainen, deterministinen MPC-optimointi, **ei AI/ML**.
- Tilalämmityksen ja käyttöveden yhteinen optimointi, laiterajapinnan ja pumpun toimintarajojen sallimissa puitteissa.
- Paikallinen selainkäyttöliittymä, konfiguroitavat lämpötilarajat, lokit ja OTA-päivitykset.
- Paikallinen toiminta väliaikaisissa verkkokatkoissa; ennusteiden ja hintojen välimuisti.
- Ensimmäinen todellinen kohde on Mitsubishi Electric FD25VA -ilmalämpöpumppu. Yhteinen ydin tukee mallikohtaista laajentamista esimerkiksi Panasonic/HeishaMoniin, NIBE/Modbusiin ja muihin dokumentoituihin paikallisiin rajapintoihin. Niiden yhteensopivuutta ei vielä ole varmennettu; langaton toiminta edellyttää kyseisen pumpun tai yhdyskäytävän soveltuvaa yhteyttä.

## Suunniteltu ohjelmistorakenne

| Osa | Tehtävä |
| --- | --- |
| **PumpAdapter** | Valmistaja- ja mallikohtainen protokolla, mittaukset lähde- ja vastaanottoaikoineen, kyvykkyydet ja rajat; tulevat komennot kulkevat Supervisorin kautta vain erikseen hyväksyttynä |
| **Forecast** | 15 min hintasarjat, sääennuste, puuttuvien tietojen käsittely |
| **Physics** | Rakennuksen kevyt lämpömalli, lämpöpumpun COP- ja tehorajat, varaajan malli |
| **Optimizer** | Rajausten mukainen ennakoiva ohjausaikataulu; ensin rajattu haku / dynaaminen optimointi |
| **Supervisor** | Turvarajat, tietojen tuoreus, fail-safe, watchdog ja päivitysten valvonta |
| **Web UI** | Valmistajariippumaton paikallinen UI: merkki/malli, todelliset kyvykkyydet, mittausten laatu, hinnat, ennuste ja optimoinnin selitys |

## Optimoinnin periaatteet

1. Minimoi ennustettu sähkön **kokonaiskustannus**, ei pelkästään sähkön spot-hintaa. Huomioi saatavilla olevat energian hinnan osat ja lämpöpumpun COP.
2. Säilytä sisälämpötilan sekä käyttöveden käyttäjä- ja laitekohtaiset rajat. Hygienia- ja laitesuojatoimintoja ei ohiteta.
3. Ohjaa pumpun sallittuja asetusarvoja; älä katkaise syöttösähköä sähkönhintapiikkien perusteella.
4. Ota käyttöön vain ensimmäinen ohjausjakso ja optimoi uudelleen mittausten päivittyessä.
5. Vanhentuneet tai virheelliset tiedot, yhteyskatkot ja optimoinnin aikakatkaisut johtavat hallittuun varatilaan. Varmista ennen tuotantokäyttöä, miten eri pumppujen asetusarvot palautetaan.

## Ennen alustan lukitsemista

- Toteuta siirrettävä C++-referenssiversio. Mitsubishi-pilotissa vertaa HEAT/OFF-ohjausta tavalliseen ylläpitolämmitykseen; vesikiertoisissa vertaa sääkäyrään ja MILP-referenssiin.
- Mittaa ESP32-S3-laitteella **192 jakson / 48 tunnin** optimoinnin muistinkäyttö, suoritusaika ja vaikutus rinnakkaiseen ohjaukseen.
- Alle 2 sekunnin tavoite täyttyi kolmessa 192 jakson synteettisessä ESP-sarjaporttitapauksessa; samanaikainen Ethernet ja käyttöliittymä ovat vielä testaamatta.
- Testaa käynnistys, verkkokatkot, epäkelvot anturitiedot, laiteyhteyden viiveet, sähkökatkon jälkeinen palautuminen ja watchdog.
- Älä salli tuotantolämpöpumpun ohjauksia ennen erillistä turvallisuus- ja laitekohtaista hyväksyntää.

## Kehitystilan periaate

Tämä repositorio on vain **Enevina Lite** -tuotetta varten. Enevina Core ja muut tuotantojärjestelmät pysyvät erillään. Ensimmäinen kehitysvaihe on simulaatio ja **shadow mode**, ilman kirjoituksia oikeaan lämpöpumppuun.

## Mitsubishi FD25VA: C0 prototype

The Mitsubishi cottage pilot uses a **separate** C++17 `cottage/` module.
The only *simulated* actions are HEAT 16 °C and OFF. No hot water, supply-water control,
thermistor emulation, device-writer or relay switching is implemented.
The bounded optimizer, supervisor and typed read-only observers have repeatable PC tests;
see [C0 cottage report](docs/C0-cottage-report.md) and `tools/cottage_reference.py`.
**This is not a live CN105 integration, ESP32 performance result, or freeze-protection guarantee.**


## Mitsubishi C2 Wi-Fi shadow (experimental)

`firmware/cottage/` now includes an **independent Wi-Fi ESP32-S3 PlatformIO
shadow firmware** based on PR #1's price/FMI readers and PR #4's Cottage MPC.
It has CN105 /events SSE GET and Shelly read-only RPC GET, Ruuvi RAWv5 BLE,
local Finnish dashboard, NVS forecast caches, 15-minute trajectory and supervisor.
[Development, test status and blockers](docs/C2-cottage-report.md).

**No actuator write path is present.** Do not flash the working CN105 bridge.
The optional `cottage_local_config.hpp` is not committed; see
`firmware/cottage/include/cottage_local_config.example.hpp`.
The **default** synthetic-physics flag is 0, which *disables computed price plans*
until the installation has a validated thermal model and independent temperature
sensor. Enabling the test model on a separate bench must not be mistaken for
calibrated safety or economic results.

## Mitsubishi C2.3: RuuviTag ja pihamökin kuivanapidon varjotila

[**C2.3 käyttö-, turvallisuus- ja testiraportti**](docs/C2_3_RUUVI_HUMIDITY_2026-11-16.md).

C2.3 lisää erillisen RuuviTag RAWv2 (Data Format 5) BLE-anturisidonnan,
lämpö-/RH-/paine-/paristo-/RSSI-mittaukset, kastepisteen ja absoluuttisen
kosteuden, viikon tuntihistorian sekä Kylmillään-, Kuivanapito- ja
Ylläpitolämpö-laskentatilat. Valinta on **vain simulointiasetus**. +12 °C:n
suunnittelutavoitetta ei lähetetä Mitsubishiin, jonka normaali alin
lämmitysasetus on +16 °C. Ei CN105-kirjoituksia eikä Shelly-releen
kirjoituksia.

Erillisen ESP32-S3:n firmware: \`firmware/cottage\`. Paikallisen
\`cottage_local_config.hpp\`-tiedoston MAC- ja Wi-Fi-tiedot eivät kuulu
GitHubiin. Sensori voidaan sitoa paikallisesta sivusta vasta, kun anturin
tunniste on fyysisesti tarkistettu. BLE-keruu ja kuivanapidon
laskenta toimivat myös ilman Wi-Fiä, mutta ilman UTC-aikaa
historia on vain käynnistyshetkestä laskettua RAM-historiaa.

**24 h C2.2-laitevakauskoe jätettiin koskematta**; C2.3 ristiinkäännettiin,
mutta sitä ei flashattu kortille tämän kokeen aikana.

## Käyttöönotto Wi-Fi ensin ja optimoinnin täystodennus (18.11.2026)

- [Käyttöönotto](docs/ONBOARDING_WIFI_FIRST_20261118.md): vaihe 1 kysyy vain kodin
  Wi-Fin avoimessa, vain tarvittaessa päällä olevassa asennusverkossa, vaihe 2 (sopimus, siirto, verot,
  CN105/Shelly, sääpaikka, lämpöpumppusovitin) tehdään selaimessa kotiverkossa.
  HOST-TESTATTU, ei fyysisesti todennettu.
- [Optimoinnin audit](docs/OPTIMIZATION_AUDIT_20261118.md): termostaatti, nykyinen
  hintaraja, MPC ja riippumaton MILP samoilla ehdoilla, 19 tapausta × 4 horisonttia.
  SIMULOITU. Uudelleenajo:
  `python3 tools/cottage_optimization_audit.py build-release/cottage_audit_cli`.
