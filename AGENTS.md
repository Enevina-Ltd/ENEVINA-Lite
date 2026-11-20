# AGENTS.md — Enevina Lite

## PYSYVÄ TUOTEPÄÄTÖS: JULKINEN JA LÄHDEKOODILTAAN AVOIMESTI NÄHTÄVÄ ENEVINA LITE (18.11.2026)

**Enevina Lite on Enevina Oy:n julkinen, avoimesti luettavalla lähdekoodilla kehitettävä tuote.** Repository `https://github.com/Enevina-Ltd/ENEVINA-Lite` on julkinen. Kehitys, dokumentaatio, katselmointi ja yhteisöosallistuminen suunnitellaan tästä lähtien julkisen projektin vaatimusten mukaan; kyse ei ole salaisesta vain sisäiseen käyttöön tarkoitetusta kokeilusta. **Tämä päätös koskee kaikkia tämän repositoryn kehitysagentteja.**

**Kielikohtainen yritysnimi (käyttäjän päätös 18.11.2026):** Käytä kaikessa englanninkielisessä tuotteen esittelyssä, README-tekstissä, sponsoriprofiilissa, asiakasviestinnässä ja markkinoinnissa nimeä **Enevina Ltd**. Käytä suomeksi nimeä **Enevina Oy**. Virallisissa oikeudellisissa yhteyksissä, tekijänoikeusilmoituksissa, yhtiön rekisteröinti-, pankki-, vero- ja sopimusasiakirjoissa käytetään **rekisteröityä oikeushenkilön nimeä Enevina Oy** (tarvittaessa englanniksi ”Enevina Ltd (registered in Finland as Enevina Oy)”). Älä muuta `LICENSE`-tiedoston lisenssinantajaa tai väitä Enevina Ltd:tä erilliseksi rekisteröidyksi yhtiöksi ilman varmennusta.

**Lisenssin täsmällinen merkitys:** tällä hetkellä lähdekoodi on **source-available / noncommercial**, ei Open Source Initiativen määritelmän mukaisesti avoimen lähdekoodin lisenssillä julkaistu. Voimassa oleva `LICENSE` sallii ei-kaupallisen käytön ja jakamisen ehtojensa mukaisesti, mutta ohjelmiston, firmwaren tai niitä sisältävien ohjainten kaupallinen myynti edellyttää Enevina Oy:n erillistä kirjallista lupaa. Agentti ei saa väittää lisenssiä MIT-, Apache-, GPL- tai OSI-avoimeksi eikä poistaa kaupallisen käytön rajoituksia ilman käyttäjän nimenomaista lisenssipäätöstä. Kolmannen osapuolen kirjastot säilyttävät omat ehtonsa; katso `THIRD_PARTY_NOTICES.md`.

**Julkisen tuotekehityksen pakolliset toimintatavat:**

1. **Julkinen sisältö on oletus.** Jokainen commit, PR, issue, keskustelu, loki, kuva, testi, julkaisu ja Git-historia voidaan lukea myös ulkopuolelta. Älä julkaise salasanoja, avaimia, tokeneita, kotiverkon tunnisteita, tarkkoja yksityisen testikohteen tietoja, yksilöiviä MAC-osoitteita tai raakoja laitelokeja. Testaa julkaisukelpoisuus ennen pushia ja huomioi, että tiedoston poistaminen uudesta commitista ei puhdista historiaa.
2. **Esittele yleinen tuote, älä yksittäistä asennuskohdetta.** Julkinen README, GitHub About, dashboard, kuvamateriaali ja dokumentaation yleiset osiot kuvaavat monimerkkistä yhden lämpöpumpun paikallista ohjainta. Kenttätestien tunnistetiedot eivät kuulu esittelyyn; vanhoja yksityisiä mitta- tai rakennustietoja ei palauteta julkisiin tiedostoihin.
3. **Kansainvälinen löydettävyys.** Pidä README:n suomen- ja englanninkielinen perusesittely, `docs/GETTING_STARTED.md`, `docs/COMPATIBILITY.md` ja GitHubin julkiset linkit käyttökelpoisina. Kuvaile ominaisuuksien todellinen tila (valmis / host-testattu / kokeellinen / suunniteltu). GitHub Discussions on yleisiin kysymyksiin ja Issues toistettaviin virheisiin.
4. **Todisteet ennen markkinointiväitteitä.** Älä väitä valmiita valmistajaintegraatioita, fyysistä turvallisuutta, tuotanto- tai CE-valmiutta, kentällä todistettuja säästöjä tai toimivaa OTA-päivitystä ilman vastaavaa mitattua näyttöä. Älä esitä generoituja dashboard-malleja oikeina kuvakaappauksina. Merkitse simulaatiot simulaatioiksi ja yksilöi todellinen laiteversio, kun se on turvallista julkaista.
5. **Yhteisö ja omistusoikeudet.** Ota vastaan virheraportteja ja ehdotuksia, mutta älä yhdistä kolmannen osapuolen koodia, grafiikkaa, dataa tai firmwarea ilman oikeuksien ja lisenssien tarkistamista. Noudata `CONTRIBUTING.md`-ohjetta sekä Enevina Oy:n kaupallista lisensointia varten tarvittavaa erillistä oikeussopimusta.
6. **Julkaisut ja turvallisuus.** Käytä kehitysvaiheessa alpha-/pre-release-merkintöjä, kun valmista asennettavaa firmwarea ei ole todennettu. GitHubin lähdekoodin julkisuus **ei** ole lupa flashata tai ohjata oikeaa lämpöpumppua eikä syrjäytä tämän tiedoston PR-, laiteturva- ja testausportteja. Älä muuta yksityisten tietojen puhdistamiseksi julkaistujen tagien tai haarojen historiaa force-pushilla ilman erillistä hyväksyntää ja palautussuunnitelmaa.

