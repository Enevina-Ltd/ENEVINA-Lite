## Testifirmware ESP32-S3 N16R8:lle (kehittäjätesti)

**v0.1.0-alpha.2 · 19.11.2026 · experimental app-only image**

Tämän julkaisun `Enevina-Lite-v0.1.0-alpha.2-ESP32-S3-N16R8-app-only-TEST.bin` on käännetty täsmälleen julkaisutagista `v0.1.0-alpha.2` (commit `95148822ec3385dbb13be2415fe04382e78c2d4b`). Julkaisun `.sha256`-tiedosto kertoo binäärin SHA-256-tarkistussumman.

### Tähän kuvaan kuuluu

- ESP32-S3 (16 MiB flash, 8 MiB OPI PSRAM); PlatformIO-ympäristö `esp32s3_cottage_shadow` ja valmistajan alkuperäinen 2,4 GHz Wi-Fi.
- Paikallinen dashboard, langattoman verkon ensiasennus, sähkön hinnan ja sääennusteen lähdekooditoiminnot, RuuviTagin luku sekä Midea V2/V3 **vain tilanluku**.
- Mitsubishi Electric CN105:n kehitystoteutus, jonka fyysinen ohjaus on mahdollista sallia erikseen asetuksista; **älä kytke testiä oikeaan lämpöpumppuun tai salli ohjausta ilman laitekohtaista testisuunnitelmaa**.
- Ei valmistajan pilven tunnuksia, ei laitekohtaisia OTA-luottamusavaimia, ei allekirjoitettua OTA-julkaisukanavaa.

### Mitä tämä EI ole

- **Ei valmis ensimmäisen USB-flashin paketti**: tämä `.bin` sisältää vain sovelluksen, ei bootloaderia, osiotaulua tai `otadata`-aluetta. Tavallista `pio run -t upload` -toimintoa ei ole tarkoituksella sallittu.
- **Ei dashboardista asennettava OTA-kuva**: kuvassa ei ole yksityisen julkaisukanavan luottamusasetuksia tai kanava-allekirjoitusta; dashboardin OTA-painike pysyy poissa käytöstä, kunnes hyväksytty allekirjoitettu kanava on erikseen asennettu.
- **Ei kaikkiin ESP32-S3-kortteihin yleispätevä kuva**: erilaiset flash-/PSRAM-koot, muistitilat ja osiotaulut voivat estää käynnistymisen.
- **Ei myyntiin, tuotantokäyttöön tai valvomattomaan lämmitysohjaukseen hyväksytty firmware**. Midea- ja CN105-yhteensopivuudet ovat mallikohtaisia kehitystestejä.

### Turvallisen asennuksen ennakkoehdot

1. Varmista kortin tarkka piiri, 16 MiB flash ja 8 MiB PSRAM sekä sen **todellinen, laitteilta luettu** bootloader/osiotaulu. Projekti tuntee ainakin kaksi toisistaan poikkeavaa 16 MiB:n osiotaulua. Älä oleta että `0x10000` on aina oikea sovellusosoite.
2. Ota **ehjä koko flashin varmuuskopio**, tarkista SHA-256, varmista palautustapa ja säilytä NVS/OTA-salaisuudet pois julkisista lokitiedoista. 17.11.2026 erillisen testikortin koko flashin lukuyritykset epäonnistuivat; sille **ei ole** asennuslupaa.
3. Irrota testi ohjaavista lämpöpumppukaapeleista ja laitteista, joissa firmwaremuutos voisi vaikuttaa lämmitykseen.
4. Käytä laitekohtaisesti tarkastettua **app-only**-asennusmenettelyä vasta yllä olevien tarkistusten ja erillisen asennushyväksynnän jälkeen. Tässä julkaisussa ei anneta yleistä kirjoituskomentoa, joka voisi tuhota rollback-bootloaderin tai NVS:n.

**English:** Experimental ESP32-S3 N16R8 *application-only* developer image built from the exact alpha.2 tag. Not a complete factory-flash image, not publisher-signed OTA, and not approved for use on an unknown board or a connected heat pump. Verify chip, partition table, full-flash backup and recovery before any model-specific flashing. The separately documented F02 test card's full-flash backups previously failed, so **do not flash that card**.

Tekninen lähde: [PlatformIO-kokoonpano](https://github.com/Enevina-Ltd/ENEVINA-Lite/blob/v0.1.0-alpha.2/firmware/cottage/platformio.ini) · [F02-testikortin varmistusesteet](https://github.com/Enevina-Ltd/ENEVINA-Lite/blob/v0.1.0-alpha.2/docs/field/F02_TESTCARD_RECOVERY_20261117.md) · [OTA:n vaatimukset](https://github.com/Enevina-Ltd/ENEVINA-Lite/blob/v0.1.0-alpha.2/docs/DASHBOARD_SIGNED_OTA_20261119.md)
