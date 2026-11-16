# AGENTS.md — Enevina Lite

## 0. KAUPALLINEN ENEVINA LITE V1 – KORKEIN OHJEPRIORITEETTI (16.11.2026)

**Tämä uusi ohje syrjäyttää alempien vanhojen lukujen V1-ohjausta tarkoittavat historialliset tavoitelauseet.** Enevina Oy:n ensimmäinen kaupallinen tuote on **Enevina Lite V1 = monitoring-only** (paikallinen mökin olosuhde- ja energiavalvonta). **V1.1 = energianeuvonta ilman laitekirjoituksia. V2 = fyysinen lämpöpumpun ohjaus vasta erillisen hyväksynnän jälkeen.**

**Sitova tuotekehityslista:** [docs/product/COMMERCIAL_V1_BACKLOG.md](docs/product/COMMERCIAL_V1_BACKLOG.md), tehtävät **L01–L25** ja kaupalliset hyväksymisportit **G0–G5**. Alempana olevat vanhat G0–G3-portit ovat tutkimusportteja, eivät kaupallisen myynnin hyväksyntä.

### V1:n kielletyt toiminnot ja arkkitehtuuri

- **L01:** Toimitettavasta V1-binaarista on poistettava tai todistettavasti estettävä KAIKKI fyysiset lämpöpumpun/CN105:n, IR:n ja Shelly-releen kirjoituspolut. UI, HTTP API, UART/serial, verkko, pilvi ja debug eivät saa tuottaa pumpun komentoa. Diagnostiikan physical_writes=false -kenttä ei yksin riitä; vaaditaan riippumaton linkitys-/rajapinta-/komentojen kaappauskoe.
- **L02–L05:** Turvallinen OTA on pakollinen release-ehto, ei valinnainen debug-ominaisuus. Päivitettyyn kuvaan on jäätävä kyky vastaanottaa seuraava allekirjoitettu päivitys. Bootloaderin ja NVS:n good/pending/bad-tila on täsmäytettävä sähkökatkojen jälkeen; app-only-päivitys ei saa korvata bootloaderia, osiotaulua tai otadataa. Virhetestit tehdään ensin ilman laitetta ja myöhemmin hyväksytyllä regressiotestikortilla.
- **L06:** CSRF-token **ei korvaa omistajan todennusta**. Lähiverkon käyttöä, istuntoja, brute force -yrityksiä, omistajanvaihtoa ja Host/Origin-tarkistuksia on suojattava hyväksyttyyn uhkamalliin perustuen.
- **L07:** Asiakas ei saa joutua muokkaamaan lähdekoodia Wi-Fi-asetuksia varten. Provisioinnin salaisuudet eivät kuulu GitHubiin, lokeihin, yhteisiin firmwarekuviin tai dokumentteihin.
- **L08–L09:** Kaikilla mittaustiedoilla on laatu, aika, ikä, lähde ja yksikkö. Vanhentunut arvo ei saa näyttää tuoreelta. CN105:n SSE-heartbeat ei yksin todista tuoretta pumppumittausta. Offline-, watchdog- ja NVS-vikatilanteiden on säilytettävä turvallinen paikallinen valvonta.
- **L10:** 72 tunnin yhdistelmäkuorma + 30 vuorokauden kenttäjakso samalla lukitulla app+bootloader-binaarilla ennen G1-päätöstä. Keskeytyneistä tai eri binääreillä tehdyistä osatesteistä ei saa muodostaa tekaistua PASSia.
- **L11–L14:** Dev/test/prod-avainmateriaalit erilleen, avainrotaatio ja palautus, riippuvuuslukot, SBOM ja lisenssit, valmistusjigi, yksilölliset identiteetit, riskit, FI/SV-ohjeet, soveltuvat laboratoriotestit ja vaatimustenmukaisuusdokumentaatio ennen ensimmäistä kaupallista myyntiä. Ei väitteitä CE-, RED- tai CRA-vaatimusten täyttymisestä ilman näyttöä.
- **L15–L21:** V1:n mittaus- ja hälytysominaisuudet sekä asiakaskäyttö ovat ehdollisia mitatulle toimivuudelle. Jos CN105:n mittauskohtaista lähdetuoreutta ei voida todistaa, ominaisuus jätetään V1-toimituksesta pois. Shellyn mittauskohdetta ei saa nimittää pumpun kulutukseksi, jos mittaus koskee koko rakennusta. Ulkoisten hintojen ja sääpalvelujen kaupalliset oikeudet tarkistetaan ennen julkaisua.
- **L22–L25:** Lämpömalli/neuvonta kuuluvat V1.1:een, kirjoittava adapteri ja fysikaalinen säätö V2:een. Älä tuo niiden komentopolkuja V1:n toimitusbuildiin. **ESP32:n on edelleen toimittava ilman AI/LLM-riippuvuuksia**; deterministinen optimointi ja tutkimus saavat jatkua erillään.

