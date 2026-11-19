# Enevina Lite — kellarin Midea, paikallisen LAN-yhteyden ensimmäinen pilotti

**19.11.2026 · Enevina Liten Midea V2/V3 -lukuadapterien lähdekoodi on yhdistetty `main`-haaraan (PR #48 ja #51), mutta Lite-laitteen fyysinen Frezzer Nordic -yhteystesti on edelleen tekemättä.** Käyttäjän kellarin **Midea-ilmalämpöpumppuun on jo toteutettu ja fyysisesti todennettu paikallinen LAN-lukuyhteys Kaelion-Core-repossa 8.–9.9.2026**. Liten oma ESP32-S3-adapteri ja uusi nykyhetken LAN-/WAN-estotesti ovat edelleen tekemättä. Sisäyksikön tarkka mallikoodi, Wi-Fi-moduuli ja firmware ovat yhä tuntemattomat. Aiemmin todettu Core-yhteys **ei yksin todista pilvetöntä ensimmäistä paritusta tai Liten toimivuutta**. Älä muuta pumpun asetuksia ilman erillistä hyväksyntää.

## Tavoite

Selvittää, voiko Enevina Lite ESP32-S3 N16R8 ottaa suoran paikallisen yhteyden **Midean omaan Wi-Fi-moduuliin** ja lukea tuoreen tilan myös, kun pumpun internet-yhteys on estetty. **Erillistä IR-lähetintä, relettä tai ohjainboksia ei lisätä.** Testityökalu saa väliaikaisesti toimia PC:llä; lopullinen firmware on itsenäinen C++/ESP-IDF.

## Inventaario (täytetään vasta kenttähavainnosta)

| Tieto | Havainto |
| --- | --- |
| Sijainti | Kellarissa oleva Midea |
| Sisäyksikön mallinumero / sarja | EI TUNNETA |
| Wi-Fi-sovittimen malli, laiteohjelmisto ja mahdollinen USB-tikku | EI TUNNETA |
| Valmistajan sovellus | EI TUNNETA |
| Wi-Fi-yhteys | Paikallinen LAN-yhteys on todistettu Core-ympäristössä syyskuussa 2026; taajuutta ei vahvistettu |
| Paikallinen IP (ei kirjata GitHubiin) | Vaihtui DHCP:ssä syyskuussa 2026; uusi osoite löydettiin automaattisesti PR #613:ssa; ajantasainen IP tarkistettava paikallisesti |
| Midea-protokolla V1/V2/V3 | Kellarin Frezzer Nordic on jo varmennettu Corella Midea V3 -LAN-yhteytenä; Lite V2/V3 -lukuadapterit eivät ole vielä kenttävarmennettuja |
| LAN-tyyppi `0xAC`, `supported` | VARMENTAMATTA |
| Autentikointi ja V3 `token/key` | Core-koodi tukee tallennettuja laitekohtaisia tunnuksia; syyskuun LAN-luku onnistui autentikoidusti aiemmin tallennetuilla tunnuksilla. Varsinainen protokollaversio ja avainten alkuperä **ei varmistettu** |
| Mallin asetuslämpötila-/tila-/tehotiedot | Syyskuun lukutilanne: tavoite 22 °C, huonelämpötila 24,5 °C, ulkolämpötila 14 °C; teho/energiakenttien luotettavuutta ei osoitettu. Erillinen Shelly Plus 1PM -mittari |
| Read-only discover / autentikointi / tilaluku | **Core: TODETTU 9.9.2026**, mukaan lukien DHCP-uudelleenhaku ja tunnistetun laitteen autentikoitu uusi tilaluku. **Lite: EI TESTATTU** |
| WAN-eston jälkeinen tuore LAN-tilaluku | EI TESTATTU |
| Fyysiset ohjauskirjoitukset | **EI SALLITTU tässä pilotin vaiheessa** |

## Aiempi toimiva paikallisintegraatio: Kaelion-Core (8.–9.9.2026)

**Tätä ei saa tutkia uudelleen kuin uutta laitetta.** Repo sisältää jo Midean autentikoidun suoran Wi-Fi/LAN-luennan, toimivan DHCP-korjauksen sekä suunnitellun HEAT-, target- ja power-ohjausketjun. Tiedot ovat historiallista todennusta, eivät väite nykyisestä live-tilasta.

- [midea_lan_control.py](https://github.com/linkki79-pixel/Kaelion-Core/blob/main/midea_lan_control.py): `msmart-ng` / `msmart.device.AirConditioner`, `msmart.discover.Discover`, tallennetun `device_id + token + key` -autentikointi; auto-discovery-reitti ei yksin täytä V3:n autentikointivaatimusta. Ohjauspolussa on erillinen `control_enabled` ja `dry_run`.
- [midea_lan_polling.py](https://github.com/linkki79-pixel/Kaelion-Core/blob/main/midea_lan_polling.py): autentikoitu paikallinen tilaluku; laitetunnisteeseen sidottu DHCP-uudelleenhaku, rajattu LAN-unicast-haku ja broadcast-varatie; todennettu uusi osoite hyväksytään vasta onnistuneen read-only-autentikoinnin jälkeen.
- [midea_lan_provider.py](https://github.com/linkki79-pixel/Kaelion-Core/blob/main/midea_lan_provider.py): pollausjakso, room sensor, sähkömittari, laitteen salaisuuksien peittäminen ja hallittu konfiguraatio.
- [midea_hvac_adapter.py](https://github.com/linkki79-pixel/Kaelion-Core/blob/main/midea_hvac_adapter.py): Midean lukutilan normalisointi HVAC-malliin.
- [PR #613](https://github.com/linkki79-pixel/Kaelion-Core/pull/613): **9.9.2026 fyysinen read-only LAN-todiste** – vanhentunut DHCP-osoite, rajattu verkon sisäinen haku, sama pysyvä laitetunniste, onnistunut autentikoitu uusi luku ja runtime-hostin korjaus. 159 kohdennettua testiä PASS. Kenttätestissä `control_enabled=false`, `dry_run=true`.
- [PR #622](https://github.com/linkki79-pixel/Kaelion-Core/pull/622): kellarin Ruuvi-anturin todellinen `ext_room_c=22.43` °C-luku, 162 kohdennettua ja 2329 laajempaa testiä PASS. Ei fyysisiä ohjauskirjoituksia.
- Kellarin Midean sähkötehon erillinen mittaus: **Shelly Plus 1PM SNSW-001P16EU** (9/2026). Shellyä ei saa käyttää syöttöreleenä hintojen perusteella.

**Tärkeä tekninen erottelu:** Core-koodi vaatii V3:ssa tunnistimen, tokenin ja avaimen, mutta jo toteutettu autentikoitu LAN-käyttö ei yksin todista, että tunnukset saatiin alun perin pilvettömästi. Laitekohtainen V1/V2/V3-versio ja salaisuuksien elinkaari tutkitaan olemassa olevan tuotantokonfiguraation turvallisesti sensuroiduista tiedoista; niitä ei kopioida GitHubiin. **Älä nollaa tai parita kellarin pumppua uudelleen.** Ensisijainen tehtävä on sovittaa toimivaksi osoitettu protokolla erilliseksi Liten C++/ESP32-adapteriksi.

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

**Vasta kun B onnistuu ja verkkokytkennät tunnetaan.** Reitittimen testisäännöllä estetään **vain pumpun pääsy valmistajan internet-palvelimiin**, ei sen IP-yhteyttä Liten/testi-PC:n kanssa. Älä irrota pumpun sähköä tai WLANia. Tee uusi paikallinen read-only-autentikointi ja tilakysely WAN-eston jälkeen **vain Coren jo paikallisesti tallennetuilla V3-tunnisteilla** tai muilla hyväksytyillä ennakkoon tallennetuilla avaimilla, ei `msmart-ng discover` -autoyhteydellä. Pelkkä pumpun WAN-esto ei estä testi-PC:n pilviyhteyttä; pilvettömyyden todentamiseksi estä myös testi-PC:n valmistajapilvikutsut;  tarkista myöhemminkin että tilatietojen *sisältö/todellinen lähde* päivittyy, ei vain yhteyden heartbeat.

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
