# Enevina Lite – kaupallinen tuotekehitys

Enevina Lite suunnitellaan **Enevina Oy:n ensimmäiseksi kaupalliseksi tuotteeksi**.

**Tuoteversiot**
- **V1 – Monitoring-only:** mökin/rakennuksen paikallinen olosuhde- ja energiavalvonta ilman pumpun tai Shelly-releen ohjauskirjoituksia.
- **V1.1 – Energy Advisory:** mitattuun laatuun perustuvat energia- ja lämpömallin neuvot; ei fyysisiä komentoja.
- **V2 – Intelligent Heating Control:** erillinen tuotteistamis-, turva- ja laitekohtainen hyväksyntä ennen lämpöpumpun ohjausta.

## Käsikirjat

- [Enevina Lite – kaupallistamisen työlista L01–L25](COMMERCIAL_V1_BACKLOG.md): **14 P0, 7 P1, 4 P2**, täydet testimenetelmät, hyväksymiskriteerit, riippuvuudet ja kaupalliset **G0–G5**-portit.
- [Agenttiohjeet](../../AGENTS.md): kaikille koodausagenteille pakolliset toteutus-, testaus-, turvallisuus- ja Git-säännöt. AGENTS.md:n §0 on nykyisen kaupallistamisen korkein prioriteetti.
- [Repo README](../../README.md): alkuperäinen prototyyppi-/tutkimuskuvaus. Siinä kuvatut fyysisen ohjauksen ominaisuudet ovat nykyisessä kaupallisessa tuotejaossa **V2-tavoitteita**, eivät V1:n myyntisisältöä.

## Ensimmäinen toteutusjärjestys

**L01 → L04/L02 → L03 → L05**, samalla voidaan valmistella L06, L08, L11 ja L12. Käyttöönotto L07 vasta L06:n jälkeen; L09 L08:n jälkeen; pitkä 72 h + 30 vrk lukittu kenttätesti L10 vasta L02–L09:n jälkeen. Valmistusketjun L13 ja vaatimustenmukaisuuden L14 hyväksyntä estävät kaupallisen myynnin ennen näyttöä.

## Todenmukainen tilanne ja laiterajat

- **Main-haara** ei automaattisesti sisällä pinottujen draft-PR:ien #5, #6 ja #7 toteutusta; jokainen agentti tarkistaa todelliset HEAD:t ennen työn aloittamista.
- **Pihamökissä oleva ESP32-S3** on erillinen kenttäpilotti. Tätä laitetta ei flashata, resetoida eikä kuormiteta pelkän dokumentaatiotehtävän vuoksi.
- **Core-koneen uusi ESP32-S3 N16R8** on regressiotesteihin tarkoitettu kortti, mutta sen bootloader/NVS:n muokkaaminen edellyttää palautussuunnitelmaa ja erillistä hyväksyntää.
- V1:stä ei saa sanoa kaupallisesti valmista, CE-vaatimustenmukaista tai turvalliseksi hyväksyttyä ilman porttikohtaista näyttöä.
- Tässä dokumentaatiomuutoksessa ei asenneta firmwarea, käynnistetä fyysistä ohjausta eikä yhdistetä PR #5–#7:ää.

**Dokumentaation laatiminen ei merkitse L01–L25:n hyväksyntää tai V1:n kaupallista julkaisua.**