### Laiterajat: pihamökki ja Core-kone

- **Pihamökissä oleva ensimmäinen ESP32-S3 N16R8:** jatkuvan kenttäseurannan kohde, RuuviTag sijaitsee mökissä. Ei flashausta, resettiä, NVS:n tyhjennystä, OTA-julkaisua, katkotestiä tai fyysistä ohjausta ilman erillistä laitekohtaista hyväksyntää. Älä tulkitse anturin etäisyyttä ohjelmistovirheeksi ilman näyttöä.
- **Core-koneeseen kytketty uusi ESP32-S3 N16R8:** erillinen regressiotestikortti (16 Mt Flash, 8 Mt PSRAM). Sen 64 KiB:n alkuperäinen flash-alkuosa on varmuuskopioitu. Tämä ei ole lupa kirjoittaa bootloaderia, otadataa, NVS:ää tai eFuseja. L05:n 100 keskeytystä **kolmella yksilöllä** ei täyty yhdellä kortilla.
- Älä koske muiden Enevina-tuotteiden tuotantoon, Home Assistantiin, CN105-siltaan, Shellyn releeseen tai Finance-koneen palveluihin ilman erillistä pyyntöä. V1:ssä ei C3-automaattiohjausta.

### Agentin käytännön työjärjestys ja PR-säännöt

1. Ennen muutosta lue AGENTS.md, README, kyseinen Lxx-rivi, nykyinen main, PR #5→#6→#7:n tuoreimmat HEAD:t, nykyiset testiraportit ja laitteeseen asennettu versio. **Älä aloita valmista työtä uudelleen.**
2. Tee ensin virhe- ja uhkamalli sekä testattava hyväksymiskriteeri. Suunnittele pienin muutos, jolla se täyttyy. Pidä fyysiset kirjoitukset pois päältä.
3. Tee yksi rajattu aihe ja erillinen haara/PR; suojaa pinottujen PR:ien base/ref. Erota firmwaremuutokset, valmistuskoodi ja puhdas dokumentaatio tarvittaessa.
4. Lisää oikea negatiivinen regressiotesti; erottele host/mock, C++ Debug/Release, Python, käännös, HIL, fyysinen kortti ja kenttä. Pelkkä tekstin etsiminen tai kääntyminen ei todista OTA-palautumista, turvallisuutta eikä säästöjä.
5. Älä flashaa pihamökin korttia. Uutta Core-testikorttia saa käyttää tuhoaviin testeihin vasta palautuskuvien ja nimenomaisen testiluvan jälkeen. Älä julkaise yhteisiä salaisuuksia tai työstä eFuseja ilman erillistä turvasuunnitelmaa.
6. Kirjaa commit-SHA, base/HEAD, app+bootloader-hash, testilokien polut, mitattu kesto, olosuhteet, tulokset, avoimet riskit ja päätös **PASS/FAIL/BLOCKED**. Pelkkä aiempi raportti ei oikeuta hyväksymään uutta versiota.
7. Jätä turvallisuutta koskeva PR draftiksi, kunnes riippumaton katselmointi ja taskin määritelty hyväksymisportti ovat läpäisseet. **Älä mergeä pinottuja PR #5–#7:ää automaattisesti**.
8. Julkaise asiakkaalle vasta kaupallisten **G0–G5**-päätösporttien hyväksymisen jälkeen. Agentti ei yksin julista CE-vaatimustenmukaisuutta, laiteyhteensopivuutta, säästöjä tai valmista tuotehyväksyntää.

**L01–L25 on tehtäväluettelo, ei lista jo hyväksytyistä tuloksista.** Toteutumistiedon lähde on aina todellinen commit, testiraportti ja hyväksytty porttipäätös.

---


