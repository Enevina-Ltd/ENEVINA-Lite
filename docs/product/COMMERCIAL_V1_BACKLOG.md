# Enevina Lite – kaupallistamisen työlista L01–L25

**Lähde:** Enevina Oy:n 16.11.2026 antama 25 kohdan tehtävätaulukko. Tekstit on säilytetty tehtävien, syiden/komponenttien, testimenetelmien, hyväksymiskriteerien ja riippuvuuksien tasolla.

**Tuotejako:** V1 = monitoring-only, V1.1 = energianeuvonta ilman fyysisiä kirjoituksia, V2 = erikseen hyväksyttävä lämpöpumpun ohjaus. Enevina Lite on Enevina Oy:n ensimmäiseksi kaupalliseksi tuotteeksi suunniteltu tuote.

> Tämä dokumentti on työlista, ei valmistumisraportti. Avoimet PR #5–#7 ovat kehitystä, eivät main-haaran tai asiakastuotteen hyväksyntää. L01–L25:n tila on aluksi TODENNETTAVA; aiempia toteutuksia ei tehdä uudelleen ennen tarkastusta.

## Työlista

| ID ja prioriteetti | Mitä tehdään | Miksi ja komponentti | Testimenetelmä | Hyväksymiskriteeri | Edellytys |
|---|---|---|---|---|---|
| **L01 P0** | Lukitse V1 monitoring-only -build ja poista ohjauspolut toimituskuvasta | V1:n tuoteturvallisuus; cottage/supervisor.*, build-profiili | Rajapinta-/linkitystesti ja komentojen kaappaus mock-pumpulla | UI, HTTP, sarjaportti ja pilvi eivät voi tuottaa CN105-kirjoitusta | V1-tuotemäärittely |
| **L02 P0** | Tee OTA-kyvykkyydestä pakollinen release-ehto | Päivitettävyys voi kadota; cottage_secure_ota.cpp, main.cpp, tools/cottage_ota_publish.py | Oikein allekirjoitettu kuva ilman OTA-tehtävää/avaimia; kaksi peräkkäistä päivitystä | Virhekuva hylätään tai rollback; hyväksytty kuva pystyy seuraavaan päivitykseen | Release-identiteetin sopimus |
| **L03 P0** | Toteuta OTA/NVS-tilan keskeytyksestä palautuva täsmäytys | Väärä good/pending/bad-kirjanpito; samat OTA-tiedostot | NVS-virhe ja reset/katko jokaisessa tilasiirtymässä | Running-kuva, boot-tila ja digest täsmäävät; hyvä ei joudu väärään karanteeniin | L02, tilakonemäärittely |
| **L04 P0** | Lukitse bootloader ja jaa tehdas-/app-asennus turvallisiksi poluiksi | USB-upload voi korvata suojan; platformio.ini, bootloader-rollback-idf55, uusi flashausjigi | Generoidun kirjoituslistan tarkistus; varakortin hashit ennen/jälkeen ja rollback | App-asennus ei kirjoita bootloaderia/osiota/otadataa; tehdasbundle on kokonaan hyväksytty | Hyväksytty layout ja bootloader |
| **L05 P0** | Automatisoi oikean C++ OTA-toteutuksen virhematriisi ja fyysiset katkot | Nykyiset Python-/tekstihook-testit eivät kata flash-tilakonetta; tests, OTA | Väärä avain/malli/koko/hash, replay, TLS-katko, täysi NVS; vähintään 100 katkoa 3 yksilöllä | Ei palautumatonta vikaa; väärä ei käynnisty; oikea seuraava paketti asentuu | L02–L04 |
| **L06 P0** | Lisää omistajan todennus ja tilakohtainen hallinnan suoja | CSRF-token ei ole omistajatodennus; cottage_air_service.cpp, WebServer | Luvaton lähiverkkokäyttäjä, CSRF/Origin/Host, session vanheneminen ja brute force | Vain oikea omistaja muuttaa rajoja, sidontaa ja OTA-asetuksia | Uhkamalli ja laiteidentiteetti |
| **L07 P0** | Korvaa build-headerin WLAN-asetukset turvallisella provisioinnilla | Asiakas ei muokkaa koodia; cottage_local_config, UI ja NVS | Viisi uutta käyttäjää, iOS/Android, piiloverkko, väärä salasana, verkkovaihto | 5/5 peruspolkua alle 15 min ilman kehittäjää; ei salaisuuksia lokiin | L06, yksilökohtainen käyttöönottoavain |
| **L08 P0** | Tee yhtenäinen tuoreus- ja laatumalli kaikille V1-lähteille | Vanha arvo ei saa näyttää tuoreelta; cottage/telemetry.*, BLE/Shelly-adapterit | Puuttuva anturi, stale, replay, kellohyppy ja väärä yksikkö | UI, historia ja hälytys käyttävät samaa laatua; stale ei tuota uutta päätelmää | Tietomallin rajat |
| **L09 P0** | Varmenna watchdog, offline-tila ja NVS:n vikasietoisuus | Mökkiverkot katkeavat; RTOS-tehtävät, asetukset ja tallennus | Task jumissa, 24 h verkko poikki, rikki/täysi NVS, reboot ja migraation katko | Paikallinen seuranta palautuu; ei bootloopia, väärää aikaa tai hiljaista asetusten nollausta | L08, asetusskeema |
| **L10 P0** | Aja lukitun kokonaisversion 72 h kuorma ja 30 vrk kenttäjakso | Uusimman kokonaisuuden vakaus puuttuu; main.cpp, muisti, BLE/TLS/UI | G1:n yhtäaikainen kuorma ja vikainjektiot; yksi binary+bootloader | G1:n saatavuus, muistimarginaalit ja toipumisrajat täyttyvät; ei selittämättömiä resettejä | L02–L09 |
| **L11 P0** | Erota tuotanto/testi/kehitysavaimet ja toteuta avainpalautusharjoitus | Asiakaskuvaan ei väärää avainta; tools/cottage_ota_provision.py, signer, CI | Väärän kanavan allekirjoitus, avaimen vaihto, varakopion palautus ja peruutus | Tuotantolaite hylkää testikuvan; päivitys jatkuu hyväksytyn rotaation jälkeen | L04, avainpolitiikka |
| **L12 P0** | Pinnaa työkalut ja riippuvuudet, tuota SBOM ja lisenssit | Build ei saa muuttua huomaamatta; platformio.ini, CMake, julkaisu | Kaksi puhdasta buildia, dependency- ja notice-katselmus | Versiot ja alkuperä todettu, erot selitetty; ei tuntematonta koodilisenssiä | Hyväksytty SDK/bootloader |
| **L13 P0** | Tee pilottierän yksilöprovisiointi ja tuotantotestijigi | Laitekanta ei saa jakaa salaisuutta; uusi manufacturing-paketti | 10 laitetta, ristikkäinen sarjanumero/QR ja väärä firmware | 10/10 jäljitettäviä; väärä laite tai bundle hylätään | L04, L11, L12, lopullinen levy |
| **L14 P0** | Kokoa riskit, FI/SV-ohjeet, tekninen tiedosto ja vaatimustenmukaisuusnäyttö | Myynti ei ala prototyyppimerkinnällä; docs/product, uusi compliance | Liitteen C matriisin sulkeminen ja G3-katselmus | Kaikki soveltuvat velvoitteet osoitettu, DoC ja merkinnät oikealle versiolle | L01–L13 ja laboratoriotulokset |
| **L15 P1** | Todista CN105:n mittauskohtainen lähdetuoreus tai poista lähde V1:stä | SSE-vastaanotto ei todista UART-lukua; adapteri ja upstream-silta | Väylä irti, silta jäässä, muuttumaton arvo ja reconnect | Lähdemittauksen laskuri/ikä todentuu; muuten ominaisuus ei kuulu toimitukseen | Upstream-sopimus; ennen V2:ta ehdoton |
| **L16 P1** | Yksilöi Shelly-yhteensopivuus ja mittauskohde | Nykydata on koko mökistä; Shelly-adapteri, UI | Tuetut mallit/fw:t, nollaus/ylivuoto, väärä piiri ja offline | Pumpun säästöä ei lasketa koko mökin mittauksesta; asennettu lähde selvästi nimetty | L08, todellinen mittauskohde |
| **L17 P1** | Toteuta historian eheys, CSV-vienti ja turvallinen omistajanvaihto | Käyttöikä, tietosuoja ja asiakkaan data; storage/UI/NVS | 30 vrk aineisto, katko kirjoituksessa, kaksi anturia, reset ja tokenin peruutus | Sovittu säilytys toteutuu, ei sekoittumista; edellisen omistajan käyttö päättyy | L06–L09 |
| **L18 P1** | Tuotteista valinnainen etäilmoitus ja katoavan laitteen hälytys | LAN-sivu ei hälytä etäällä; uusi remote-adapteri | Push/email katkolla, heartbeat puuttuu, tilaus päättyy | Katko näkyy erillisenä tilana; paikallistoiminnot jatkuvat; asiakkaiden data ei sekoitu | L06, L08, GDPR-roolit ja palveluehdot |
| **L19 P1** | Ratkaise Nord Pool/ENTSO-E-oikeudet ja FMI-attribuutio | Tekninen URL ei ole jakelulupa; data-adapterit ja UI | Lisenssikatselmus, attribution, rajat ja lähteen katko | Kirjallinen käyttöperuste jokaiselle julkaistulle datatoiminnolle | Valittu palvelumalli; ennen V1.1:tä |
| **L20 P1** | Yhtenäistä README, PR-tila ja testien näyttöluokat | Vanha kuvaus sanoo OTA:n puuttuvan; docs/PR-muistiot | Väitteiden vertailu SHA:han ja alkuperäiseen testiin | main/draft/projektiraportti/uusi testiajo erottuvat eikä ristiriitoja jää | L02–L05 tulokset |
| **L21 P1** | Mittaa väärät hälytykset, käyttöönoton tuki ja maksuvalmius | Ostoperuste ja kate avoinna; pilotin aineisto | 5 käyttöönottoa, 20 haastattelua, poikkeamien manuaalinen luokitus | G4/G5; hälytyksistä aitojen/väärien ja puuttuneiden määrät raportoitu | L07–L10 ja V1 toimitussisältö |
| **L22 P2** | Kalibroi kevyt lämpömalli ja neuvonnan epävarmuus | V1.1; CottagePhysics, MPC ja mallien testityökalut | Eri opetus/arviointijaksot, vertailu pysyvyysmalliin | Sovitut MAE-/harharajat ja epävarmuusväli osoitettu; vajaa data estää neuvon | L08, L10, L16, L19 |
| **L23 P2** | Suunnittele V2:n riippumaton turvarakenne ja pumpun rajat | Fyysisen ohjauksen vastuu; supervisor, lämpö-/pumppuasiantuntija | Anturi/CPU/sähkö/verkko-vika, käsiohitus ja paikallinen fallback penkillä | Jäännösriski hyväksytty erikseen; pumppu ei jää haitalliseen tilaan | L15 ja erillinen V2-suunnittelupäätös |
| **L24 P2** | Toteuta kirjoittava mallikohtainen adapteri V2:een | Nykyinen koodi ei ohjaa; CN105 adapteri | Mock, hyväksytty testipenkki, ack/nack, timeout, command-rate ja reboot | Vain turvavalvojan sallimat komennot; ei verkkovirran katkaisua; mallilista todennettu | L23; fyysiset kokeet erillisellä toimeksiannolla |
| **L25 P2** | Todista V2:n nettosäästö ja yhteinen Lite/Core-mallirajapinta | Tuoteperheen arvo; mallikirjasto ja validointi | Useita kohteita/kausi, sää- ja käyttökorjaus, sama olosuhdetaso; rajapinnan versiotesti | Läpinäkyvä hyöty haittoineen ja epävarmuuksineen; Core ei ohita Lite-turvaa | L22–L24, tutkimusprotokolla |

