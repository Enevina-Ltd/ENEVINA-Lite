# Enevina Lite — Legacy Outdoor Sensor Control (V1-varayhteys)

**Tila:** sitova tuotevaatimus ja alustava toteutussuunnitelma. **Anturisimuloinnin laite- ja tuotantototeutusta ei ole vielä tehty tai hyväksytty.** Ei sähköisiä kytkentäohjeita tiettyyn pumppuun ilman valmistajakohtaista dokumentaatiota.

## Käyttötarkoitus

Yksi Lite ohjaa yhtä vanhaa lämpöpumppua, jolla ei ole toimivaa digitaalista ohjausrajapintaa. Jos pumppu käyttää erillistä, turvalliseksi todennettua ulkolämpötila-anturia pelkän sääkompensoivan menoveden säätöön, Lite voi manipuloida pumpun *kokemaa* ulkolämpötilaa ohjatakseen lämmityskäyrän tavoitetta. Tämä on **viimesijainen vaihtoehto**, ei universaali toiminto.

Ohjausreittien prioriteetti:
1. Dokumentoitu suora digitaalinen asetusarvo (HeishaMon/Modbus tai muu valmistajakohtainen).
2. Valmistajan dokumentoima ulkoinen lämmityksen korjaus-/termostaatti-/kuormitusohjaus.
3. Ulkolämpötila-anturin **erikseen hyväksytty** passiivinen resistanssi- tai muu signaaliemulointi.

Esimerkiksi NIBE F1245:n dokumentoitu `external adjustment` -tulo pystyy muuttamaan lämmityskäyrän offsetia sulkeutuvalla koskettimella. Tätä pitää harkita ennen BT1:n simulointia. F1245:n BT1 vaikuttaa myös toimintatilan vaihtoon, joten BT1-emulointia ei saa pitää automaattisesti turvallisena.

## Turvallisuusportti — ei kiertoteitä

Ennen mallikohtaista laitetoteutusta on todennettava kaikki seuraavat:
- Valmistajan huolto-ohjeesta anturiliittimen toimintatapa, anturin tarkka tyyppi, resistanssi–lämpötilakäyrä, käyttöjännitteen/virran ja johtimien luonne sekä virhetilan reagointi.
- Onko anturi *yksinomaan sääkompensointia varten*. Jos sama arvo vaikuttaa sulatukseen, jäätymissuojaan, kompressorin turvallisuuteen tai muuhun kriittiseen toimintaan, **emulointi kielletään**, kunnes turvallinen vaihtoehtoinen rajapinta on vahvistettu.
- Ulkoyksikön/laitteen liittimen sähköinen erottelu ja kytkentä toteutetaan pätevän asentajan ja laiteohjeiden mukaisesti. Vältä takuuseen tai CE-/EMC-vaatimustenmukaisuuteen vaikuttavia oletuksia.
- Alkuperäinen anturi voidaan ottaa käyttöön ilman ESP:n toimintaa: **de-energized / watchdog / brownout / sähkökatko / kaapelivika => alkuperäinen ulkoanturi takaisin**. Suunnittele tämän varmistava erikseen mitoitetuilla kontakteilla toteutettu, normaalisti suljettu kahden johtimen bypass. On testattava myös jumiutunut rele, rikkoutunut emulaattori ja irronnut anturi; pelkkä NC-rele ei suojaa kaikilta näiltä vioilta.
- Vahvistettu pumpun näytöltä tai diagnostiikasta, että simuloitu lämpötila vastaa asetettua eikä tuota anturihälytystä.
- Emulointi käynnistyy aina pois päältä ja siirtyy vasta erillisellä hyväksynnällä shadow -> rajoitettu testitila -> tuotantotila. Muutokset on rajattava ja palautus määriteltävä ilman katkoksen aikaista lämmitystä vaarantavia hyppyjä.

**Älä koskaan siirrä, simuloi tai korvaa ulkoyksikön höyrystin-/lämmönvaihdin-/kylmäaine-/sulatus-/kompressorisuojauksen antureita.**

## Toimintaperiaate

Syötteet:
- `T_outdoor_real`: riippumaton todellinen ulkolämpötila, jota käytetään rakennusfysiikassa ja COP-/tehoraja-arviossa.
- `T_indoor_real`: sisälämpötila, jos mitattavissa.
- `T_supply_observed`: toteutunut menovesi, jos mitattavissa.
- `heat_curve(T_outdoor_perceived)`: kyseisen pumpun testattu lämmityskäyrä, mahdolliset kompensaatiot ja käytettävissä olevat rajat.
- `T_supply_target`: MPC:n tavoittelema **menoveden asetusarvo**, ei ideaalinen lämpötehokomento.

Lasketaan `T_outdoor_perceived = inverse_heat_curve(T_supply_target)` vain sellaisessa käyrän osassa, jossa inversio on mahdollinen ja turvallinen. Sitten rajataan:
- mallikohtainen absoluuttinen uskottava ulkolämpötila-alue,
- sallittu ero oikeaan ulkolämpötilaan,
- muutoksen enimmäisnopeus ja vähimmäisodotusaika,
- lämmityksen/viilennyksen, kesä-/talvitilan, käyttöveden ja hälytystilojen vaikutukset,
- pumpun omat asetusarvo- ja ohjausrajat.