Tämä tiedosto ohjaa kaikkia tässä repositoriossa työskenteleviä koodausagentteja (ChatGPT, Codex, Claude ja muut). Noudata sitä ennen suunnittelua, muutoksia, testejä ja Git-toimia. **README.md määrittelee tuotteen lähtötilanteen; tämän tiedoston rajaukset ja turvallisuusvaatimukset ovat sitovia.**

## 1. Tuotteen tehtävä ja ehdottomat rajaukset

**V2:n tavoitteena on yhden lämpöpumpun paikallinen optimointiohjain; kaupallinen V1 on yhden kohteen monitoring-only -valvontalaite.**

- **Tasan yksi lämpöpumppu per Lite-laite.** Älä lisää monen pumpun orkestrointia, keskitettyä ohjausta, energia-akkujen, aurinkosähkön, EV-latauksen tai koko talon EMS:n hallintaa.
- **Ei tekoälyä.** Älä lisää koneoppimista, neuroverkkoja, LLM:iä, AI-palveluita, mallien koulutusta tai AI-riippuvuuksia. Deterministinen rakennus- ja lämpöpumppufysiikan laskenta sekä matemaattinen optimointi ovat sallittuja.
- **Ei Linuxia, Raspberry Pi -vaatimusta, Dockeria, tietokantaa tai Home Assistant -riippuvuutta laitteeseen.** Myös internetin katkettua laitteen on kyettävä toimimaan turvallisesti paikallisesti.
- Alustava laitevalinta: **ESP32-S3 N16R8** (16 Mt Flash, 8 Mt PSRAM), **2,4 GHz Wi-Fi ensimmäisessä Mitsubishi-pilotissa**, Ethernet muissa laiteversioissa tarvittaessa. C++ ja ESP-IDF; laitekokoonpano on varmistettava.
- **Ensimmäinen todellinen pilotti: Mitsubishi Electric MSZ-FD25VA / MUZ-FD25VA, ilma–ilmapumppu**, ks. [pihamökin Mitsubishi-pilotti](docs/cottage-mitsubishi-fd25va.md). Panasonic/HeishaMon jää erilliseksi myöhemmäksi adapteriksi. CN105 on tarkistettava todellisesta pumpusta ennen komentoja.
- Erottele Lite Enevina Coresta, Enevina Adaptive Intelligencestä, Finance-koneen tehtävistä sekä olemassa olevista tuotantojärjestelmistä. Älä muuta niiden repositoryja tai tuotantokonfiguraatioita.

Jos ehdotus rikkoo näitä rajoja, älä toteuta sitä. Kerro ristiriita ja ehdota rajaukseen sopiva ratkaisu.

## 2. Haluttu käyttäjäkokemus (V1-valvonta, V1.1-neuvonta ja myöhemmän V2:n tavoitteet)

V1 lukee ja visualisoi, mutta alla kuvatut tulevat ohjauskyvykkyydet ovat V2-tavoitteita. Tuoteperheen pitkän aikavälin tavoite:
1. Lukemaan pumpun mittaukset, tilan ja sallitut ohjauskyvykkyydet.
2. Hakemaan ja välimuistittamaan 15 minuutin sähkönhinnat ja sääennusteen luotettavasta lähteestä; käsittelemään puuttuvat, muuttuneet ja vanhentuneet tiedot.
3. Laskemaan lämpöhäviön, sisälämpötilan kehityksen ja COP:n. Menoveden ja käyttöveden laskenta koskee vain niitä laitteita, joissa ne ovat olemassa; ensimmäinen ilma–ilmapumppu käyttää 16 °C HEAT / OFF -ohjausta.
4. Optimoimaan tilalämmityksen kustannukset turvallisesti; käyttövettä optimoidaan vain sitä tukeville pumpuille.
5. Näyttämään tila, ennuste, seuraava ohjaus, perustelut, viimeisin onnistunut päivitys ja varatilaan siirtymisen syy paikallisessa selainkäyttöliittymässä.
6. Säilyttämään asetukset turvallisesti ja jatkamaan toimintaa sähkö- tai verkkokatkon jälkeen.

Paikallinen käyttö ei saa edellyttää jatkuvaa pilviyhteyttä, tilausta tai ulkopuolista palvelinta. Ulkoiset hintojen ja sään tietolähteet ovat vapaaehtoisia optimointisyötteitä, eivät turvallisen perustoiminnan edellytyksiä.

## 3. Optimointimoottorin vaatimukset (V1.1-neuvonta ja V2; ei V1:n laitekirjoituksia)

