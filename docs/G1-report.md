# G1: ESP32-S3:n sarjaporttimittaus (2026-11-15)

**G1 on kesken.** Core-koneeseen kytketty piiri tunnistettiin `esptool`-työkalulla ESP32-S3:ksi, jossa on 16 Mt flash ja 8 Mt sisäistä PSRAMia. USB-silta on CH340. USB-tunniste ei kerro kehityskortin mallia, Ethernet-piiriä tai pinoutia. Vanhan ohjelman tunniste oli `arduino-lib-builder`, ja sen binäärissä oli teksti `Generic S3: W5500 Ethernet disabled`. Niistä ei voi päätellä, että Wavesharen W5500-pinout sopii tähän korttiin.

Varsinainen ESP-IDF 5.2.7 -firmware kääntyi myös Nord Poolin HTTPS-hintahaun, FMI-säähaun sekä paikallisen hinta- ja säänäkymän kanssa. Binäärin koko oli `0xb8b80` tavua ja pienimpään 1 Mt:n sovellusosioon jäi noin 28 % vapaata. Tätä firmwarea ei asennettu tunnistamattomaan korttiin. Hintojen ja sään verkkohaku, Ethernet ja UI eivät siis ole fyysisesti todennettuja.

Käyttäjän sallimalla ESP:llä korvattiin vanha ohjelma erillisellä `firmware/bench/`-mittausohjelmalla. Se käyttää samaa `physics.cpp`- ja `optimizer.cpp`-lähdekoodia kuin PC ja ESP-IDF-laiteohjelmisto. Ohjelma tulostaa vain sarjaporttiin; se ei ota verkkoyhteyttä eikä sisällä lämpöpumppuliitäntää tai kirjoituskomentoja. PlatformIO:n yleiseen ESP32-S3-profiiliin asetettiin havaitun piirin 16 Mt flash ja OPI-PSRAM. Ensimmäinen ajo ilman PSRAM-asetusta käynnistyi uudelleen. Seuraava ajo paljasti Arduino-loopin oletuspinon ylittymisen; mittaus siirrettiin 32 kt:n erilliseen FreeRTOS-tehtävään. Nämä epäonnistumiset korjattiin ennen alla olevia mittauksia.

Eteenpäinhaun lämpöpumpun teho, COP ja jakson sähkökustannus lasketaan nyt kerran jaksoa kohti. Sisäinen haku käyttää 32-bittisiä liukulukuja, mutta rekonstruoitu aikataulu simuloidaan uudelleen alkuperäisellä kaksinkertaisen tarkkuuden fysiikkamallilla. Kaikki 12 G0:n PC-tapausta säilyttivät aiemman kelvollisuuden ja kustannuksen viiden desimaalin tarkkuudella. Alkuperäisen G0:n MILP-aineiston taloudelliset luvut ovat edelleen kyseisen ajon tuloksia; uutta MILP-ajoa ei ole tehty.

## Mitattu ajoaika

Jokainen profiili ja horisontti ajettiin kolme kertaa 240 MHz:n ESP32-S3:lla. Profiilit ja lähtöarvot vastaavat `tools/reference_benchmark.py`-testin lievää, kylmää ja patterilämmitystä. `status=0` tarkoittaa kelvollista ratkaisua. [Sarjaportin raakaloki](benchmarks/g1-esp32s3-serial.log) sisältää yksittäiset ajat, kustannukset ja muistitiedot.

| Tapaus | Jaksoja | Keskiarvo ms | Pahin ms | Ratkaisut |
| --- | ---: | ---: | ---: | ---: |
| Lievä | 96 | 377,3 | 378 | 3/3 |
| Lievä | 192 | 841,0 | 841 | 3/3 |
| Kylmä | 96 | 399,0 | 399 | 3/3 |
| Kylmä | 192 | 1101,0 | 1101 | 3/3 |
| Patteri | 96 | 465,3 | 466 | 3/3 |
| Patteri | 192 | 1494,0 | 1494 | 3/3 |

Hitain mitattu 192 jakson ajo jäi alle kahden sekunnin tavoitteen. Sarjaporttimittauksessa ei ollut samanaikaista Ethernetiä, Web UI:ta, anturilukua tai HeishaMon-kyselyjä. Siksi se **ei vielä todista G1:n kokonaissuorituskykyä**.

Moniprofiiliajon pienin sisäisen vapaan heapin alaraja oli 325 636 tavua ja PSRAMin 6 738 575 tavua. Erillisessä [laajemman huonelämpötila-alueen ajossa](benchmarks/g1-esp32s3-wide-room.log) 192 jaksoa kesti 809 ms ja PSRAMin alaraja oli 5 934 975 tavua. `heap_caps_get_minimum_free_size` on käynnistyksen aikainen alaraja; se ei ole tarkka yksittäisen funktion huippuallokaatio. Mittaustehtävän pienin vapaa pino oli 20 836 tavua. ESP-IDF:n varsinaisen laiteohjelmiston muistia ei mitattu tällä testillä.

## G1:stä puuttuu

- Tunnista kortin fyysinen Ethernet-piiri ja pinout; nykyinen ESP-IDF-firmware on tehty Waveshare ESP32-S3-ETH:n W5500-kytkennälle, jota USB-laite ei todista.
- Aja sama 96/192-jakson kuorma ESP-IDF-laiteohjelmistossa Ethernetin, paikallisen sivun ja laitelukemien kanssa; mittaa tehtävien vasteajat ja vapaa muisti.
- Testaa aikakatkaisut, watchdog, verkkokatko, uudelleenkäynnistys ja pitkäkestoinen vakausajo.
- Älä kytke mitään lämpöpumpun ohjausta ennen G2:n laitekohtaista todentamista ja erillistä hyväksyntää.