## Etenemisjärjestys ja riippuvuudet

- Aloitus: **L01**, **L04**, **L02**, **L03**. Ensimmäiseksi lukitaan V1:n toimitussisältö, release-identiteetti, flash-layout ja rollback-bootloader. L03 seuraa L02:n tilakonemäärittelyä.
- **L05** aloitetaan vasta L02–L04:n jälkeen: 100 keskeytystä kolmella erillisellä laiteyksilöllä; yhden uuden testikortin tulos on vain osatulos.
- **L06 → L07**: omistajan todennus ennen WLAN-provisiointia. **L08 → L09**: tuoreus- ja laatumalli ennen pitkäaikaisen offline- ja NVS-käytöksen vahvistusta.
- **L10** vasta L02–L09:n yhteisellä lukitulla sovelluskuvalla ja bootloaderilla; 72 tunnin kuorma sekä 30 vuorokauden kenttäjakso on dokumentoitava täysimääräisinä.
- **L11/L12 → L13 → L14**: avainten elinkaari, toistettavat buildit ja SBOM, yksilöprovisiointi ja valmistus, lopuksi soveltuvat vaatimustenmukaisuusarviot.
- **L15–L21** täydentävät V1:n toimitettavia ominaisuuksia, käytettävyyttä, asiakastukea ja lupia. CN105 vain jos sen mittauskohtainen tuoreus voidaan todistaa; muuten jätetään V1-toimituksesta pois.
- **L22** on V1.1; **L23–L25** ovat V2-ohjaus, jota ei saa aktivoida V1:n tuotantobinaarissa tai kenttäpilotissa.