- Toteuta **alustariippumaton, deterministinen C++-ydin**, jota voi ajaa samoilla syötteillä PC:llä ja ESP32:ssa.
- Käytä **15 minuutin aika-askelta**; tutki 24–48 tunnin ennustehorisontti (enintään 192 askelta). Älä kovakoodaa hintadatan vuorokausipituudeksi 96 jaksoa: kesä-/talviaikaan siirtyminen on käsiteltävä oikein.
- Käytä ennustemallina rakennuksen kevyttä lämpömallia ja lämpöpumpun tyypin mukaista teho- ja COP-mallia; käyttöveden energiatasetta vain niitä tukeville pumpuille. Parametrit tulee pystyä kalibroimaan ilman AI:ta; älä keksi pumpun kapasiteetti- tai hyötysuhdekäyriä todellisten tietojen tilalle tuotantotilassa.
- Minimoi **kokonaiskustannus**, ei pelkkää spot-hintaa: huomioi konfiguroitavat sähköenergian, siirron, verojen ja muiden relevanttien maksujen ehdot; vältä kaksoislaskentaa. Hintayksiköt, verollisuus ja aikaleimat on määriteltävä eksplisiittisesti.
- Optimointi huomioi sisälämpötilan mukavuusrajat, menoveden sallittujen muutosten rajat, käyttöveden tarpeen ja pumpun omat toimintarajoitukset. Mallinna kalliin esilämmityksen COP-haitta ja vältä tarpeettomia asetusarvopomppuja.
- Ensimmäinen toteutus: **rajattu dynaaminen ohjelmointi tai muu muisti- ja aikarajattu deterministinen haku**. Täysimittainen MILP-ratkaisija ei ole oletus ESP32:ssa. PC:n MILP saa olla **vain riippumaton vertailuratkaisu**.
- Uudelleenoptimoi lähtökohtaisesti 15 minuutin välein tai olennaisen tilamuutoksen vuoksi. Lähetä vain kuluvan ohjausjakson tarvittava asetusarvo, älä tulevien tuntien kokonaisohjauslistaa pumpulle.
- Optimoinnille asetetaan enimmäissuoritusaika ja muistibudjetti. Tavoite on alle **2 s** 48 h:n optimoinnille, mutta se on vasta suunnittelutavoite; älä ilmoita sitä mitatuksi.
- Optimointialgoritmin on voitava keskeytyä ja palauttaa turvallinen varatila ilman, että pumpun tiedonsiirto pysähtyy.

## 4. Kriittinen turvallisuus: turvallisuus ennen säästöä

**Oikeaa lämpöpumppua ei saa ohjata ennen erillistä, nimenomaista tuotantokäyttöhyväksyntää.** Aloita simulaatiosta ja read-only/shadow-tilasta.

- Älä katkaise lämpöpumpun sähkösyöttöä pörssihintojen perusteella; ohjaa ainoastaan rajapinnan sallimia asetusarvoja.
- Älä ohita jäätymisenestoa, kompressorin suojausta, sulatuksia, hälytyksiä, pumpun omia rajoituksia tai käyttövesijärjestelmän hygieniatoimintoja.
- Erota **PumpAdapter** (protokollat), **Supervisor** (turva- ja kirjoitusportti) ja **Optimizer** (ehdotukset). Optimizer ei saa koskaan lähettää suoria laitekomentoja.
- Supervisor tarkistaa kirjoitusoikeuden tilan, laitekyvykkyydet, lukemien tuoreuden, rajat, muutoksen nopeuden sekä ohjauskäskyn kuittauksen. Yksikään adapteri ei saa ohittaa tätä.
- Käynnistyksen oletustila on **read-only/shadow**. Kirjoitus sallitaan vasta erikseen hyväksytyssä laitekohtaisessa tilassa; ei hiljaista automaattista aktivointia firmware-päivityksen tai uudelleenkäynnistyksen jälkeen.
- Hintojen, sään, sisälämpötilan, mittausyhteyden, kellonajan, optimizerin tai watchdogin virheestä siirrytään määriteltyyn varatilaan. Vanhentuneeseen ennusteeseen perustuvaa optimointia ei saa jatkaa rajatta.
- Varatila: turvallinen ennalta määritelty / pumpun oma ulkolämpötilakompensoitu ohjaus, **vain jos sen palautustapa on kyseisen adapterin osalta todennettu**. Jos sitä ei voida turvallisesti vahvistaa, estä uudet kirjoitukset, näytä hälytys ja anna pumpun oman säätimen hoitaa turvatoiminnot.
- Ei oletuksia siitä, että tietyn rajapinnan write-komento on välitön tai onnistunut ilman kuittausta; vältä kirjoitusmyrskyä ja komennon jatkuvaa uudelleenlähetystä.