Pumppu näkee siis `T_outdoor_perceived`, mutta **Lite käyttää todellista ulkolämpötilaa fysiikkalaskennassa**. Optimoijan kustannusmalli arvioi, kuinka pumpun todellinen säätö ja mahdollinen COP-haitta muuttuvat epäsuoran ohjauksen seurauksena. Lämmityskäyrän muutos ei välttämättä vaikuta hetkelliseen sähkötehoon odotetulla tavalla; se pitää mitata.

Esimerkkisuunta (ei hyväksytty rajaus): jos ulkona on −5 °C, −10 °C:ksi tulkittu anturi voi nostaa menoveden tavoitetta ja 0 °C:ksi tulkittu anturi alentaa sitä, **vain silloin kun pumpun lämmityskäyrä ja muut toiminnot tämän sallivat**.

## Mahdollinen elektroniikka, ei vielä valittu

Anturisovitin voisi koostua:
- ESP32-Lite-emolevyn ohjaamasta, galvaanisesti erotetusta releestä/sopivasta analogisesta kytkimestä ja valmistajakohtaisesta anturisimulaattorista.
- Vastusverkosta / kalibroidusta säädettävästä resistanssista **vain vahvistetun resistiivisen anturityypin tapauksessa**. Digitaalinen potentiometri soveltuu ainoastaan, jos sen jännite-, virta-, erottelu-, resoluutio-, lämpötila- ja vikakäyttäytymisrajat on osoitettu; sitä ei saa kytkeä suoraan tuntemattomaan anturipiiriin.
- Alkuperäisen anturin virrattomasta ohituksesta sekä riippumattomasta oikean ulkolämpötilan anturista.

Emulointia ei yhdistetä paralleelivastuksella tai kiinteällä yleis-NTC-arvolla arvaamalla. Mitään tietyn pumpun liitin- tai kytkentäkaaviota ei saa julkaista tuotantoratkaisuna ennen validointia.

## Ohjelmistoarkkitehtuuri

- `LegacyOutdoorSensorAdapter` toteuttaa uuden **ohjauskyvykkyyden** `IndirectHeatCurveAdjustment`; se ei ole yleinen digitaalinen `SetHeatPower`-komento.
- `LegacySensorProfile`: valmistaja, pumppumalli, anturikäyrä, käyrän oikea inversio, turvalliset rajat, kytkentätilan lukeminen ja validoitu elektroniikkaversio.
- `LegacyController`: laskee MPC-tavoitteesta kohtuullisen näennäisen ulkolämpötilan, tarkistaa rajoitukset ja pyytää Supervisorilta lupaa.
- `Supervisor`: oletuksena `bypass`, sallii aktivoida emuloinnin vain laitekohtaisen hyväksynnän, todellisten anturilukemien tuoreuden, kellon, virheettömän kytkentäpalautteen ja raja-/nopeustestien jälkeen.
- `HardwareFailSafe`: emulointikytkentä siirtyy alkuperäiselle anturille ilman ohjelmiston onnistumista, ja todellinen tila pyritään havaitsemaan.
- `Optimizer` saa käyttöönsä `AvailableActuationProfile`-rajoitukset ja ennustetun ohjausvasteen. Se ei saa olettaa voivansa käskeä ideaalista lämpötehoa.

## Portit ja hyväksyntäkokeet

**L0 — Desktop-only**
- Rakennusmallin oikea ulkolämpötila on aina erillinen pumpulle emuloitavasta.
- Käyrän käänteismuunnos: epämonotonisuus, clamp, puuttuva kalibrointi, nan/inf ja virheellinen anturikäyrä.
- Käyttöveden ja kesä-/talvitilan vaikutus; ei tunnistamattomien tilojen emulointia.
- Viiveet, lämpötilan muutosrajat, ehdotusten rate limit, keskeytys, shadow/bypass.
- Optimoinnin MILP-vertailu epäsuoran ohjauksen saavutettavissa olevilla, ei ideaalisen lämpötehon toiminnoilla.

**L1 — Pöytätesti ilman pumppua**
- Anturisimulaattorin resistanssivastaavuus ja johdineheys eri asetusarvoilla ja lämpötiloilla.
- Virrattoman palautuksen, watchdogin, brownoutin, kaapelikatkon, jumittuneen releen ja virhetilojen testaus.
- Eristys, EMC- ja sähköiset rajat.

**L2 — Hyväksytty pumppumalli, aluksi read-only**
- Mallikohtainen dokumentaatio sekä asentajan hyväksyntä.
- Pumpun diagnostiikan, lämmityskäyrävasteen ja sivuvaikutusten todennus.
- Rajattu testi vain nimenomaisella käyttöluvalla; palautuksen todentaminen.

**L3 — Tuotanto**
- Toistettava lopullinen turvallisuus- ja säästövertailu, pitkäaikaiskoe, mallikohtainen käyttöohje.
- Ei automaattista pääsyä tuotantokäyttöön ilman erillistä hyväksyntää.

## Ei sisälly nykyiseen PR #1:een

Nykyinen G0-optimointimoottori mallintaa idealisoituja lämpötehotoimintoja. Anturisimulointi **ei ole toteutettu** eikä nykyinen PR #1 ohjaa yhtään anturia. Tätä lisävaatimusta ei pidä merkitä valmiiksi pelkän dokumentin perusteella.
