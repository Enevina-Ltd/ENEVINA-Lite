# AGENTS.md — Enevina Lite

Tämä tiedosto ohjaa kaikkia tässä repositoriossa työskenteleviä koodausagentteja (ChatGPT, Codex, Claude ja muut). Noudata sitä ennen suunnittelua, muutoksia, testejä ja Git-toimia. **README.md määrittelee tuotteen lähtötilanteen; tämän tiedoston rajaukset ja turvallisuusvaatimukset ovat sitovia.**

## 1. Tuotteen tehtävä ja ehdottomat rajaukset

**Enevina Lite on yhden lämpöpumpun itsenäinen, edullinen, paikallinen optimointiohjain.**

- **Tasan yksi lämpöpumppu per Lite-laite.** Älä lisää monen pumpun orkestrointia, keskitettyä ohjausta, energia-akkujen, aurinkosähkön, EV-latauksen tai koko talon EMS:n hallintaa.
- **Ei tekoälyä.** Älä lisää koneoppimista, neuroverkkoja, LLM:iä, AI-palveluita, mallien koulutusta tai AI-riippuvuuksia. Deterministinen rakennus- ja lämpöpumppufysiikan laskenta sekä matemaattinen optimointi ovat sallittuja.
- **Ei Linuxia, Raspberry Pi -vaatimusta, Dockeria, tietokantaa tai Home Assistant -riippuvuutta laitteeseen.** Myös internetin katkettua laitteen on kyettävä toimimaan turvallisesti paikallisesti.
- Alustava laitevalinta: **ESP32-S3 N16R8** (16 Mt Flash, 8 Mt PSRAM), **Ethernet** sopivalla erillisellä Ethernet-ohjaimella. C++ ja ESP-IDF. Tämä on **testattava ehdokas, ei vielä todistetusti riittävä tuotantoalusta**.
- Ensimmäinen lämpöpumppusovitin: **Panasonic Aquarea / HeishaMon**. Muut merkit vasta myöhemmissä erillisissä sovittimissa. Varmista käytettävän HeishaMon-version todelliset rajapinnat ja kyvykkyydet ennen toteutusta; älä oleta, että erillinen MQTT-brokeri tai Home Assistant on saatavilla.
- Erottele Lite Enevina Coresta, Enevina Adaptive Intelligencestä, Finance-koneen tehtävistä sekä olemassa olevista tuotantojärjestelmistä. Älä muuta niiden repositoryja tai tuotantokonfiguraatioita.

Jos ehdotus rikkoo näitä rajoja, älä toteuta sitä. Kerro ristiriita ja ehdota rajaukseen sopiva ratkaisu.

## 2. Haluttu käyttäjäkokemus

Laitteen pitää pystyä itsenäisesti:
1. Lukemaan pumpun mittaukset, tilan ja sallitut ohjauskyvykkyydet.
2. Hakemaan ja välimuistittamaan 15 minuutin sähkönhinnat ja sääennusteen luotettavasta lähteestä; käsittelemään puuttuvat, muuttuneet ja vanhentuneet tiedot.
3. Laskemaan lämpöhäviön, sisälämpötilan kehityksen, menoveden tavoitteen, COP-arvion ja käyttöveden lämmitystarpeen.
4. Optimoimaan tilalämmityksen ja käyttöveden kustannukset **yhdessä** niiden sallittujen rajojen puitteissa.
5. Näyttämään tila, ennuste, seuraava ohjaus, perustelut, viimeisin onnistunut päivitys ja varatilaan siirtymisen syy paikallisessa selainkäyttöliittymässä.
6. Säilyttämään asetukset turvallisesti ja jatkamaan toimintaa sähkö- tai verkkokatkon jälkeen.

Paikallinen käyttö ei saa edellyttää jatkuvaa pilviyhteyttä, tilausta tai ulkopuolista palvelinta. Ulkoiset hintojen ja sään tietolähteet ovat vapaaehtoisia optimointisyötteitä, eivät turvallisen perustoiminnan edellytyksiä.

## 3. Optimointimoottorin vaatimukset

- Toteuta **alustariippumaton, deterministinen C++-ydin**, jota voi ajaa samoilla syötteillä PC:llä ja ESP32:ssa.
- Käytä **15 minuutin aika-askelta**; tutki 24–48 tunnin ennustehorisontti (enintään 192 askelta). Älä kovakoodaa hintadatan vuorokausipituudeksi 96 jaksoa: kesä-/talviaikaan siirtyminen on käsiteltävä oikein.
- Käytä ennustemallina ainakin rakennuksen kevyttä lämpömallia, lämpöpumpun tehon ja COP:n riippuvuutta olosuhteista sekä käyttöveden energiatasetta. Parametrit tulee pystyä kalibroimaan ilman AI:ta; älä keksi pumpun kapasiteetti- tai hyötysuhdekäyriä todellisten tietojen tilalle tuotantotilassa.
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