## 5. Koodiarkkitehtuuri

Pidä moduulirajat selkeinä; alla olevat ovat rajapintoja, eivät lupa lisätä raskasta kehystä:
- `pump/` — `PumpAdapter`, kyvykkyydet, telemetria, protokollakohtaiset toteutukset.
- `forecast/` — spot-hinnat, sää, aikaleimat, tuoreus, välimuisti.
- `physics/` — rakennuksen, lämmitysveden, lämpöpumpun ja käyttöveden mallit.
- `optimizer/` — puhdas laskenta ilman verkko- tai laiterajapintaa.
- `safety/` — Supervisor, tilakone, turvallisuusrajat, watchdog, fallback.
- `ui/` — kevyt paikallinen web-käyttöliittymä.
- `tests/` — PC-simulaatiot, yksikkötestit, regressiot ja adapterien mockit.

Tee ensin vain tarvittavat hakemistot ja rajapinnat. Koodi, mittayksiköt, versiot ja rajat dokumentoidaan. Pidä heap-allokoinnit, flash-kirjoitukset, verkkomuisti ja tehtävien kuorma hallinnassa. Älä anna verkkokutsun tai optimizerin lukita turvallisuustehtävää.

## 6. Historialliset tutkimusportit G0–G3 (kaupalliset G0–G5 määritellään erikseen)

Älä ilmoita säästöä, suorituskykyä tai laitetukea todennetuksi ilman mitattua tulosta ja toistettavaa testiaineistoa.

**G0: PC-laskennan oikeellisuus**
- Yksikkö- ja invarianttitestit: energia, lämpötila, teho, kWh/kW, COP, hinnan yksiköt, aikaleimat, käyttöveden energia.
- Kiinteäsiemeniset simulaatiot eri säille, hinnoille ja talojen parametreille.
- Vertaa tavalliseen lämmityskäyrään, yksinkertaiseen hintarajaan ja PC:llä laskettuun MILP-referenssiin **samoin alku-/lopputilan ja mukavuusrajoituksin**. Raportoi myös tilanteet, joissa Lite häviää.

**G1: ESP32-resurssit ja vakaus**
- Mittaa 96 ja 192 jakson optimizerin pahin suoritus-/keskimääräinen aika, huippumuisti, vapaa heap/PSRAM, tehtävien vasteajat sekä yhteyksien käyttö samanaikaisesti.
- Kuormita samanaikaisesti Ethernetiä, paikallista UI:ta, antureita ja pumppuyhteyttä. Testaa watchdog ja vähintään pitkäkestoinen vakausajo.
- Jos taloudellinen tulos tai vakaus kärsii liikaa, ehdota tehokkaampaa alustaa **mittausten perusteella**, ei oletuksella.

**G2: Laiteadapteri ja turvallisuus**
- Todista erikseen telemetrian tuoreus, virheelliset arvot, yhteyden katkeaminen, hidas/epäonnistunut kuittaus, sähkökatko, reboot, hinnan ja sään puuttuminen sekä kellon väärä aika.
- Varatila ja pumpun omat suojaus-/hygieniatoiminnot eivät saa rikkoutua. Tee ensin mockattu adapteri ja read-only/shadow laitetesti.

**G3: Tuotantokäyttö**
- Ei automaattista julkaisua tai ohjaustilan aktivointia. Käyttöönotto vaatii erillisen käyttäjän hyväksynnän, laitekohtaisen tarkistuslistan ja varatilan todentamisen.

## 7. Turvallisuus, ylläpito ja käyttöönotto