## Kaupallistamisen päätösportit G0–G5

| Portti | GO / pakollinen näyttö | NO-GO |
|---|---|---|
| **G0 – Tekninen perusta** | L01–L05: V1-tuotebinaarissa ei fyysisiä kirjoituksia; pakollinen OTA, NVS-tila, bootloader ja virhetestaus todennettu | P0-turvavika, todentamaton rollback tai saavutettava pumpun kirjoituspolku |
| **G1 – Kenttäpilotti** | L06–L10: toimiva paikallishallinta ja onboarding, lähdetuoreus, offline/valvonta; 72 h kuorma ja 30 vrk kenttätesti samasta lukitusta binääristä | Keskeytynyt testi tai selittämättömät resetit eivät ole PASS |
| **G2 – Tuotearkkitehtuuri** | Vakioitu laitteisto, flash/layout ja releaseketju, SBOM, 10 yksilön valmistusjigi ja yksilölliset identiteetit (L11–L13) | Kehityskortin yksittäinen toimiva build ei riitä |
| **G3 – Vaatimustenmukaisuus** | L14: soveltuvien vaatimusten testit, riskien arviointi, FI/SV-ohjeet, tekniset asiakirjat, asianmukaiset merkinnät ja EU DoC | CE-/muu vaatimustenmukaisuusväite ilman laiteversiokohtaista näyttöä |
| **G4 – Pilottierä** | Sovittu laite-erä, laitekohtaiset testit, jäljitettävyys, L21:n käytettävyys-/hälytysnäyttö ja valmistuslaatu | Yhteiset salaisuudet, puutteellinen jäljitettävyys tai puuttuvat testit |
| **G5 – Kaupallinen julkaisu** | G0–G4 hyväksytty, V1-rajat ja hinta hyväksytty, tuki/huolto/tietoturvapäivitykset ja myyntidokumentaatio valmiina | Prototyyppiä ei saa nimetä kaupalliseksi julkaisuksi ilman näyttöä |

