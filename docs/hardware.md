# Laitevalinnan tarkistus

V1:n kehityskortiksi valittiin **Waveshare ESP32-S3-ETH**, ei yleinen nimeämätön N16R8-kortti. [Valmistajan tuotetiedot](https://docs.waveshare.com/ESP32-S3-ETH) ilmoittavat ESP32-S3R8:n, 16 Mt Flashin, 8 Mt PSRAMin ja SPI-väyläisen W5500:n. [Valmistajan ESP-IDF-ohje](https://docs.waveshare.com/ESP32-S3-ETH/Development-Environment-Setup-ESP-IDF) antaa W5500-kytkennän:

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
