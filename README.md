# Enevina Lite

**Yhden lämpöpumpun paikallinen optimointiohjain.**

Enevina Lite on Enevina Coresta erillinen, kevyt ohjain, jonka tavoitteena on minimoida lämpöpumpun lämmitys- ja käyttövesikustannukset turvallisesti. **Lite ei sisällä tekoälyä**, ei edellytä Home Assistantia eikä tarvitse Linuxia tai pilvipalvelua normaalissa käytössä.

> Tila: PC:llä testattu G0-prototyyppi ja ESP32-S3:lle kääntyvä **vain luku** -laiteohjelmisto. G0–G3-hyväksyntäportit ovat edelleen auki; tuotantolaitetta ei saa ohjata tällä versiolla.

## Nykyinen toteutus

- `physics/`: rakennuksen ja käyttövesivaraajan energiatase, parametrinen COP, ulkolämpötilasta riippuva pumpun enimmäisteho ja rakennuksen deterministinen kalibrointi.
- `optimizer/`: 15 minuutin, enintään 192 jakson yhteisoptimointi. Rajattu dynaaminen ohjelmointi ja tarkkoja tiloja kuljettava varahaku; molemmat simuloivat tuloksen uudelleen ennen hyväksyntää.
- `forecast/`: kokonaishinnan verokäsittely sekä UTC-jaksojen jatkuvuuden ja tuoreuden tarkistus. Verkkohakuja ei ole vielä yhdistetty laiteohjelmistoon.
- `safety/`: erillinen kirjoitusportin päätöslogiikka. Todellisia pumpun kirjoituksia ei ole toteutettu.
- `firmware/`: Waveshare ESP32-S3-ETH:n W5500-Ethernet, HeishaMonin HTTP JSON -lukeminen ja suomenkielinen paikallinen tilasivu. Käynnistyy aina shadow-tilaan, eikä sisällä kirjoitusrajapintaa.
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

Asenna ESP-IDF 5.2.7, avaa sen ympäristö ja suorita `idf.py set-target esp32s3` sekä `idf.py build` hakemistossa `firmware/`. HeishaMonin osoite asetetaan ESP-IDF-valikossa `Enevina Lite shadow → HeishaMon base URL`; tyhjä arvo estää kyselyt. Osoite on paikallinen `http://`-osoite. Ohjelma ei vielä hae spot-hintoja tai sääennustetta, eikä paikallista sivua ole tarkoitettu Internetiin avattavaksi. **Älä flashaa tai kytke sitä todelliseen pumppuun osana tätä kehitysvaihetta.**

Valittu kortti, pinout ja lähteet on kuvattu [laitemuistiossa](docs/hardware.md). Käännös ei todista Ethernetin, HeishaMonin, watchdogin tai käyttöliittymän toimintaa fyysisellä laitteella.

## V1:n rajaus

- **Yksi lämpöpumppu per laite**.
- Ensisijainen laitealusta: **ESP32-S3 N16R8**, Ethernet-yhteydellä (yhteensopiva Ethernet-ohjain vaaditaan).
- C++ / ESP-IDF; mahdollisimman paljon optimointilogiikasta alustariippumatonta C++-koodia.
- 15 minuutin sähkönhintajakso; ennakointihorisontin tavoite 24–48 tuntia.
- Fysiikkapohjainen, deterministinen MPC-optimointi, **ei AI/ML**.
- Tilalämmityksen ja käyttöveden yhteinen optimointi, laiterajapinnan ja pumpun toimintarajojen sallimissa puitteissa.
- Paikallinen selainkäyttöliittymä, konfiguroitavat lämpötilarajat, lokit ja OTA-päivitykset.
- Paikallinen toiminta väliaikaisissa verkkokatkoissa; ennusteiden ja hintojen välimuisti.
- Ensimmäinen sovitinkohde: Panasonic Aquarea / HeishaMon. Muut merkit omilla adaptereillaan myöhemmin.

## Suunniteltu ohjelmistorakenne

| Osa | Tehtävä |
| --- | --- |
| **PumpAdapter** | Mallikohtainen mittausten luku, kyvykkyydet ja turvalliset asetusarvokirjoitukset |
| **Forecast** | 15 min hintasarjat, sääennuste, puuttuvien tietojen käsittely |
| **Physics** | Rakennuksen kevyt lämpömalli, lämpöpumpun COP- ja tehorajat, varaajan malli |
| **Optimizer** | Rajausten mukainen ennakoiva ohjausaikataulu; ensin rajattu haku / dynaaminen optimointi |
| **Supervisor** | Turvarajat, tietojen tuoreus, fail-safe, watchdog ja päivitysten valvonta |
| **Web UI** | Paikalliset asetukset, tilatiedot ja optimoinnin selitys |

## Optimoinnin periaatteet

1. Minimoi ennustettu sähkön **kokonaiskustannus**, ei pelkästään sähkön spot-hintaa. Huomioi saatavilla olevat energian hinnan osat ja lämpöpumpun COP.
2. Säilytä sisälämpötilan sekä käyttöveden käyttäjä- ja laitekohtaiset rajat. Hygienia- ja laitesuojatoimintoja ei ohiteta.
3. Ohjaa pumpun sallittuja asetusarvoja; älä katkaise syöttösähköä sähkönhintapiikkien perusteella.
4. Ota käyttöön vain ensimmäinen ohjausjakso ja optimoi uudelleen mittausten päivittyessä.
5. Vanhentuneet tai virheelliset tiedot, yhteyskatkot ja optimoinnin aikakatkaisut johtavat hallittuun varatilaan. Varmista ennen tuotantokäyttöä, miten eri pumppujen asetusarvot palautetaan.

## Ennen alustan lukitsemista

- Toteuta siirrettävä C++-referenssiversio ja vertaa tuloksia tietokoneella laskettuun MILP-vertailuratkaisuun sekä tavalliseen lämmityskäyrään.
- Mittaa ESP32-S3-laitteella **192 jakson / 48 tunnin** optimoinnin muistinkäyttö, suoritusaika ja vaikutus rinnakkaiseen ohjaukseen.
- Tavoitteena alle 2 sekunnin optimointiaika; tämä **ei ole vielä todennettu tulos**.
- Testaa käynnistys, verkkokatkot, epäkelvot anturitiedot, laiteyhteyden viiveet, sähkökatkon jälkeinen palautuminen ja watchdog.
- Älä salli tuotantolämpöpumpun ohjauksia ennen erillistä turvallisuus- ja laitekohtaista hyväksyntää.

## Kehitystilan periaate

Tämä repositorio on vain **Enevina Lite** -tuotetta varten. Enevina Core ja muut tuotantojärjestelmät pysyvät erillään. Ensimmäinen kehitysvaihe on simulaatio ja **shadow mode**, ilman kirjoituksia oikeaan lämpöpumppuun.