---

## AJANTASAINEN KEHITYSTILANNE – 19.11.2026 (TARKISTA AINA GITHUBISTA)

**Tämä osio ohjaa aktiivista työtä; alempana olevat Mitsubishi-pilottia, F01–F09-jonoa ja G-portteja koskevat osiot sisältävät myös historiallista suunnittelua.** Uusimman käyttäjäpäätöksen ja GitHubin todellisen main/PR-tilanteen on ohjattava toteutusta. Älä tulkitse päivämäärällä varustettua tilannekuvaa pysyväksi valmistumisilmoitukseksi.

- **Toteutettu lähdekoodiin aiemmin:** Midea V2/V3:n paikalliset lukuadapterit ja kokeellinen manuaaliohjaus on yhdistetty (#48, #51, #54). Tuotantokelpoisuutta, todellista Lite→Midea-ohjausta tai optimoinnin automaattista kirjoitusta ei ole vielä osoitettu fyysisillä laitetesteillä. Mitsubishi CN105 on erillinen pilottireitti, ei kaikkien pumppujen pakollinen tekniikka.
- **Avoin pinottu kehitysketju tarkistushetkellä:** #55 monimaan sähkömarkkina-alueet, valuutta/tariffit ja kaupunkihaku → #56 käyttöönottovelho → #57 elävän hintasäätimen optimointiprofiilit → #58 RuuviTag-/Shelly-haku → #59 suunnitelman hintaluokka-aikajana. Kaikki olivat draft-tilassa 19.11.2026 ja Codexin löydöksiä oli auki. Tarkista ennen muokkauksia, etteivät PR:t ole jo muuttuneet tai yhdistyneet.
- **Ensisijainen seuraava työ:** korjaa PR #55–#59:n todelliset Codex-havainnot ja lisää niitä toistavat regressiot; korjaa alimman yhteisen haara-/rakennetason virhe siellä missä se syntyy ja vie korjaus pinottuihin haaroihin. Päivitä katselmoinnit muuttuneelle head-SHA:lle ja yhdistä hyväksytyt PR:t riippuvuusjärjestyksessä. Älä jatka loputonta draft-PR-pinoa korjausten kustannuksella.
- **Seuraava toiminnallinen tavoite:** yhdistä valittu sijainti, markkina-alue, oikeat hintayksiköt, sääennuste, käyttäjän mukavuusrajat ja valittu optimointiprofiili yhteen todelliseen optimointisuunnitelmaan. Aikajana ei yksin ole fysiikkapohjainen ennuste. Sisälämpötila-, energiankulutus-, kustannus- tai säästöennustetta ei saa piirtää keksityillä arvoilla: käytä olemassa olevaa todistettavaa mallia ja merkitse tuntematon tieto puuttuvaksi.
- **Valmis tuotekokemus:** ensin salasanaton Wi-Fi-verkkoon liittyminen, sitten selkeä kieli/maa/markkina-alue/kaupunki/sää/sähkösopimus/pumppu/anturit/mittari/lämpötilarajat/optimointiprofiili -käyttöönotto; mobiiliystävällinen premium-dashboard ja automaattinen yötila, kaikki asetukset myöhemmin muokattavissa, dashboardista täysi OTA-päivityspolku. Yhtään aiemmin toimivaa ominaisuutta ei poisteta tuotteen yksinkertaistamisen nimissä.
- **Automaattihaku on todellinen toiminto, ei koriste:** valittava yksi paikallisesti yhteensopiva lämpöpumppu, RuuviTag BLE -hakulista ja sensorien roolit, Shelly-haku ja mittarin kohteen vahvistus; manuaalinen osoite/asetus sallittu, kun automaattista tunnistusta ei ole. Lisäanturi tai Shelly ei ole pakollinen Liten käyttöönotolle.
- **Sään ja hinnan alueellisuus:** käyttäjän valitsema kaupunki/koordinaatit ohjaavat sääpalvelun pyyntöjä; nykyinen kansainvälinen sääsuunta on MET Norway, FMI voi olla todennettu vaihtoehto/fallback, ei pakollinen Suomi-lukitus. Valittu sähkömarkkina-alue ja sen valuutta, hinta-/veroyksikkö, aikavyöhyke sekä 15 min / DST -jaksot kulkevat palvelusta optimointiin ja dashboardiin saakka. Älä väitä fallbackia toimivaksi ennen toteutusta ja testiä.
- **Todistusasteet:** host-Debug/Release-, Node-, selain-simulaatio- ja firmware-build ovat eri näyttöä kuin flashattu ESP32-S3 N16R8, puhelinselain, fyysinen Ruuvi/Shelly tai oikea pumppuyhteys. Merkitse kukin erikseen. Fyysinen flash/NVS-muutos ja pumppukomennot noudattavat jäljempänä olevia lupaehtoja.

### Pakolliset regressiot nykyiselle PR-ketjulle

1. **Asetusten säilyminen:** NVS/site-skeeman jokainen muutos tarvitsee versionoidun taaksepäin yhteensopivan migraation (erityisesti aiempi `site/v2`), eheys- ja sähkökatkotestit. Päivitys ei saa kadottaa Wi-Fi:tä, pumppuyhteyttä, kaupunkia, tariffia eikä lämpötilarajoja.
2. **Hintavälimuisti ja valuutta:** uudelleenkäynnistys ei saa pyyhkiä saman alueen ehjää hintavälimuistia; alueen vaihto ei saa näyttää edellisen alueen hintoja. UI, laskenta ja API käyttävät samaa valittua valuuttaa/yksikköä. Maan vaihto mitätöi edellisen maan syötetyn tariffiluonnoksen.
3. **Ohjaus ja tuoreus:** käyttöliittymän terve yhteys vaatii ajantasaisen, oikeasta lähteestä vahvistetun tiedon; aiemmin saatu SSE-viesti ei riitä. Kaikkien sallittujen optimointiprofiilien parametrien on oltava myös elävän ohjaimen hyväksymällä alueella; mikään sallittu asetus ei saa hiljaisesti estää lämmitystä.
4. **Käyttöönotto ja laitehaku:** Ohita ei tallenna hylättyjä luonnosasetuksia; Shelly-haun valmistunut tulos pysyy saatavilla riittävän pitkään; RuuviTagin viimeksi kuultu mainos ja hyväksytyn mittauksen tuoreus ovat eri asioita.
5. **Suunnitelman rehellisyys:** ilman lämmitystavoitetta ei esitetä lämmitysohjeita; puuttuvan sääjakson yli ei piirretä yhtenäistä ennusteviivaa. Testaa myös offline, aikavyöhykkeen ja DST:n vaihdot, asetusten reboot sekä puuttuvat sensorit.

Nämä ovat konkreettisia toiminnallisia regressioita, **eivät** syy löysentää `AGENTS.md`:n turvallisuusvaatimuksia tai hylätä Codex-löydöksiä viittauksen perusteella. Arvioi havainnot koodin ja toistettavan testin perusteella; dokumentoi teknisesti väärät positiiviset löydökset erikseen.

---

## PAKOLLINEN GITHUB-PR-KATSELMOINTI (17.11.2026 alkaen)

**Järjestys on aina oma katselmointi → omat korjaukset ja testit → Codex-katselmointi → Codex-havaintojen korjaukset ja tarvittaessa uusi Codex-katselmointi → lopullinen hyväksyntä → merge.** Älä muuta tätä järjestystä oikoteillä.

1. **Oma katselmointi ensin.** Lue PR:n todellinen head/base ja koko diff, riippuvuudet, testit sekä aiemmat katselmoinnit. Kirjaa konkreettiset P0/P1/P2-havainnot GitHub-reviewksi ennen Codex-kutsua. Tarkista ainakin mittaustiedon alkuperä ja tuoreus, reset- ja muistirajat, OTA/A-B-osioiden sopivuus sekä ohjausrajapinnan riskit silloin, kun muutokset koskevat niitä. Älä päättele fyysistä PASSia host-/käännöstestistä.
2. **Korjaa itse löydetyt virheet** pienimmällä perustellulla muutoksella ja lisää negatiivinen regressiotesti. Aja soveltuvat Debug/Release, Python ja firmwarebuildit. Kirjaa SHA:t, testitulokset ja keskeneräiset fyysiset kokeet. Älä lisää turhia watchdogeja tai poista ohjauksen kehitystä.
3. **Pyydä Codex vasta oman katselmoinnin ja korjausten jälkeen** PR:n GitHub-keskustelussa täsmällisesti viestillä `@codex review`. Tämä on pyyntö, ei hyväksyntä. Varmista GitHubista, että Codex todella julkaisi katselmointinsa. Pelkkä @-maininta, emoji, kommentti tai CI-build ei korvaa katselmointia.
4. **Korjaa Codexin perustellut P0/P1/P2-havainnot**, lisää regressiot, tarkista korjaus itse ja aja testit uudelleen. Jos korjaukset muuttavat PR:n head-SHA:ta, pyydä Codexia tarkistamaan uusin versio uudestaan. Älä ohita havaintoa ilman dokumentoitua teknistä perustelua.
5. **Merge on kielletty**, kun uusimmasta muutossarjasta puuttuu Codex-katselmointi, olennaisia havaintoja on auki, soveltuvat regressiot ovat punaisina, todellista laitehyväksyntää väitetään ilman näyttöä tai käyttäjä ei ole hyväksynyt käyttöönottoa. Pidä PR luonnoksena tai merkitse BLOCKED. Älä automergeä pinottuja PR:iä.
6. **Oma GitHub-review ja Codex ovat erillisiä.** PR:n tekijä ei voi GitHubissa hyväksyä omaa PR:äänsä virallisena riippumattomana hyväksyntänä; oma COMMENT-review on auditointinäyttö. Lopullinen hyväksyminen tehdään GitHub-oikeuksien ja mahdollisen branch protection -säännöstön mukaisesti.

**Huom:** `AGENTS.md` ohjaa agentteja mutta ei teknisesti estä GitHubin merge-painiketta. Kovaan pakotukseen vaaditaan repositoryn branch protection / ruleset ja tarvittavat required review-/status check -ehdot. Älä koskaan väitä näitä asetetuksi ilman GitHubista vahvistettua näyttöä.

### Code Review Rules

- **Lähdetuoreus:** SSE/HTTP-viestin vastaanotto tai syötteen kasvava laskuri ei yksin todista ajantasaista pumppumittausta. Lähteen todistettu aika ja vastaanottoaika verrataan oikeaan nykyhetkeen; replay, counter reset ja lähdekellon virheet eivät saa hyväksyä vanhaa arvoa.
- **Laiteturvallisuus:** Fyysiset pumpun komennot vaativat laitekohtaisen hyväksytyn koesuunnitelman ja Supervisorin rajat; älä tulkitse pelkkää adapterin kyvykkyysbittiä kirjoitusluvaksi. Host/mock-testit eivät oikeuta fyysistä käyttöönottoa.
- **Ei turhia vahtimekanismeja:** Korjaa resetin tai jumittumisen juurisyy; älä ehdota uusia watchdogeja, automaattisia restart-silmukoita tai ohjaustoimintojen poistamista ilman todistettua tarvetta.

---


## 0. KEHITYSPÄÄTÖS: ENEVINA LITE MONIMERKKISENÄ OHJAIMENA ENSIN TOIMIVAKSI (16.11.2026)

**18.11.2026 – KEHITYSVAIHEEN EHDOTON SALASANAKIELTO (käyttäjän päätös):** Älä lisää Liteen uusia salasanoja, PIN-koodeja, kirjautumispakkoa, laitekohtaisia WPA2-asennusavaimia, käyttäjätilejä tai näihin verrattavia uusia autentikointivaatimuksia. Wi-Fi-asennusverkko on tarkoituksella avoin ja salasanaton. Kotiverkkoon yhdistäminen käyttää vain kyseisen olemassa olevan Wi-Fi-verkon tavanomaisia tunnuksia; tämä ei ole Liten uusi kirjautumisvaatimus. Säilytä jo olemassa oleva CSRF-asetustoken muuttamatta sitä uudeksi kirjautumisportiksi. Asennusverkon `/setup` ja `/api/wifi/*` sallitaan edelleen, mutta pumpun ohjaus ja vaiheen 2 asetukset eivät saa olla käytettävissä asennusverkon kautta. Tuotteen autentikointia käsitellään vasta valmistumisen yhteydessä ja omistajan erillisellä hyväksynnällä. Agentti ei saa ohittaa kieltoa tietoturvahavainnon perusteella, vaan ratkaisee kehitysvaiheen virheet ilman salasanoja.

**Käyttäjän uusin nimenomainen päätös on etusijalla:** Enevina Lite kehitetään monimerkkiseksi, yhden lämpöpumpun paikalliseksi energianhallinta- ja optimointituotteeksi. Mitsubishi Electric on aiemmin valittu kenttäpilotti ja Midea V2/V3 on aktiivinen paikallisen Wi-Fi-yhteensopivuuden kehityskohde; kumpikaan ei rajaa yleistä arkkitehtuuria tai käyttöliittymää. Älä lukitse V1:tä monitoring-only-tilaan, poista ohjauspolkuja, kiellä optimoinnin kehitystä tai siirrä niitä automaattisesti V2:een. Kaupallistamisen ominaisuusrajat päätetään myöhemmin mitatun toimintakyvyn perusteella.

**Ei uusia turhia watchdogeja, watchdog-kerroksia, health-watcher-silmukoita, reset-ajastimia eikä automaattisia käynnistysketjuja.** Alkuperäinen Enevina Core kärsi ylenpalttisesta valvonta- ja resetlogiikasta. Selvitä juurisyy, mittaa ongelma ja korjaa koodi. Älä lisää uutta restart-mekanismia vain varmuuden vuoksi. Säilytä jo käytössä olevat perustellut ESP-IDF/Arduino-suojat; niiden korvaaminen tai muuttaminen vaatii erillisen todistettuun vikaan perustuvan päätöksen.

### Ensisijainen toimivuusjono F01–F09

1. **F01:** ESP32:n käynnistyminen, Wi-Fi, paikallinen UI ja HTTP/API luotettavasti.
2. **F02:** Kohteen RuuviTagin BLE-lämpötila, kosteus ja tiedon todellinen tuoreus.
3. **F03:** Valitun pumpun (esim. Mitsubishi CN105 tai Midea V2/V3) todellinen paikallinen mittaustieto ja lähdekohtainen tuoreus; SSE-heartbeat tai yksittäinen vastaanotettu paketti ei riitä näytöksi.
4. **F04:** Shellyn sähkönkulutuksen luku ja oikea mittauskohde. Älä ohjaa pumpun sähkösyöttöä Shellyn releellä.
5. **F05:** Valitun maan ja sähkömarkkina-alueen 15 minuutin hinnat, paikallisen kaupungin sääennuste (MET Norway / toteutettu vaihtoehto), valuutat, aikavyöhykkeet, yksiköt, välimuistit ja katkojen käsittely.
6. **F06:** Yhtenäinen mobiiliystävällinen käyttöönottovelho ja premium-dashboard, historia, hälytykset, kieli-/maa-/kaupunkivalinnat, optimointisuunnitelma ja selkeä mittausten tila.
7. **F07:** Rakennusfysiikka ja deterministinen MPC oikeilla, turvallisesti luetuilla mittauksilla; tulokset testataan riippumatonta referenssiä vasten.
8. **F08:** Ohjausketjun, turvallisuusrajojen, Midea V2/V3:n ja Mitsubishi CN105:n sekä muiden mallikohtaisten adapterien kuittausten kehitys jatkuu host/mock-/testipenkissä. **Älä poista ohjauskoodia**; oikean pumpun ensimmäiset kirjoitukset edellyttävät erikseen hyväksyttyä mallikohtaista koetta.
9. **F09:** Säilytä toimiva allekirjoitettu OTA ja A/B-rollback, korjaa osoitetut viat. Älä luo lisävalvontasysteemiä toimivan päivityksen tilalle.

**Toimivuus ensin, tuotantoportit sitten.** [Kaupallistamisen työlista L01–L25](docs/product/COMMERCIAL_V1_BACKLOG.md) ja G0–G5 ovat kaupallisen julkaisemisen työjono, eivät kielto toteuttaa tai testata ominaisuuksia nyt. L01 tarkoittaa toiminnallisen baseline-version todennusta, ei ohjauskoodin poistamista. L09 tarkoittaa offline-/NVS-ongelmien korjaamista ilman uusia watchdogeja.

**Fyysinen turvallisuus säilyy:** mock-, simulaatio- ja testipenkkikokeet ovat sallittuja, mutta kenttätestiin asennettua ESP32-korttia ei flashata tai resetoida ilman erillistä lupaa eikä oikealle pumpulle lähetetä uusia käskyjä ilman laitekohtaista hyväksyttyä testaussuunnitelmaa. Core-koneen uusi ESP32-S3 N16R8 on regressiotestilaite, ei automaattisesti lupa pyyhkiä sen NVS:ää tai bootloaderia. Älä muuta Home Assistantia, Shelly-relettä tai muuta tuotantoa.

**Työskentely:** lue nykyinen main ja tämänhetkinen GitHubin avoin PR-ketju (19.11.2026 tilanteessa #55–#59), tarkista mitä on jo tehty ja mitä katselmoinneissa on auki, korjaa todellinen ongelma pienellä muutoksella, testaa, kirjaa commit/testinäyttö ja jatka. Älä kehitä tarpeettomia uusia sääntökerroksia tai merkitse simulaatiota fyysiseksi testiksi. Jo toimivat OTA-/turvarajat ja salaisuuksien suojaus säilyvät.

---

## 1. Tuotteen tehtävä ja ehdottomat rajaukset

**Enevina Lite on monimerkkinen, yhden lämpöpumpun paikallinen energianhallinta- ja optimointiohjain, josta kehitetään Enevina Oy:n ensimmäinen kaupallinen tuote. Kehitystä ei lukita monitoring-onlyksi.**

**Adapterisopimus:** Yhteinen fysiikka, optimointi, hinnat, sää, turvaportti, laatu-/tuoreustiedot, käyttöliittymä ja OTA eivät saa riippua yhden valmistajan protokollasta. Mallikohtaiset kyvykkyydet, rajat, kuittaukset, kompressori-/sulatustiedot ja varatila kuuluvat adapteriin. Mallit, joissa lähdettä ei voida todentaa, eivät saa väittää vastaanottoaikaa alkuperäiseksi pumppumittaukseksi. Adapterit voivat hyödyntää paikallista langatonta yhteyttä tai erillistä langatonta yhdyskäytävää; tekninen ja turvallinen yhteensopivuus todistetaan mallikohtaisesti. Työn alla ovat Midea V2/V3:n paikallinen Wi-Fi ja Mitsubishi CN105; mahdollisia muita sovittimia ovat Panasonic/HeishaMon ja NIBE Modbus. Luettelo ei ole yleinen yhteensopivuus- tai tuotantohyväksyntä. Älä lisää monipumppuorkestrointia tämän päätöksen perusteella.

- **Tasan yksi lämpöpumppu per Lite-laite.** Älä lisää monen pumpun orkestrointia, keskitettyä ohjausta, energia-akkujen, aurinkosähkön, EV-latauksen tai koko talon EMS:n hallintaa.
- **Ei tekoälyä.** Älä lisää koneoppimista, neuroverkkoja, LLM:iä, AI-palveluita, mallien koulutusta tai AI-riippuvuuksia. Deterministinen rakennus- ja lämpöpumppufysiikan laskenta sekä matemaattinen optimointi ovat sallittuja.
- **Ei Linuxia, Raspberry Pi -vaatimusta, Dockeria, tietokantaa tai Home Assistant -riippuvuutta laitteeseen.** Myös internetin katkettua laitteen on kyettävä toimimaan turvallisesti paikallisesti.
- Alustava laitevalinta: **ESP32-S3 N16R8** (16 Mt Flash, 8 Mt PSRAM), **2,4 GHz Wi-Fi paikallisille pumppuyhteyksille**, Ethernet muissa laiteversioissa tarvittaessa. C++ ja ESP-IDF; laitekokoonpano on varmistettava.
- **Mallikohtaiset pilotit:** Mitsubishi Electric MSZ-FD25VA / MUZ-FD25VA on aiempi kenttäpilotti (ks. [Mitsubishi-pilotti](docs/cottage-mitsubishi-fd25va.md)); Midea V2/V3 on tämänhetkinen suoran paikallisen Wi-Fi-yhteensopivuuden testikohde. Muiden valmistajien integraatiot ja myös näiden pilottien fyysinen toiminta on todistettava mallikohtaisesti. CN105 on tarkistettava todellisesta Mitsubishistä ennen komentoja.
- Erottele Lite Enevina Coresta, Enevina Adaptive Intelligencestä, Finance-koneen tehtävistä sekä olemassa olevista tuotantojärjestelmistä. Älä muuta niiden repositoryja tai tuotantokonfiguraatioita.

Jos ehdotus rikkoo näitä rajoja, älä toteuta sitä. Kerro ristiriita ja ehdota rajaukseen sopiva ratkaisu.

## 2. Haluttu käyttäjäkokemus – kokonaisuus ensin toimivaksi

Ominaisuuksia kehitetään ja testataan yhdessä asteittain; fyysiset laitekirjoitukset hyväksytään erikseen:
1. Lukemaan pumpun mittaukset, tilan ja sallitut ohjauskyvykkyydet.
2. Hakemaan ja välimuistittamaan 15 minuutin sähkönhinnat ja sääennusteen luotettavasta lähteestä; käsittelemään puuttuvat, muuttuneet ja vanhentuneet tiedot.
3. Laskemaan lämpöhäviön, sisälämpötilan kehityksen ja COP:n silloin, kun lähtötiedot ovat riittävät. Menoveden ja käyttöveden laskenta koskee vain niitä laitteita, joissa ne ovat olemassa; historiallisen Mitsubishi-ilma–ilmapumppupilottimme ohjausmalli on 16 °C HEAT / OFF, ei kaikkien pumppujen yleisohjaus.
4. Optimoimaan tilalämmityksen kustannukset turvallisesti; käyttövettä optimoidaan vain sitä tukeville pumpuille.
5. Näyttämään tila, ennuste, seuraava ohjaus, perustelut, viimeisin onnistunut päivitys ja varatilaan siirtymisen syy paikallisessa selainkäyttöliittymässä.
6. Säilyttämään asetukset turvallisesti ja jatkamaan toimintaa sähkö- tai verkkokatkon jälkeen.

Paikallinen käyttö ei saa edellyttää jatkuvaa pilviyhteyttä, tilausta tai ulkopuolista palvelinta. Ulkoiset hintojen ja sään tietolähteet ovat vapaaehtoisia optimointisyötteitä, eivät turvallisen perustoiminnan edellytyksiä.

## 3. Optimointimoottorin vaatimukset – aktiivinen kehityskohde

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
- Hintojen, sään, sisälämpötilan, mittausyhteyden, kellonajan tai optimizerin todetusta virheestä siirrytään määriteltyyn varatilaan. Vanhentuneeseen ennusteeseen perustuvaa optimointia ei saa jatkaa rajatta.
- Varatila: turvallinen ennalta määritelty / pumpun oma ulkolämpötilakompensoitu ohjaus, **vain jos sen palautustapa on kyseisen adapterin osalta todennettu**. Jos sitä ei voida turvallisesti vahvistaa, estä uudet kirjoitukset, näytä hälytys ja anna pumpun oman säätimen hoitaa turvatoiminnot.
- Ei oletuksia siitä, että tietyn rajapinnan write-komento on välitön tai onnistunut ilman kuittausta; vältä kirjoitusmyrskyä ja komennon jatkuvaa uudelleenlähetystä.

## 5. Koodiarkkitehtuuri

Pidä moduulirajat selkeinä; alla olevat ovat rajapintoja, eivät lupa lisätä raskasta kehystä:
- `pump/` — `PumpAdapter`, kyvykkyydet, telemetria, protokollakohtaiset toteutukset.
- `forecast/` — spot-hinnat, sää, aikaleimat, tuoreus, välimuisti.
- `physics/` — rakennuksen, lämmitysveden, lämpöpumpun ja käyttöveden mallit.
- `optimizer/` — puhdas laskenta ilman verkko- tai laiterajapintaa.
- `safety/` — Supervisor, tilakone, turvallisuusrajat ja fallback; älä lisää tarpeettomia watchdog- tai reset-kerroksia.
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
- Kuormita samanaikaisesti Ethernetiä, paikallista UI:ta, antureita ja pumppuyhteyttä. Mittaa vakaus ja mahdollisten olemassa olevien resetien syyt ilman uusia watchdogeja.
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

Historiallinen alkuvaiheen tehtävä oli [Mitsubishi Electric FD25VA -pilotti](docs/cottage-mitsubishi-fd25va.md): PC:llä ajettava C++-malli 16 °C HEAT / OFF -jaksoille, turvallisuusrajoille ja riippumattomalle vertailulle. Tämä ei ole nykyisen monimerkkisen tuotteen prioriteettijono; tarkista ajantasainen kehitystilanne yllä. Hydronisen lämpöpumpun G0:n tulokset säilyvät erillään. Fyysiset komennot edellyttävät edelleen erillistä mallikohtaista hyväksyntää.

Päämittari: *todennettavasti alhaisemmat lämmityksen kokonaiskustannukset ilman mukavuus-, hygienia- tai turvallisuusrajojen rikkomista.*

## 10. Legacy Control — vaihtoehtoinen, mallikohtaisesti testattava ohjausadapteri

**Ehdollinen mallikohtainen arkkitehtuuritutkimus (ei automaattinen fyysisen asennuksen lupa):** Enevina Liten on tuettava myöhemmin laitekohtaisesti ulkolämpötilan signaalin simulointia ("outdoor temperature sensor emulation") silloin, kun suoraa ohjausta tai valmistajan dokumentoitua ulkoista ohjaustuloa ei ole. Tämä on saman yhden pumpun optimointituotteen vaihtoehtoinen adapteri, **ei toinen lämpöpumppu**. Toteutuksen tekniset ja turvallisuusehdot: [docs/legacy-outdoor-sensor-control.md](docs/legacy-outdoor-sensor-control.md).

Priorisoi ohjausreitit: (1) valmistajan dokumentoitu digitaalinen ohjaus, (2) valmistajan hyväksymä ulkoinen asetusarvo-/kuormitus-/lämmityskäyrätulo, (3) dokumentoitu ja todennettu säälle kompensoivan **erillisen** ulkoanturin passiivinen simulointi. Älä koskaan huijaa kylmäaine-, jäätymisenesto-, sulatus-, turva- tai muuta suojausanturia. Jos ulkoanturin turvallinen erillisyys ei ole osoitettavissa, emulointia ei saa sallia kyseisellä pumppumallilla.

- Toteuta anturisovitin erillisenä, pumpun mallin ja mitatun anturin resistanssi–lämpötila-taulukon / muun hyväksytyn sähköisen spesifikaation mukaan. **Älä käytä universaalia NTC-käyrää, vakioresistanssia, yleistä DAC-jännitettä tai oletettua pinoutia.**
- Suunnittele **virraton = alkuperäinen ulkoanturi** -palautus laitteistotasolle (esim. sopivasti mitoitettu normaalisti suljettu, galvaanisesti erotettu kaksinapainen rele), ei vain ohjelmalliseksi fallbackiksi. Älä kytke prototyyppiä todelliseen pumppuun ennen sähköisen turvallisuuden ja kytkennän erillistä hyväksyntää.
- Käytä todellista, riippumatonta ulkolämpötilamittausta rakennuksen lämpöhäviömallissa ja sääennusteessa. Pumpulle simuloitava ulkolämpötila on **vain ohjaussignaali**; nämä kaksi lämpötilaa eivät saa sekoittua.
- Muunna MPC:n tavoittelema menoveden asetusarvomuutos pumpun **todellisen lämmityskäyrän käänteisellä mallilla** rajatuksi näennäiseksi ulkolämpötilaksi. Älä oleta, että lämmitystehon komento vastaa suoraan anturisignaalia.
- Laitemallikohtaiset turvalliset rajat, muutosnopeus, komento-/anturiseuranta ja palautusehdot ovat pakollisia. Oletuksena emulointi on **pois käytöstä** ja vain read-only/shadow-testattavissa.
- Toteuta ensin pelkät simulaatio- ja yksikkötestit (käyrän inversio, rajat, anturikäyrä, vanhentunut oikea lämpötila, power-loss/rele-bypass, sensor fault, lämpötilamittausten sekoittuminen). Ei fyysistä anturi- tai relekytkentää ilman hyväksyttyä mallikohtaista suunnitelmaa.
- **Varmista pumpun muun toiminnan riippuvuus anturista:** lämmityskausi-/kesätila, lisälämmitys, sulatus, jäätymisenesto, hälytykset ja käyttövesiprioriteetti. Jos jotakin turvallisuuden kannalta kriittistä riippuvuutta ei voi rajata pois, älä ota huijausta käyttöön.


## 11. Mitsubishi Electric — mallikohtainen integraatiotesti 16.11.2026

Tämä on **historiallinen ensimmäiseksi valittu Mitsubishi-kenttäpilotti**, ei merkki tämänhetkisestä yleisestä laiteyhteensopivuudesta eikä koko tuotteen ainoa kehityspolku. Aktiivinen kehitys ja PR-ketju on kuvattu yllä; F01–F09 ja kaupallisen julkaisun L01–L14-portit säilyvät tarkistuslistoina. Täsmävaatimukset, lähtötilan vahvistukset ja C0–C3-portit: [docs/cottage-mitsubishi-fd25va.md](docs/cottage-mitsubishi-fd25va.md).

- Vain **yksi** Mitsubishi Electric **MSZ-FD25VA + MUZ-FD25VA** (ilma–ilmapumppu). Ei käyttövesivaraajaa, menoveden säätöä tai shunttia.
- Pumpun alin lämmityspyynti **16 °C**. Kohteen ylläpitotavoite **5 °C** ja ennakoiva turvamarginaali **8 °C**: ohjaus sallittujen lämmitys-/OFF-komentojen kautta, ei 5 °C:n asetusarvona. 5 °C:n suojaa ei saa väittää taatuksi ilman riippumatonta jäätymissuojausta.
- **Shelly Plus 1PM mittaa vain sähkönkulutusta.** Sen releellä ei koskaan katkaista lämpöpumpun syöttöä.
- Ensisijainen yhteys ESP32-S3:n paikallinen **Wi-Fi + CN105**, joka on validoitava mallikohtaisesti; alkuperäinen huoltomanuaali ei yksiselitteisesti näytä CN105:tä ja valmistajan yhteensopivuusluettelo vain tukee sen mahdollisuutta.
- Optimointi: **15 min aika-askel, 24–48 h**, mutta älä kytke kompressoria päälle/pois jokaisella aika-askeleella. Minimi käynti-/seisonta-ajat, hystereesi, mittauksen tuoreus, pumpun tilakuittaukset ja turvamarginaali ovat ensisijaisia.
- Ensin C0 offline-malli, C1 fyysisen rajapinnan vain luku -vahvistus, C2 shadow, C3 vasta erillisellä hyväksynnällä. **Älä flashaa tai ohjaa todellista laitetta tässä dokumentaatiovaiheessa.**
- Vanhojen vesikiertoisten pumppujen Legacy Outdoor Sensor Control pysyy erillisenä myöhempänä sovittimena, ei tämän Mitsun anturihuijauksena.
