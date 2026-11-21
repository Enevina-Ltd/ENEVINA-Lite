# Enevina Lite — kellarin Midea, paikallisen LAN-yhteyden ensimmäinen pilotti

**21.11.2026 · Frezzer Nordicin paikallinen Midea V3 -tilaluku ja rajattu manuaalinen asetuslämpötilan muutos on nyt todennettu myös Litellä.** Firmware `2026112102` muutti sammutetun, lämmitystilaan asetetun pumpun pyynnön **22 → 23 → 22 °C**. Sekä Liten tuore takaisinluku että erillinen Coren tilakysely vahvistivat molemmat muutokset. Testiasetukset palautettiin ja tilapäinen ohjauslupa poistettiin. Katso [kenttätestin tulos ja tarkat rajaukset](../field/MIDEA_LAN_CONTROL_20261121.md) ja [ajantasainen malliluettelo](MIDEA_MODEL_CATALOG.md).

Tämä dokumentti säilyttää myös alkuperäisen lukupilotin suunnitelman. WAN-estokoetta, pilvetöntä ensimmäistä paritusta, kompressorin käyntiä tai kaikkia ohjaustoimintoja ei ole osoitettu kyseisellä kokeella. Tarkka vähittäismyyntimalli ja Wi-Fi-moduulin laiteversio ovat edelleen kirjaamatta; aiemman Coren lukutestin lisäksi käytössä on nyt erillinen Liten oma näyttö.

## Tavoite

Selvittää, voiko Enevina Lite ESP32-S3 N16R8 ottaa suoran paikallisen yhteyden **Midean omaan Wi-Fi-moduuliin** ja lukea tuoreen tilan myös, kun pumpun internet-yhteys on estetty. **Erillistä IR-lähetintä, relettä tai ohjainboksia ei lisätä.** Testityökalu saa väliaikaisesti toimia PC:llä; lopullinen firmware on itsenäinen C++/ESP-IDF.

## Inventaario (päivitetty 21.11.2026)

| Tieto | Havainto |
| --- | --- |
| Sijainti | Kellarissa oleva Midea |
| Sisäyksikön mallinumero / sarja | Frezzer Nordic; tarkka vähittäismyyntimalli ei kirjattu |
| Wi-Fi-sovittimen malli, laiteohjelmisto ja mahdollinen USB-tikku | EI TUNNETA |
| Valmistajan sovellus | EI TUNNETA |
| Wi-Fi-yhteys | Paikallinen LAN-yhteys todennettu Corella 9/2026 ja Litellä 21.11.2026; taajuutta ei vahvistettu |
| Paikallinen IP (ei kirjata GitHubiin) | Vaihtui DHCP:ssä syyskuussa 2026; uusi osoite löydettiin automaattisesti PR #613:ssa; ajantasainen IP tarkistettava paikallisesti |
| Midea-protokolla V1/V2/V3 | V3 varmennettu Corella ja Litellä; V2 ei ole tämän laitteen kenttätesti |
| LAN-tyyppi `0xAC`, `supported` | `0xAC`, classic C0, lyhyt tilavastaus; Liten oma onnistunut autentikoitu tilaluku ja rajattu SET |
| Autentikointi ja V3 `token/key` | Lite ja Core käyttivät ennestään tallennettuja laitekohtaisia V3-tunnuksia; avainten alkuperäistä pilvetöntä hankintaa ei osoitettu |
| Mallin asetuslämpötila-/tila-/tehotiedot | Liten 21.11. testi: OFF / HEAT, 22 → 23 → 22 °C. Teho/energiakenttien luotettavuutta ei osoitettu. Erillinen Shelly Plus 1PM -mittari |
| Read-only discover / autentikointi / tilaluku | **Core: TODETTU 9.9.2026**, mukaan lukien DHCP-uudelleenhaku. **Lite: TODETTU 21.11.2026**, autentikoitu tuore V3-tilaluku; haun käyttöliittymän mallikentät ovat erillinen myöhempi muutos |
| WAN-eston jälkeinen tuore LAN-tilaluku | EI TESTATTU |
| Fyysiset ohjauskirjoitukset | Käyttäjän hyväksymä rajattu **22 → 23 → 22 °C PASS**; pumppu pysyi OFF-tilassa. Testin tilapäinen lupa poistettu; automaattiohjaus pois |

## Aiempi toimiva paikallisintegraatio: Kaelion-Core (8.–9.9.2026)

**Tätä ei saa tutkia uudelleen kuin uutta laitetta.** Repo sisältää jo Midean autentikoidun suoran Wi-Fi/LAN-luennan, toimivan DHCP-korjauksen sekä suunnitellun HEAT-, target- ja power-ohjausketjun. Tiedot ovat historiallista todennusta, eivät väite nykyisestä live-tilasta.

