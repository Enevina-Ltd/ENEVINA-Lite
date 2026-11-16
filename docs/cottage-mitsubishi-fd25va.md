# Enevina Lite — ensimmäinen pilotti: pihamökin Mitsubishi Electric FD25VA

**Päätös 16.11.2026:** Enevina Lite V1:n **ensimmäinen todellinen kohde** on käyttäjän pihamökin vanha **Mitsubishi Electric MSZ-FD25VA (sisäyksikkö) + MUZ-FD25VA (ulkoyksikkö), noin 2009**. Kyseessä on **ilma–ilmalämpöpumppu, ei Panasonic Aquarea eikä ilma–vesi-/maalämpöpumppu**. Tässä pilotissa ei ole käyttövesivaraajaa tai menoveden säätöä. Yksi Lite ohjaa vain yhtä pumppua.

Tämän pilotin toteutus ohittaa aiemman Panasonic/HeishaMon-ensimmäisyyden, mutta ei poista tai riko avoimen PR #1:n yhteistä fysiikka-/optimointitutkimusta. Erota pumpputyypin mallit ja adapterit; älä pakota ilma–ilmapumppua hydronisen lämmönjaon ja käyttöveden toimintatiloihin. Legacy-ulkoanturin simulointi jää muille pumpputyypeille, **ei tämän Mitsubishi-pilotin ensisijaiseksi ohjaustavaksi**.

## Aiempien keskustelujen vahvistetut lähtötiedot

- Sisäyksikkö **MSZ-FD25VA**, ulkoyksikkö **MUZ-FD25VA**; vanha **Mitsubishi Heavy Industries** -profiili on virheellinen.
- Mökin haluttu talvinen ylläpitolämpö **noin +5 °C**, mutta aiemmin sovittu **ennakoivan ohjauksen turvamarginaali +8 °C**. Sisälämpö **ei saa suunnitelmassa alittaa +5 °C**. Älä lupaa tätä ilman riippumatonta suojausta ja reaaliaikaista validointia.
- Pumpun pienin lämmityksen asetusarvo on **+16 °C**. Tämä on **pumpun sallittu käyntitilan asetusarvo**, ei koko ajan käytettävä mökin tavoite. +5 °C ylläpitolämmitys edellyttää hallittua **HEAT 16 °C / OFF** -jaksotusta, ei 5 °C:n asetusarvokomentoa pumpulle.
- Pihamökissä on erillinen **Shelly Plus 1PM**, jonka rele on pidettävä päällä; sen tehtävä on mitata mökin **kokonaissähkönkulutusta**, ei katkaista kompressorin tai pumpun syöttösähköä. Shellyn mittaus sisältää mahdolliset muut mökin kuormat ja on siksi vain **approksimaatio** lämpöpumpun teholle.
- **Aiemmissa 22.10. ja 26.10.2026 keskusteluissa todellista CN105-yhteyttä käytettiin onnistuneesti:** ESP32-S3/Wi-Fi-ohjain, firmwareksi ilmoitettu 2026.5.2, paikallinen `/events`-SSE-telemetria (HTTP 200) ja sisälämpötilan, asetuksen, kompressoritaajuuden sekä käyntitilan read-only-luku. Lokissa nähtiin esimerkiksi HEAT, sisä 12 °C, pyynti 16 °C, kompressori 19–20 Hz, sekä myöhemmin OFF/0 Hz. **Ohjauskomentoja ei testattu.** Aiemmat mittaustulokset ovat historiallisia: varmista yhteyden nykyinen tila ennen Liten integrointia. Älä ohita nykyisen toimivan ohjaimen firmwarea tai OTA-asetuksia.
- **Ei tekoälyä, ei HA:n, Coren tai pilven jatkuvaa riippuvuutta.** Hinnan ja FMI-sään lataus paikalliselle Lite-ohjaimelle, paikallinen fallback.
- **Laiteosoitteita tai tunnuksia ei tallenneta julkisiin Git-tiedostoihin**; ne kuuluvat paikalliseen konfiguraatioon.

## Ohjausliitännän soveltuvuus ja lähteet

- Mitsubishi Electricin MAC-567IF-E -Wi-Fi-sovittimen yhteensopivuustaulukossa on **MSZ-FD25/35/50VA**; dokumentaatio näyttää CN105/CN92-yhteyden. Tämä tukee **CN105:n mahdollisuutta**: https://manuals.plus/m/6cb6bcb2b4e6d231f3b692ff896b1664952c80fcd6e0e09c25018a4b85e897c4_optim.pdf
- MSZ-FD25VA:n huoltokäsikirja OBH488 näyttää sisäkortin komponentit, mutta **ei yksiselitteisesti merkitse CN105:tä** näkyvään kytkentäkaavioon. Siksi mallin ja korttirevision todellinen liitin on tarkastettava: https://scanoffice.fi/wp-content/uploads/2023/09/Huolto-ohje_MSZ-FD.pdf
- Yleisen Mitsubishi CN105 -protokollan avoimia toteutuksia on olemassa, mutta ne **eivät todista käyttäjän yksilön toimintaa**. Esimerkki: https://github.com/Konderi/esphome-mitsubishi-heatpump
- CN105:n logiikkatasot ja mahdollinen 5 V vs 3,3 V -sovitus tulee varmistaa valitusta ESP32-ohjaimesta. Älä kytke GPIO:ita suoraan tuntemattomiin signaaleihin.

## Ohjausarkkitehtuuri ja prioriteetit