- Tunnukset, API-avaimet ja laitteiden osoitteet eivät kuulu Git-repositorioon. Tarvittavat asetukset tallennetaan turvallisesti; lokit eivät saa vuotaa salaisuuksia.
- Paikallinen hallinta on suojattava asianmukaisesti. Jos OTA toteutetaan, käytä firmware-allekirjoituksen tai muun todennetun alkuperän tarkistusta ja epäonnistuneen päivityksen rollback-mekanismia ennen tuotantokäyttöä.
- Käytä vakaita, ylläpidettyjä kirjastoja ja minimoi riippuvuudet. Kiinnitä ja dokumentoi versiot; älä aja automaattisia riippuvuuspäivityksiä tuotannossa.
- Älä muuta käyttäjän todellisia pumppuja, Home Assistantia, HeishaMonia tai muita järjestelmiä tämän repositoryn tehtävissä ilman erillistä nimenomaista pyyntöä.

## 8. Agentin työskentelytapa

1. Tarkista ensin nykyinen repository, README, tämä AGENTS.md, Git-tila ja olemassa olevat testit. **Älä aloita jo tehtyä työtä uudelleen.**
2. Toteuta annettu tehtävä pieninä, arvioitavina muutoksina. Erota laitekohtainen I/O puhtaasta laskennasta, jotta testit toimivat ilman rautaa.
3. Lisää regressiotestit korjattuihin virheisiin ja dokumentoi suunnittelupäätökset. Älä korvaa todellisia testejä näennäisillä mock-läpäisyillä.
4. Testaa relevantit osat, tarkasta diff ja varmista ettei mukaan tule salaisuuksia. Jos testiä ei voitu suorittaa, kerro syy; älä keksi tuloksia.
5. Käytä mielellään erillistä kehityshaaraa ja PR:ää laajemmissa muutoksissa; älä tee tarpeettomia uudelleenmuotoiluja tai muuta unrelated tiedostoja.
6. Raportoi lopuksi: muutetut tiedostot, commit/PR, suoritetut testit ja tulokset, tiedossa olevat riskit, seuraava hyväksymisportti.
7. **Älä julista prototyyppiä tuotantovalmiiksi** ilman G0–G3-portteja.

## 9. Historiallinen ensimmäinen toteutustehtävä (ei kaupallisen V1:n nykyinen työjärjestys)

Uusi ensisijainen kehitystehtävä on [pihamökin Mitsubishi Electric FD25VA -pilotti](docs/cottage-mitsubishi-fd25va.md): rakenna PC:llä ajettava, puhdas C++-malli ilma–ilmapumpun 16 °C HEAT / OFF -jaksojen 15 minuutin ennakointiin, turvallisuusrajoihin ja riippumattomaan referenssivertailuun. Hydronisen lämpöpumpun G0:n tulokset säilytetään erillään. Ensimmäisessä vaiheessa **ei kirjoituksia todelliseen pumppuun**, ei AI:ta eikä tarpeetonta käyttöliittymä- tai pilvirakennetta.

Päämittari: *todennettavasti alhaisemmat lämmityksen kokonaiskustannukset ilman mukavuus-, hygienia- tai turvallisuusrajojen rikkomista.*

## 10. Legacy Control — myöhempi V2-tutkimus, ei V1-toimitussisältö

**Ehdollinen V2-arkkitehtuuritutkimus (ei V1-ohjauslupaus):** Enevina Liten on tuettava myöhemmin laitekohtaisesti ulkolämpötilan signaalin simulointia ("outdoor temperature sensor emulation") silloin, kun suoraa ohjausta tai valmistajan dokumentoitua ulkoista ohjaustuloa ei ole. Tämä on saman yhden pumpun optimointituotteen vaihtoehtoinen adapteri, **ei toinen lämpöpumppu**. Toteutuksen tekniset ja turvallisuusehdot: [docs/legacy-outdoor-sensor-control.md](docs/legacy-outdoor-sensor-control.md).

Priorisoi ohjausreitit: (1) valmistajan dokumentoitu digitaalinen ohjaus, (2) valmistajan hyväksymä ulkoinen asetusarvo-/kuormitus-/lämmityskäyrätulo, (3) dokumentoitu ja todennettu säälle kompensoivan **erillisen** ulkoanturin passiivinen simulointi. Älä koskaan huijaa kylmäaine-, jäätymisenesto-, sulatus-, turva- tai muuta suojausanturia. Jos ulkoanturin turvallinen erillisyys ei ole osoitettavissa, emulointia ei saa sallia kyseisellä pumppumallilla.

