# Enevina Lite

**Yhden lämpöpumpun paikallinen optimointiohjain.**

Enevina Lite on Enevina Coresta erillinen, kevyt ohjain, jonka tavoitteena on minimoida lämpöpumpun lämmityskustannukset ja soveltuvilla laitteilla myös käyttöveden kustannukset turvallisesti. **Lite ei sisällä tekoälyä**, ei edellytä Home Assistantia eikä tarvitse Linuxia tai pilvipalvelua normaalissa käytössä.

> **Uusi ensimmäinen pilotti (16.11.2026):** pihamökin **Mitsubishi Electric MSZ-FD25VA / MUZ-FD25VA**. ESP32-S3 Wi-Fi + mallikohtaisesti vahvistettava CN105, Shelly Plus 1PM vain kulutusmittaukseen. Mökin tavoiteltu ylläpitolämpö noin +5 °C, ennakoiva turvamarginaali +8 °C; pumpun alin lämmityspyynti +16 °C, joten suunniteltu ohjaus on HEAT 16 °C / OFF. **Tämä ilma–ilmapumppu ei käytä menoveden tai käyttöveden optimointia.** Katso [pilottisuunnitelma](docs/cottage-mitsubishi-fd25va.md). Oikeaa laitetta ei vielä ohjata.

> Tila: suunnittelun lähtötaso. Tässä repositoriossa ei vielä ole toimivaa laiteohjelmistoa eikä todistettuja säästötuloksia.

## V1:n rajaus

- **Yksi lämpöpumppu per laite**.
- Ensisijainen laitealusta: **ESP32-S3 N16R8**, pihamökin pilotissa **2,4 GHz Wi-Fi**, muissa sovittimissa tarvittaessa Ethernet.
- C++ / ESP-IDF; mahdollisimman paljon optimointilogiikasta alustariippumatonta C++-koodia.
- 15 minuutin sähkönhintajakso; ennakointihorisontin tavoite 24–48 tuntia.
- Fysiikkapohjainen, deterministinen MPC-optimointi, **ei AI/ML**.
- Tilalämmityksen ja käyttöveden yhteinen optimointi, laiterajapinnan ja pumpun toimintarajojen sallimissa puitteissa.
- Paikallinen selainkäyttöliittymä, konfiguroitavat lämpötilarajat, lokit ja OTA-päivitykset.
- Paikallinen toiminta väliaikaisissa verkkokatkoissa; ennusteiden ja hintojen välimuisti.
- Ensimmäinen todellinen kohde: Mitsubishi Electric FD25VA -ilmalämpöpumppu; Panasonic/HeishaMon ja muut merkit myöhempinä adaptereina.

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

- Toteuta siirrettävä C++-referenssiversio. Mitsubishi-pilotissa vertaa HEAT/OFF-ohjausta tavalliseen ylläpitolämmitykseen; vesikiertoisissa vertaa sääkäyrään ja MILP-referenssiin.
- Mittaa ESP32-S3-laitteella **192 jakson / 48 tunnin** optimoinnin muistinkäyttö, suoritusaika ja vaikutus rinnakkaiseen ohjaukseen.
- Tavoitteena alle 2 sekunnin optimointiaika; tämä **ei ole vielä todennettu tulos**.
- Testaa käynnistys, verkkokatkot, epäkelvot anturitiedot, laiteyhteyden viiveet, sähkökatkon jälkeinen palautuminen ja watchdog.
- Älä salli tuotantolämpöpumpun ohjauksia ennen erillistä turvallisuus- ja laitekohtaista hyväksyntää.

## Kehitystilan periaate

Tämä repositorio on vain **Enevina Lite** -tuotetta varten. Enevina Core ja muut tuotantojärjestelmät pysyvät erillään. Ensimmäinen kehitysvaihe on simulaatio ja **shadow mode**, ilman kirjoituksia oikeaan lämpöpumppuun.