1. **MitsubishiCN105Adapter** (ensisijainen): hyödynnä aiemmin toiminutta read-only `/events`-SSE-rajapintaa ja nykyistä firmwarea alkuvaiheen **ulkoisena datalähteenä** ilman uudelleenflashausta. Lopullisessa yhden ESP:n Lite-firmwaressa CN105-protokollan voi toteuttaa suoraan vasta erillisen yhteensopivuus-/turvatestauksen jälkeen. HEAT 16 °C / OFF -komennot, kuittaus tai havaittu tila on testattava erikseen; aiemmin on osoitettu vain telemetrian toiminta.
2. **MitsubishiIRAdapter** (valinnainen varajärjestelmä): vain jos CN105 ei toimi ja kyseisen kaukosäätimen komennot voidaan toistaa luotettavasti; IR:llä ei aina saada palautetta, joten suljettua ohjausta ei saa teeskennellä vahvistetuksi.
3. **CottageTemperatureProvider**: pumpun ilmoittaman sisälämmön lisäksi ensisijaisesti **riippumaton** mökin lämpötila-anturi jäätymisriskin arviointiin. Telemetrian tuoreus on pakollinen.
4. **ShellyPowerObserver**: vain sähkön mittaus, ei `switch`/`relay`-kirjoituksia eikä laitteen syöttövirran katkaisua.
5. **CottagePhysics**: ilma–ilmalämpöpumpun rakennuksen RC-lämpömalli, ulkoilma, lämpökuormat, pumpun ON/OFF vaste ja COP/kapasiteettirajat. Vasta todellisilla mittauksilla kalibroiduilla tehoilla tarkempi kustannusoptimointi.
6. **CottageMPC**: 15 min jaksot, 24–48 h suunnittelu, 16 °C HEAT / OFF, minimikäynti-/seisonta-aika, hystereesi ja vähäiset tilamuutokset. Rakennuksen turvallinen lämpötila on kovempi vaatimus kuin säästö.
7. **CottageSupervisor**: estää kirjoittamisen oletuksena. Vasta testatun laiteliitännän, lämpötilan tuoreuden, command ack:n, määriteltyjen turvarajojen ja erillisen hyväksynnän jälkeen rajoitettu ohjaus.

## Toimintaperiaate (ei vielä hyväksyttyä laiteohjausta)

- Pakkasella tai alle sovitun **+8 °C:n ennakko-/turvamarginaalin** ohjain pyytää lämmitystä (HEAT, +16 °C), kun rajat ja laitteiden tilat sen sallivat. +8 °C ei ole pumpun asetusarvo. Vakiintumisen jälkeen +5–+8 °C alueen käyttöä arvioidaan **todellisilla mittauksilla**.
- Lämmitä ennakoivasti halvemmissa jaksoissa vain, jos lämpötila- ja energiavertailu osoittaa hyödyn. Älä tee jatkuvaa 15 min ON/OFF-kytkentää; optimoinnin aika-askel **ei** ole sallittu kompressorikytkentäväli.
- Kun sääennuste tai hintatieto puuttuu, **lopeta kustannusoptimointi**. Jos valvonta ja yhteys toimivat, noudata konservatiivista jäätymissuojaa; jos pumppu ei vastaa, hälytä ja merkitse ohjaustila epävarmaksi.
- +5 °C:n ehdotonta pakkassuojausta ei voi taata ohjelmistolla tai vanhalla ilmalämpöpumpulla ilman erillistä riippumatonta suojausta, sähkönsyöttöä ja toiminnan valvontaa. Kriittisiä vesiputkia/irtaimistoa ei saa jättää tämän testiohjaimen varaan.
- Shelly Plus 1PM:n releen on pysyttävä päällä ohjaimen komentoihin katsomatta. Seuranta voi havainnoida sähkötehon ja todellisen lämmitysvasteen erot mutta ei voi yksin todistaa kompressorin käyntiä.
- Etäkäyttöä ei tarvita. Ohjaimen paikallinen web-UI näyttää lämmityksen tilan, mökin lämpötilan, spot-hinnan, varatilan syyn sekä ennustetun +5/+8 marginaalin.

## Hyväksyntäportit

**C0 — PC-simulaatio:** oma C++ `CottagePhysics` ja `CottageMPC` / safety-moduuli; testaa 16 °C ON / OFF, hystereesi, alarajat, jaksotus, viiveet, sähkönhinnan puuttuminen, virheelliset sensorit, 24–48 h lämpötilan ennuste, kompressorirajoitukset ja samat alku-/lopputilat MILP-vertailussa. Ei pumpun kirjoituksia.

**C1 — Laitteen kyvykkyydet:** aiemmat lokakuun 2026 CN105- ja Shelly-lukukokeet antavat lähtönäytön; tarkista **nykyinen** `/events`-SSE-telemetria, kuormitustietojen oikeellisuus, ESP:n firmware/pinout, erillisen sisäanturin olemassaolo sekä tietojen tuoreus. Ei ohjauskomentoja eikä vanhan firmwareversion korvaamista.

**C2 — Varjotilan integrointi:** kaikki datalähteet ja lokalisoitu UI ESP32:ssa, jatkuva seuranta, valvotut virhetilat ja simuloidut komennot; yhteyskatko-, kellonajan-, watchdog- ja huoneen jäähtymistestit.

**C3 — Rajoitettu ohjaus ja tuotanto:** hyväksy käyttäjän pyynnöstä erikseen vasta laitekohtaisen toiminnan, min ON/OFF aikojen, virheistä palautumisen, luotettavan jäätymissuojauksen ja 24–48 h taloudellisen vertailun jälkeen.

## Ei vielä valmis

Nykyisen PR #1:n algoritmi käsittelee rakennuksen **tilalämmitystä ja käyttövesivaraajaa**, joka ei vastaa tämän ilma–ilmapumpun aktuaattoria. Se on uudelleenkäytettävä tutkimuspohja, ei valmis mökkiohjain. Tämä dokumentti **ei kytke, flashaa tai ohjaa** mitään fyysistä laitetta.
