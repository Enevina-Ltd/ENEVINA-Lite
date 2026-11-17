# F03 – CN105:n todellisen UART-lähdemittauksen todentaminen (17.11.2026)

## Varmennettu lähtötilanne

- Käytössä olevan CN105-sillan paikallinen HTTP SSE `GET /events` vastasi `200 text/event-stream`; read-only-otos sisälsi esimerkiksi `sensor/CN105 Connection Uptime`, `text_sensor/Functions Readback`, climate-tilan ja kompressoritaajuuden. **Yksikään tarkistettu viesti ei sisältänyt kyseisen pumpun UART-mittauksen alkuperäistä aikaa, onnistuneen 0x02/0x03/0x06-lukusarjan tunnistetta tai näihin arvoihin sidottua laskuria.**
- Sillan gzip-pakatun HTTP-etusivun sovelluskoodissa näkyy ESPHome-WebServerin tunniste. Vastaavat CN105-komponentin luokat löytyvät julkisesta [echavet/MitsubishiCN105ESPHome](https://github.com/echavet/MitsubishiCN105ESPHome) -projektista. Tarkasteltu **julkisen lähteen commit** on [`cdcfc7545df61cab63420eb5a14dbbcb0c084fa4`](https://github.com/echavet/MitsubishiCN105ESPHome/commit/cdcfc7545df61cab63420eb5a14dbbcb0c084fa4) (7.11.2026); **käytössä olevan sillan tarkkaa rakennetta, YAML-konfiguraatiota, ESPHome-versiota tai git-committia ei voida päätellä näistä HTTP-vastauksista**.
- Tutkitussa upstream-lähteessä `components/cn105/hp_readings.cpp` tarkistaa `parser_.checksum_valid()` ennen `processCommand()`-käsittelyä. `components/cn105/cn105.cpp` rekisteröi statuskyselyt: `0x02` asetukset, `0x03` huonelämpötila, `0x06` käyttö-/kompressoritila. `request_scheduler.cpp` voi ohittaa/aikakatkaista pyyntöjä ja `terminateCycle()` kasvattaa `nbCompleteCycles_`-laskuria **ilman vaatimusta kaikkien näiden kolmen onnistuneesta lukemisesta**. Siksi `nbCompleteCycles_`, yhteyden uptime tai SSE-heartbeat **ei ole kelvollinen mittaustodiste**.

## Toteutettu Enevina Lite -vastaanotin ja host-mock

- Uusi `cottage/cn105_source_proof.hpp` käyttää **olemassa olevaa** `PumpReadCounterTracker`-tarkistusta. Koko polku on vain ensimmäisen CN105-pilotin adapterissa; yleisen `PumpAdapter`-sopimusta ei muuteta.
- ESP32:n nykyiseen SSE-lukijaan lisättiin vapaaehtoinen yhden tapahtuman atomi `text_sensor/Enevina CN105 Verified Snapshot`. Tämä on **uuden, erikseen auditoinnin ja testauksen jälkeen asennettavan upstream-version sopimus**, **ei olemassa olevan sillan lähettämä viesti**. Vanhan sillan SSE jää `UNVERIFIED_SOURCE`-tilaan.
- Esimerkki yhden ESPHome-text-sensorin **simuloidusta** arvosta (tekstimuotoinen JSON, ei tapahtumavastaanoton muokkaama):

```json
{
  "schema": 1,
  "boot": 271828,
  "seq": 201,
  "oldest_utc": 1800000000,
  "latest_utc": 1800000004,
  "uart_connected": true,
  "complete_poll": true,
  "indoor_c": 18.5,
  "setpoint_c": 21.0,
  "compressor_hz": 35,
  "mode": "HEAT"
}
```

Tämä arvostring sijoitetaan ESPHome-SSE:n `value`-kenttään, jonka `id` on `text_sensor/Enevina CN105 Verified Snapshot`. `boot` on **pysyvästi talletettu, aidosti monotonisesti kasvava lähdesillan käynnistys-/UART-istuntolaskuri** (32-bit, ei 0), **ei satunnainen boot-ID**. Jos lähdesilta ei pysty takaamaan monotonista ja rebootien yli säilyvää laskuria, mittaus pysyy **UNVERIFIED_SOURCE**-tilassa. Epochin pieneneminen hylätään; uudella jaksolla lähdelukuajan täytyy kasvaa aiemman jakson korkeinta lähdeaikaa suuremmaksi ja `seq` kasvaa **vain** valmistuneesta todellisesta, eheästä kolmesta pumpun vastauksesta. `oldest_utc` ja `latest_utc` mitataan **pumpun UART-vastausten dekoodaushetkellä sillassa**, ei Enevina Liten SSE-vastaanoton hetkellä. Vastaanottoajan Lite asettaa itse.

- Malli vaatii samasta lähdejaksosta kolme onnistunutta 0x02/0x03/0x06-vastausta, todelliseen lähdelukuun sidotut kaikki arvot, ajan järjestyksen, enintään 60 s hajonnan, enintään 180 s iän, saman boot-epochin kasvavan laskurin ja kelvolliset arvorajat. Ensimmäinen viesti uudella yhteydellä on pelkkä baseline; **vasta seuraava oikea kasvava laskuri kelpaa**. Replay, reconnect, vanheneminen, kello-/pakettivirhe ja laskurin reset eivät saa uusintatuoreuttaa vanhaa arvoa.
- Uutta valvontasäiettä, watchdogia, auto-resettiä, fyysistä pumppuohjausta tai OTA-korvausta ei lisätty. Tapahtuman tarkistus kytketään samaan olemassa olevaan SSE-työntekijään, ja hylkäykset sulkevat mittauksen pois optimoinnilta.

## Pienin turvallinen upstream-muutos ennen fyysistä hyväksyntää

1. **Varmista ensin käytössä olevan CN105-sillan tarkka ESPHome-YAML, ulkoisen CN105-komponentin versio/commit ja nykyinen flash-/OTA-palautuspolku**. Näitä ei ole saatu HTTP SSE:stä. Älä arvaa yhteensopivuutta julkisen `main`-haaraversion perusteella.
2. Erillisessä build-/testiympäristössä lisää mallikohtaiseen CN105-komponenttiin jokaisen **onnistuneesti tarkistetun ja dekoodatun** `0x02` / `0x03` / `0x06` -UART-vastauksen jälkeen aikaleima, arvo ja poll-jaksotunniste. Palauta vastaavan dekoodauksen **todellinen onnistumistila**; pelkkä vastauksen saapuminen, yritys, ajastin tai muuttumaton climate-tila ei kelpaa.
3. Toteuta ja riippumattomasti todenna pysyvä, monotoninen boot-/UART-istuntolaskuri ja pumpun UART-dekoodauksen UTC-aikaleimojen oikea järjestys. Satunnainen boot-tunniste, yhteyden uptime tai vastaanottoaika ei kelpaa. Sähkökatkon ja uudelleenkäynnistyksen regressiot pakollisia; muuten F03 jää BLOCKED.
4. Nollaa kesken jäänyt 3-osainen kertymä UART-katkossa, timeoutissa tai yhteyden uusiutuessa; älä julkaise osittaista tai yhteen liimattua eri kierrosten tilaa. Kun kaikki 3 saman kierroksen vastausta ovat vahvistetut ja sillan UTC-kello on oikea, kasvata `seq`-laskuria ja julkaise **yhtenä** JSON-tekstisensorin tilapäivityksenä arvoineen. `boot` vaihtuu uuden bootin / luotettavuusjakson yhteydessä. Tällöin kaksi samaa lämpötilalukemaa saavat eri `seq`-arvon vain, jos kumpikin todella dekoodattiin uudesta UART-kyselystä.
5. Testaa eristettyä upstream-versiota ja Liten vastaanotinta mock-/testipenkissä, mukaan lukien katkot, laskurin nollaus, arvojen muuttumattomuus, yhteyden alussa toistuvat SSE-snapshotit, kellovirheet ja vanheneminen.
6. Vasta uuden laitekohtaisen palautus- ja fyysisen kokeen erillisen hyväksynnän jälkeen voidaan päivittää käytössä oleva CN105-silta. **Tässä työssä sitä ei ole muutettu eikä flashattu.**

### Hyväksynnän tila

**Host-koodi / parserin käännös ja lähdemittauslogiikan mock: PASS.** Käytössä olevan sillan todellinen mittauksen lähdetodiste: **BLOCKED / UNVERIFIED_SOURCE** kunnes yllä oleva producer toteutetaan ja mitataan pumpun UART-vastauksista. Laitetukea, todellista tuoreutta, pumpun ohjausta tai kustannussäästöä ei tällä väitetä todennetuksi.
