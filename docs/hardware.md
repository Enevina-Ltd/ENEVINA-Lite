# Laitevalinnan tarkistus

Core-koneeseen liitetty *fyysinen* laite tunnistettiin ESP32-S3 rev v0.2 -piiriksi, jossa on 16 Mt flash, 8 Mt PSRAM ja CH340-USB-silta. USB-tiedoista tai vanhan ohjelman binääristä ei voitu tunnistaa kortin valmistajaa, Ethernet-ohjainta tai pinoutia. Korttiin asennettiin vain erillinen, sarjaporttia käyttävä laskentatesti. Alla oleva Wavesharen kytkentä on firmware-lähdekoodin nykyinen oletus, **ei tunnistetun laitteen vahvistettu kytkentä**. [Fyysisen piirin mittaus](G1-report.md).

Waveshare ESP32-S3-ETH on dokumentoitu mahdollinen kehityskortti. [Valmistajan tuotetiedot](https://docs.waveshare.com/ESP32-S3-ETH) ilmoittavat ESP32-S3R8:n, 16 Mt Flashin, 8 Mt PSRAMin ja SPI-väyläisen W5500:n. [Valmistajan ESP-IDF-ohje](https://docs.waveshare.com/ESP32-S3-ETH/Development-Environment-Setup-ESP-IDF) antaa W5500-kytkennän:

| W5500 | ESP32-S3 GPIO |
| --- | ---: |
| MISO | 12 |
| MOSI | 11 |
| SCLK | 13 |
| CS | 14 |
| RST | 9 |
| INT | 10 |

ESP-IDF 5.2.7:n `esp_eth`-komponentissa on W5500 SPI Ethernet -ajuri. Tämä tarkistaa lähdekoodin ja dokumentoidun pinoutin yhteensopivuuden, mutta ei todista toimintaa kortilla.

[HeishaMonin virallinen dokumentaatio](https://github.com/heishamon/HeishaMon/blob/main/README.md) kuvaa paikallisen `GET /json?topic=...` -lukurajapinnan; firmware käyttää vain sitä. HeishaMon tukee myös `/command`-kirjoituksia, mutta niitä ei ole otettu mukaan. HeishaMon huomauttaa, että vanhat TOP15/16-teholukemat voivat eräissä lämpöpumpuissa olla virheellisiä; tästä syystä firmware ei käytä niitä energiamittaukseen.

## Ennen G1/G2-hyväksyntää

- Tarkista fyysisen kortin versio ja pinout ennen flashausta.
- Mittaa 96/192 jakson laskenta-aika, huippumuisti ja vapaa sisäinen heap/PSRAM kortilla Ethernetin, HTTP-palvelimen ja HeishaMon-kyselyjen ollessa samanaikaisesti käynnissä.
- Testaa Ethernet-linkki, DHCP, HeishaMonin vastausmuoto, aikakatkaisut, reboot ja pitkäkestoinen vakausajo.
- Varmista erikseen kyseisen Panasonic-mallin ohjauskyvykkyydet ja turvallinen palautus pumpun omaan säätöön. Siihen asti kaikki kirjoitukset estetään.
