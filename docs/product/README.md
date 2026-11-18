# Enevina Lite – kaupallinen tuotekehitys

Enevina Lite suunnitellaan **Enevina Oy:n ensimmäiseksi kaupalliseksi tuotteeksi**.

**Kehityslinja – käyttäjän uusin päätös**

**Tärkeintä on saada Enevina Lite ensin toimimaan.** V1:tä ei lukita monitoring-only-tilaan eikä jo kehitettyä lämmityksen ohjaus-/optimointikoodia poisteta. Aiemmat V1/V1.1/V2-nimet kuvaavat kaupallistamisen suunnittelua, mutta eivät rajoita turvallista host-/mock-kehitystä. Oikean pumpun uudet kirjoitukset edellyttävät erillistä laitekohtaista hyväksyntää.

**Ei uusia turhia watchdogeja tai reset-silmukoita.** Mitataan vian todellinen syy ja korjataan koodi; säilytetään nykyiset perustellut suojat.

**Aiemmat suunnitellut tuoteversiot (ei kehityslukitusta)**
- **V1 – ensimmäinen kaupallinen julkaisu:** tarkka ominaisuussisältö päätetään toimivan pilotin ja turvallisen tuotantovalidoinnin jälkeen; ei valmiiksi asetettua monitoring-only-pakkoa.
- **Energianeuvonta ja MPC:** niiden kehitys aloitetaan jo nyt, eikä niitä siirretä versiorajan vuoksi myöhemmäksi.
- **Fyysinen lämpöpumpun ohjaus:** kehitetään hostissa/mockissa/testipenkissä jo nyt; todellisen pumpun komennot hyväksytään erillisellä mallikohtaisella koesuunnitelmalla ja mahdollinen asiakasjulkaisu vasta todennuksen jälkeen.

## Käsikirjat

- [Enevina Lite – kaupallistamisen työlista L01–L25](COMMERCIAL_V1_BACKLOG.md): **14 P0, 7 P1, 4 P2**, täydet testimenetelmät, hyväksymiskriteerit, riippuvuudet ja kaupalliset **G0–G5**-portit.
- [Agenttiohjeet](../../AGENTS.md): kaikille koodausagenteille pakolliset toteutus-, testaus-, turvallisuus- ja Git-säännöt. AGENTS.md:n §0 on nykyisen kaupallistamisen korkein prioriteetti.
- [Repo README](../../README.md): alkuperäinen prototyyppi-/tutkimuskuvaus. Siinä kuvatut fyysisen ohjauksen ominaisuudet ovat nykyisessä kaupallisessa tuotejaossa **V2-tavoitteita**, eivät V1:n myyntisisältöä.

## Ensimmäinen toteutusjärjestys

**F01–F09 ensin: käynnistys, BLE, CN105, Shelly, hinnat/sää, UI, MPC, mock-ohjaus ja OTA.** Sen jälkeen kaupallistamisen L01–L25-portit toteutetaan todellisten puutteiden mukaan. Käyttöönotto L07 vasta L06:n jälkeen; L09 L08:n jälkeen; pitkä 72 h + 30 vrk lukittu kenttätesti L10 vasta L02–L09:n jälkeen. Valmistusketjun L13 ja vaatimustenmukaisuuden L14 hyväksyntä estävät kaupallisen myynnin ennen näyttöä.

## Todenmukainen tilanne ja laiterajat

- **Main-haara** ei automaattisesti sisällä pinottujen draft-PR:ien #5, #6 ja #7 toteutusta; jokainen agentti tarkistaa todelliset HEAD:t ennen työn aloittamista.
- **Kenttätestiin asennettu ESP32-S3** on erillinen koelaite. Tätä laitetta ei flashata, resetoida eikä kuormiteta pelkän dokumentaatiotehtävän vuoksi.
- **Core-koneen uusi ESP32-S3 N16R8** on regressiotesteihin tarkoitettu kortti, mutta sen bootloader/NVS:n muokkaaminen edellyttää palautussuunnitelmaa ja erillistä hyväksyntää.
- Kaupallista laitetta ei saa sanoa CE-vaatimustenmukaiseksi tai myyntivalmiiksi ilman porttikohtaista näyttöä. Tämä ei estä toiminnallisen laitteen kehitystä.
- Tässä dokumentaatiomuutoksessa ei asenneta firmwarea, käynnistetä fyysistä ohjausta eikä yhdistetä PR #5–#7:ää.

**Dokumentaation laatiminen ei merkitse L01–L25:n hyväksyntää tai V1:n kaupallista julkaisua.**
