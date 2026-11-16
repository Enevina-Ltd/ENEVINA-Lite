# Enevina Lite

**Monimerkkinen, yhden lämpöpumpun paikallinen optimointiohjain.**

Enevina Lite on Enevina Coresta erillinen, valmistajariippumattoman ytimen ja mallikohtaisten adapterien varaan rakentuva kevyt ohjain, jonka tavoitteena on minimoida lämpöpumpun lämmityskustannukset ja soveltuvilla laitteilla myös käyttöveden kustannukset turvallisesti. **Lite ei sisällä tekoälyä**, ei edellytä Home Assistantia eikä tarvitse Linuxia tai pilvipalvelua normaalissa käytössä.

> **Ensimmäinen pilotti, ei tuotteen valmistajarajaus (16.11.2026):** pihamökin **Mitsubishi Electric MSZ-FD25VA / MUZ-FD25VA**. ESP32-S3 Wi-Fi + lokakuussa 2026 read-only-tilassa toiminut CN105; uusi Lite-kytkentä ja ohjauskomennot vielä testaamatta. Shelly Plus 1PM vain kulutusmittaukseen. Mökin tavoiteltu ylläpitolämpö noin +5 °C, ennakoiva turvamarginaali +8 °C; pumpun alin lämmityspyynti +16 °C, joten suunniteltu ohjaus on HEAT 16 °C / OFF. **Tämä ilma–ilmapumppu ei käytä menoveden tai käyttöveden optimointia.** Katso [pilottisuunnitelma](docs/cottage-mitsubishi-fd25va.md). Oikeaa laitetta ei vielä ohjata.

> Tila: PC:llä testattu G0-prototyyppi, fyysisellä ESP32-S3:lla mitattu laskentaydin ja ESP32-S3:lle kääntyvä **vain luku** -laiteohjelmisto. G0–G3-hyväksyntäportit ovat edelleen auki; tuotantolaitetta ei saa ohjata tällä versiolla.

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