- [midea_lan_control.py](https://github.com/linkki79-pixel/Kaelion-Core/blob/main/midea_lan_control.py): `msmart-ng` / `msmart.device.AirConditioner`, `msmart.discover.Discover`, tallennetun `device_id + token + key` -autentikointi; auto-discovery-reitti ei yksin täytä V3:n autentikointivaatimusta. Ohjauspolussa on erillinen `control_enabled` ja `dry_run`.
- [midea_lan_polling.py](https://github.com/linkki79-pixel/Kaelion-Core/blob/main/midea_lan_polling.py): autentikoitu paikallinen tilaluku; laitetunnisteeseen sidottu DHCP-uudelleenhaku, rajattu LAN-unicast-haku ja broadcast-varatie; todennettu uusi osoite hyväksytään vasta onnistuneen read-only-autentikoinnin jälkeen.
- [midea_lan_provider.py](https://github.com/linkki79-pixel/Kaelion-Core/blob/main/midea_lan_provider.py): pollausjakso, room sensor, sähkömittari, laitteen salaisuuksien peittäminen ja hallittu konfiguraatio.
- [midea_hvac_adapter.py](https://github.com/linkki79-pixel/Kaelion-Core/blob/main/midea_hvac_adapter.py): Midean lukutilan normalisointi HVAC-malliin.
- [PR #613](https://github.com/linkki79-pixel/Kaelion-Core/pull/613): **9.9.2026 fyysinen read-only LAN-todiste** – vanhentunut DHCP-osoite, rajattu verkon sisäinen haku, sama pysyvä laitetunniste, onnistunut autentikoitu uusi luku ja runtime-hostin korjaus. 159 kohdennettua testiä PASS. Kenttätestissä `control_enabled=false`, `dry_run=true`.
- [PR #622](https://github.com/linkki79-pixel/Kaelion-Core/pull/622): kellarin Ruuvi-anturin todellinen `ext_room_c=22.43` °C-luku, 162 kohdennettua ja 2329 laajempaa testiä PASS. Ei fyysisiä ohjauskirjoituksia.
- Kellarin Midean sähkötehon erillinen mittaus: **Shelly Plus 1PM SNSW-001P16EU** (9/2026). Shellyä ei saa käyttää syöttöreleenä hintojen perusteella.

**Tärkeä tekninen erottelu:** Core-koodi vaatii V3:ssa tunnistimen, tokenin ja avaimen, mutta jo toteutettu autentikoitu LAN-käyttö ei yksin todista, että tunnukset saatiin alun perin pilvettömästi. V3 on nyt varmennettu myös Litellä. Salaisuuksien elinkaaren tiedot tarkistetaan olemassa olevasta konfiguraatiosta; niitä ei kopioida GitHubiin. **Älä nollaa tai parita pumppua uudelleen.** Liten C++/ESP32-adapteri on toteutettu; jäljellä olevat kokeet erotetaan yllä jo tehdystä testistä.

## Alkuperäinen lukupilotin suunnitelma (historiallinen)

Alla olevat vaiheet kuvaavat ensimmäistä, vain lukuun rajattua suunnitelmaa.
Ne eivät kumoa yllä kirjattua myöhempää, erikseen hyväksyttyä ohjauskoetta.

## Vaihe A — passiivinen inventaario

1. Lue pumpun tyyppikilven **sisäyksikön malli** ja tarkista Wi-Fi-moduulin tunniste (jos näkyy ilman laitteen vaarallista avaamista). Kirjaa valmistajan sovelluksen nimi, jos käytössä.
2. Selvitä reitittimen DHCP-luettelosta Midean Wi-Fi-moduulin IP. Älä julkaise IP:tä, MACia tai sarjanumeroa GitHubiin. Jos moduuli ei ole WLANissa, pysähdy: älä resetoi tai tee paritusta ilman käyttäjän lupaa.
3. Testi-PC:n on oltava yhteydessä samaan lähiverkkoon. Jos käytössä on VLAN, testaa sääntöjen sallima yhteys, älä avaa koko lähiverkkoa.

## Vaihe B — **vain luku** PC:llä

Käytä harrastajakirjaston omaa discoveryä Linux-/Windows-/macOS-testiympäristössä, **ei kellarin pumpun ohjaukseen**:

```bash
python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install msmart-ng
# Pilvetön lähiverkon discovery: EI automaattista V3 token/key-hakua.
python - <<'PY'
import asyncio
from msmart.discover import Discover

async def main():
    devices = await Discover.discover(auto_connect=False)
    for device in devices:
        # Tulosta vain ei-salaiset kentät (ei IP/MAC/ID/token/key).
        print("Protokolla:", getattr(device, "version", "tuntematon"),
              "Laitetyyppi:", getattr(device, "type", "tuntematon"))
    print("Laitteita löytyi:", len(devices))

asyncio.run(main())
PY
```

**Älä käytä tässä vaiheessa `msmart-ng discover` -CLI-komentoa.** Sen oletuskäytös voi hakea V3 token/key-parin NetHome Plus -pilvestä automaattisesti myös ilman käyttäjän antamia pilvitunnuksia. Yllä `Discover.discover(auto_connect=False)` estää `Discover.connect()`-polun automaattisen V3-autentikoinnin. Se lähettää edelleen lähiverkon tunnistuspaketteja, joten ei ole passiivinen verkkokaappaus. Älä suorita `set`/`toggle`/`setattr`-komentoja tai tulosta yksilöiviä laitetietoja. Testi-PC:n pakettiasennus voi vaatia internetiä; pilviavainhaku vaatii erillisen käyttäjän hyväksynnän.

**Odotettu / kirjattava (sensuroitu) tulos:**
- Onko laite löydettävissä UDP 6445 / 20086? Mikä on `version` (V1/V2/V3) ja tyyppi `0xAC`?
- Onko `supported=true`; onnistuuko autentikointi ja **todellinen paikallinen tilaluku**?
- Jos V3: havaitaanko puuttuva token/key? **Älä syötä valmistajan pilvitunnuksia, ellei pilvitön ensiaktivointi ole erikseen käsitelty ja käyttäjä hyväksynyt poikkeuksen.**
- Säilytä kellotettu loki, sensorikenttien nimet ja viestien tuoreus. Poista lokista **IP, MAC, sarjanumero, device ID, token, key, sovellustili, SSID ja salasana** ennen GitHubiin siirtoa.

Referenssit: [Tofee/midea-ac-HA-py](https://github.com/Tofee/midea-ac-HA-py) (`msmart-ng discover`), [TA2k/ioBroker.midea](https://github.com/TA2k/ioBroker.midea) (V1/V2/V3), [Torsten85/midea-local](https://github.com/Torsten85/midea-local) (UDP 6445/20086, TCP 6444).

## Vaihe C — pilvestä riippumattomuuden koe

**Vasta kun B onnistuu ja verkkokytkennät tunnetaan.** Reitittimen testisäännöllä estetään **vain pumpun pääsy valmistajan internet-palvelimiin**, ei sen IP-yhteyttä Liten/testi-PC:n kanssa. Älä irrota pumpun sähköä tai WLANia. Tee uusi paikallinen read-only-autentikointi ja tilakysely WAN-eston jälkeen **vain Coren jo paikallisesti tallennetuilla V3-tunnisteilla** tai muilla hyväksytyillä ennakkoon tallennetuilla avaimilla, ei `msmart-ng discover` -autoyhteydellä. Pelkkä pumpun WAN-esto ei estä testi-PC:n pilviyhteyttä; pilvettömyyden todentamiseksi estä myös testi-PC:n valmistajapilvikutsut; tarkista myöhemminkin että tilatietojen *sisältö/todellinen lähde* päivittyy, ei vain yhteyden heartbeat.

- **PASS ajonaikainen LAN:** uusi mitattu tilaluku ja uudelleenyhteys ilman internetiä.
- **PASS L0:** tämän lisäksi todennettu ensiliitos sekä kaikki tarvitut avaimet **ilman valmistajan pilveä**.
- **L1:** vain jo aiemmin pilvestä ladatut V3-avaimet mahdollistavat paikallisen käytön. Ei merkitä täysin pilvettömäksi.
- **FAIL / tuntematon:** ainoastaan sovellus-/pilviyhteys, epäonnistunut LAN-auth, vanhentuneet lukemat tai tuntematon moduuli.

Keskeytä testi, jos lämpöpumpun normaali toiminta muuttuu, yhteydet häviävät tai havaitaan virhetiloja. Poista vain luotu WAN-testisääntö, älä muuta pumpun asetuksia.

## Vaihe D — adapterin kehityspolku

1. Kiinnitä C++-testivektorit löydettyyn protokollaversioon ja todelliseen (salaisuuksista siivottuun) vastaukseen.
2. Toteuta `MideaLanAdapter` vain **luku-/shadow-tilassa**: paikallinen discovery, V1/V2/V3-versiologiikka, tarvittaessa valmiiksi provisionoidun avaimen turvallinen käyttö, jäsentimen virhetilat, aidon lähdetuoreuden valvonta, uudelleenyhdistys.
3. Aja mock-/negatiiviset testit: ei laitetta, väärä IP, väärä laite, väärä avain, katkennut LAN, vanha tilatieto, epävalidi paketti, restart ja katkennut internet.
4. Vasta erillisessä hyväksytyssä fyysisessä koesuunnitelmassa: rajattu lämmön asetusarvo **Supervisorin kautta**, kuittaus, todellinen palautus. Ei sähkökatkaisua releellä.

**Tähän PR:ään kuuluu vain dokumentaatio**; mitään automaattista aktivointia, paritusta, firmwareflashia, pumpun ohjauskomentoa tai Home Assistant -muutosta ei tehdä.

Yhteensopivuus: [LOCAL_WIFI_COMPATIBILITY.md](LOCAL_WIFI_COMPATIBILITY.md) · muut merkit: [LOCAL_WIFI_CONNECTION_GUIDE.md](LOCAL_WIFI_CONNECTION_GUIDE.md).