## 6. Todentaminen ja hyväksyntäportit

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

## 9. Ensimmäinen toteutustehtävä

Aloita **G0-työstä**: rakenna PC:llä ajettava, puhdas C++-malli yhden pumpun lämmityksen ja käyttöveden 15 minuutin ennakoivaan optimointiin, turvallisuusrajoihin ja riippumattomaan referenssivertailuun. Ensimmäisessä vaiheessa **ei kirjoituksia todelliseen pumppuun**, ei AI:ta eikä tarpeetonta käyttöliittymä- tai pilvirakennetta.

Päämittari: *todennettavasti alhaisemmat lämmityksen kokonaiskustannukset ilman mukavuus-, hygienia- tai turvallisuusrajojen rikkomista.*

## 10. Legacy Control — vanhojen lämpöpumppujen ohjaus ilman digitaalista rajapintaa

**Pakollinen V1-arkkitehtuurivaatimus:** Enevina Liten on tuettava myöhemmin laitekohtaisesti ulkolämpötilan signaalin simulointia ("outdoor temperature sensor emulation") silloin, kun suoraa ohjausta tai valmistajan dokumentoitua ulkoista ohjaustuloa ei ole. Tämä on saman yhden pumpun optimointituotteen vaihtoehtoinen adapteri, **ei toinen lämpöpumppu**. Toteutuksen tekniset ja turvallisuusehdot: [docs/legacy-outdoor-sensor-control.md](docs/legacy-outdoor-sensor-control.md).

Priorisoi ohjausreitit: (1) valmistajan dokumentoitu digitaalinen ohjaus, (2) valmistajan hyväksymä ulkoinen asetusarvo-/kuormitus-/lämmityskäyrätulo, (3) dokumentoitu ja todennettu säälle kompensoivan **erillisen** ulkoanturin passiivinen simulointi. Älä koskaan huijaa kylmäaine-, jäätymisenesto-, sulatus-, turva- tai muuta suojausanturia. Jos ulkoanturin turvallinen erillisyys ei ole osoitettavissa, emulointia ei saa sallia kyseisellä pumppumallilla.

- Toteuta anturisovitin erillisenä, pumpun mallin ja mitatun anturin resistanssi–lämpötila-taulukon / muun hyväksytyn sähköisen spesifikaation mukaan. **Älä käytä universaalia NTC-käyrää, vakioresistanssia, yleistä DAC-jännitettä tai oletettua pinoutia.**
- Suunnittele **virraton = alkuperäinen ulkoanturi** -palautus laitteistotasolle (esim. sopivasti mitoitettu normaalisti suljettu, galvaanisesti erotettu kaksinapainen rele), ei vain ohjelmalliseksi fallbackiksi. Älä kytke prototyyppiä todelliseen pumppuun ennen sähköisen turvallisuuden ja kytkennän erillistä hyväksyntää.
- Käytä todellista, riippumatonta ulkolämpötilamittausta rakennuksen lämpöhäviömallissa ja sääennusteessa. Pumpulle simuloitava ulkolämpötila on **vain ohjaussignaali**; nämä kaksi lämpötilaa eivät saa sekoittua.
- Muunna MPC:n tavoittelema menoveden asetusarvomuutos pumpun **todellisen lämmityskäyrän käänteisellä mallilla** rajatuksi näennäiseksi ulkolämpötilaksi. Älä oleta, että lämmitystehon komento vastaa suoraan anturisignaalia.
- Laitemallikohtaiset turvalliset rajat, muutosnopeus, komento-/anturiseuranta ja palautusehdot ovat pakollisia. Oletuksena emulointi on **pois käytöstä** ja vain read-only/shadow-testattavissa.
- Toteuta ensin pelkät simulaatio- ja yksikkötestit (käyrän inversio, rajat, anturikäyrä, vanhentunut oikea lämpötila, power-loss/rele-bypass, sensor fault, lämpötilamittausten sekoittuminen). Ei fyysistä anturi- tai relekytkentää ilman hyväksyttyä mallikohtaista suunnitelmaa.
- **Varmista pumpun muun toiminnan riippuvuus anturista:** lämmityskausi-/kesätila, lisälämmitys, sulatus, jäätymisenesto, hälytykset ja käyttövesiprioriteetti. Jos jotakin turvallisuuden kannalta kriittistä riippuvuutta ei voi rajata pois, älä ota huijausta käyttöön.