**Portin hyväksyjän tulee olla erillinen kriittisen portin toteuttajasta.** P0-työlle sovitaan vähintään tuoteomistaja, tekninen katselmoija ja testitodisteet. Vanhan prototyypin G0–G3 ovat eri portit kuin tämän kaupallistamissuunnitelman G0–G5.

## Agentin Definition of Done – jokaiselle tehtävälle

1. Tunnista olemassa oleva toteutus, main ja avoimien PR:ien HEAD:t, riippuvuudet, aiemmat testiraportit ja todelliset puutteet.
2. Määritä ennen muutosta fail-closed-virhepolku, hyväksymiskriteeri, laite- ja ohjelmistoversiot. V1:n fyysiset kirjoitukset kielletään.
3. Toteuta rajattu muutos erillisessä haarassa. Lisää virheeseen kohdistuva regressiotesti, älä vain tekstihaku-asserttia.
4. Ilmoita erikseen host/mock, C++ Debug/Release, Python, käännös, HIL, fyysinen ja kenttätesti. Liitä run ID, SHA, ohjelmiston ja bootloaderin hashit, käyttö- ja olosuhdejakso, tulos sekä avoimet riskit.
5. Kriittistä P0-työtä ei kuitata ilman toistettavaa testinäyttöä ja riippumatonta katselmointia; PR pidetään draftina siihen asti.
6. Päivitä tehtävän status muodossa TODO / IN PROGRESS / BLOCKED / REVIEW / PASS sekä linkit issueen, PR:ään ja näyttöön. Älä käytä PASS-merkintää pelkästä aiemmasta suunnitelmasta.

## Laitetestaamisen rajat

- Pihamökissä oleva ESP32 on V1:n kenttäpilotissa. Sen flashaaminen, reboot, OTA-jakelu tai automaattisen fyysisen ohjauksen aktivointi edellyttää erillistä hyväksyntää; sitä ei käytetä L05:n tuhoaviin katkotesteihin.
- Core-koneeseen liitetty uusi ESP32-S3 N16R8 on erillinen regressiotestikortti (16 Mt Flash, 8 Mt PSRAM). Sen alkuperäinen flash-prefix on otettu talteen, mutta tämä ei vielä anna lupaa pyyhkiä NVS:ää tai kirjoittaa bootloaderia.
- Enevina Core, Adaptive Intelligence, Finance, Home Assistant, tuotannon CN105-silta ja Shellyn rele ovat tämän työlistan muutosten ulkopuolella.

## Tilannekirjanpito

- Yhteensä **25 tehtävää: P0 14, P1 7, P2 4**.
- Tämän dokumentin laatiminen ei tarkoita yhdenkään Lxx-tehtävän hyväksymistä tai valmista kaupallista tuotetta.
- Kaupallinen laatu on todistettava ennen markkinoille saattamista; juridiset ja komponenttikohtaiset vaatimukset vahvistetaan erikseen oikeista ajantasaisista lähteistä.