- Toteuta anturisovitin erillisenä, pumpun mallin ja mitatun anturin resistanssi–lämpötila-taulukon / muun hyväksytyn sähköisen spesifikaation mukaan. **Älä käytä universaalia NTC-käyrää, vakioresistanssia, yleistä DAC-jännitettä tai oletettua pinoutia.**
- Suunnittele **virraton = alkuperäinen ulkoanturi** -palautus laitteistotasolle (esim. sopivasti mitoitettu normaalisti suljettu, galvaanisesti erotettu kaksinapainen rele), ei vain ohjelmalliseksi fallbackiksi. Älä kytke prototyyppiä todelliseen pumppuun ennen sähköisen turvallisuuden ja kytkennän erillistä hyväksyntää.
- Käytä todellista, riippumatonta ulkolämpötilamittausta rakennuksen lämpöhäviömallissa ja sääennusteessa. Pumpulle simuloitava ulkolämpötila on **vain ohjaussignaali**; nämä kaksi lämpötilaa eivät saa sekoittua.
- Muunna MPC:n tavoittelema menoveden asetusarvomuutos pumpun **todellisen lämmityskäyrän käänteisellä mallilla** rajatuksi näennäiseksi ulkolämpötilaksi. Älä oleta, että lämmitystehon komento vastaa suoraan anturisignaalia.
- Laitemallikohtaiset turvalliset rajat, muutosnopeus, komento-/anturiseuranta ja palautusehdot ovat pakollisia. Oletuksena emulointi on **pois käytöstä** ja vain read-only/shadow-testattavissa.
- Toteuta ensin pelkät simulaatio- ja yksikkötestit (käyrän inversio, rajat, anturikäyrä, vanhentunut oikea lämpötila, power-loss/rele-bypass, sensor fault, lämpötilamittausten sekoittuminen). Ei fyysistä anturi- tai relekytkentää ilman hyväksyttyä mallikohtaista suunnitelmaa.
- **Varmista pumpun muun toiminnan riippuvuus anturista:** lämmityskausi-/kesätila, lisälämmitys, sulatus, jäätymisenesto, hälytykset ja käyttövesiprioriteetti. Jos jotakin turvallisuuden kannalta kriittistä riippuvuutta ei voi rajata pois, älä ota huijausta käyttöön.


## 11. Pihamökin Mitsubishi Electric — ensisijainen kohde 16.11.2026

Tämä on **ensimmäinen todellinen kenttäpilotti**, joka korvaa aikaisemman Panasonic-pilotin. Kaupallisen V1:n ensisijainen toteutusjärjestys on kuitenkin **L01–L14**. Täsmävaatimukset, lähtötilan vahvistukset ja C0–C3-portit: [docs/cottage-mitsubishi-fd25va.md](docs/cottage-mitsubishi-fd25va.md).

- Vain **yksi** Mitsubishi Electric **MSZ-FD25VA + MUZ-FD25VA** (ilma–ilmapumppu). Ei käyttövesivaraajaa, menoveden säätöä tai shunttia.
- Pumpun alin lämmityspyynti **16 °C**. Mökin ylläpitotavoite **5 °C** ja ennakoiva turvamarginaali **8 °C**: ohjaus sallittujen lämmitys-/OFF-komentojen kautta, ei 5 °C:n asetusarvona. 5 °C:n suojaa ei saa väittää taatuksi ilman riippumatonta jäätymissuojausta.
- **Shelly Plus 1PM mittaa vain sähkönkulutusta.** Sen releellä ei koskaan katkaista lämpöpumpun syöttöä.
- Ensisijainen yhteys ESP32-S3:n paikallinen **Wi-Fi + CN105**, joka on validoitava mallikohtaisesti; alkuperäinen huoltomanuaali ei yksiselitteisesti näytä CN105:tä ja valmistajan yhteensopivuusluettelo vain tukee sen mahdollisuutta.
- Optimointi: **15 min aika-askel, 24–48 h**, mutta älä kytke kompressoria päälle/pois jokaisella aika-askeleella. Minimi käynti-/seisonta-ajat, hystereesi, mittauksen tuoreus, pumpun tilakuittaukset ja turvamarginaali ovat ensisijaisia.
- Ensin C0 offline-malli, C1 fyysisen rajapinnan vain luku -vahvistus, C2 shadow, C3 vasta erillisellä hyväksynnällä. **Älä flashaa tai ohjaa todellista laitetta tässä dokumentaatiovaiheessa.**
- Vanhojen vesikiertoisten pumppujen Legacy Outdoor Sensor Control pysyy erillisenä myöhempänä sovittimena, ei tämän Mitsun anturihuijauksena.
