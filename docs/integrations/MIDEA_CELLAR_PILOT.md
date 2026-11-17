# Enevina Lite — kellarin Midea, paikallisen LAN-yhteyden ensimmäinen pilotti

**17.11.2026 · suunniteltu read-only-todennus, ei tehty testi.** Käyttäjällä on kellarissa **Midea-ilmalämpöpumppu**, mutta **sisäyksikön tarkka mallikoodi, Wi-Fi-moduuli, firmware, protokolla ja IP ovat vielä varmentamatta**. Älä kirjaa laitetta toimivaksi tai muuta sen ohjausta ilman todellista mittausnäyttöä. Tämä dokumentti ei muuta HA:ta, pumpun asetuksia, Wi-Fiä tai ESP32:n ohjelmistoa.

## Tavoite

Selvittää, voiko Enevina Lite ESP32-S3 N16R8 ottaa suoran paikallisen yhteyden **Midean omaan Wi-Fi-moduuliin** ja lukea tuoreen tilan myös, kun pumpun internet-yhteys on estetty. **Erillistä IR-lähetintä, relettä tai ohjainboksia ei lisätä.** Testityökalu saa väliaikaisesti toimia PC:llä; lopullinen firmware on itsenäinen C++/ESP-IDF.

## Inventaario (täytetään vasta kenttähavainnosta)

| Tieto | Havainto |
| --- | --- |
| Sijainti | Kellarissa oleva Midea |
| Sisäyksikön mallinumero / sarja | EI TUNNETA |
| Wi-Fi-sovittimen malli, laiteohjelmisto ja mahdollinen USB-tikku | EI TUNNETA |
| Valmistajan sovellus | EI TUNNETA |
| Wi-Fi-yhteys pumpusta omaan 2,4 GHz verkkoon | VARMENTAMATTA |
| Paikallinen IP (ei julkaista tähän julkisiin raportteihin) | VARMENTAMATTA |
| Midea-protokolla V1/V2/V3 | VARMENTAMATTA |
| LAN-tyyppi `0xAC`, `supported` | VARMENTAMATTA |
| V3 `token/key` (vain paikallisessa salaisuusvarastossa) | EI TUNNETA |
| Mallin asetuslämpötila-/tila-/tehotiedot | VARMENTAMATTA |
| Read-only discover / autentikointi / tilaluku | EI TESTATTU |
| WAN-eston jälkeinen tuore LAN-tilaluku | EI TESTATTU |
| Fyysiset ohjauskirjoitukset | **EI SALLITTU tässä pilotin vaiheessa** |

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
msmart-ng discover
```

Komennot on tarkoitettu tiedonhakuun. `msmart-ng discover` voi lähettää LAN-discovery-viestejä ja tehdä tunnistukseen liittyviä yhteyspyyntöjä; se **ei ole täysin passiivinen verkkokaappaus**, mutta ei pyydä lämmitysasetusten muuttamista. Älä suorita `set`/`toggle`/`setattr` komentoja. Asennus vaatii työkoneelle internetin paketin lataamiseen, **ei tarkoita pumpun pilviohjausta**.

**Odotettu / kirjattava (sensuroitu) tulos:**
- Onko laite löydettävissä UDP 6445 / 20086? Mikä on `version` (V1/V2/V3) ja tyyppi `0xAC`?
- Onko `supported=true`; onnistuuko autentikointi ja **todellinen paikallinen tilaluku**?
- Jos V3: havaitaanko puuttuva token/key? **Älä syötä valmistajan pilvitunnuksia, ellei pilvitön ensiaktivointi ole erikseen käsitelty ja käyttäjä hyväksynyt poikkeuksen.**
- Säilytä kellotettu loki, sensorikenttien nimet ja viestien tuoreus. Poista lokista **IP, MAC, sarjanumero, device ID, token, key, sovellustili, SSID ja salasana** ennen GitHubiin siirtoa.

Referenssit: [Tofee/midea-ac-HA-py](https://github.com/Tofee/midea-ac-HA-py) (`msmart-ng discover`), [TA2k/ioBroker.midea](https://github.com/TA2k/ioBroker.midea) (V1/V2/V3), [Torsten85/midea-local](https://github.com/Torsten85/midea-local) (UDP 6445/20086, TCP 6444).

## Vaihe C — pilvestä riippumattomuuden koe

**Vasta kun B onnistuu ja verkkokytkennät tunnetaan.** Reitittimen testisäännöllä estetään **vain pumpun pääsy valmistajan internet-palvelimiin**, ei sen IP-yhteyttä Liten/testi-PC:n kanssa. Älä irrota pumpun sähköä tai WLANia. Tee uusi paikallinen read-only-autentikointi ja tilakysely WAN-eston jälkeen; tarkista myöhemminkin että tilatietojen *sisältö/todellinen lähde* päivittyy, ei vain yhteyden heartbeat.

- **PASS ajonaikainen LAN:** uusi mitattu tilaluku ja uudelleenyhteys ilman internetiä.
- **PASS L0:** tämän lisäksi todennettu ensiliitos sekä kaikki tarvitut avaimet **ilman valmistajan pilveä**.
- **L1:** vain jo aiemmin pilvestä ladatut V3-avaimet mahdollistavat paikallisen käytön. Ei merkitä täysin pilvettömäksi.
- **FAIL / tuntematon:** ainoastaan sovellus-/pilviyhteys, epäonnistunut LAN-auth, vanhentuneet lukemat tai tuntematon moduuli.

Keskeytä testi, jos lämpöpumpun normaali toiminta muuttuu, yhteydet häviävät tai havaitaan virhetiloja. Poista vain luotu WAN-testisääntö, älä muuta pumpun asetuksia.

## Vaihe D — adapterin kehityspolku

1. Kiinnitä C++-testivektorit löydettyyn protokollaversioon ja todelliseen (salaisuuksista siivottuun) vastaukseen.
2. Toteuta `MideaLanAdapter` vain **luku-/shadow-tilassa**: paikallinen discovery, V1/V2/V3-versiologiikka, tarvittaessa valmiiksi provisionoidun avaimen turvallinen käyttö, parsin virhetilat, aidon lähdetuoreuden valvonta, uudelleenyhdistys.
3. Aja mock-/negatiiviset testit: ei laitetta, väärä IP, väärä laite, väärä avain, katkennut LAN, vanha tilatieto, epävalidi paketti, restart ja katkennut internet.
4. Vasta erillisessä hyväksytyssä fyysisessä koesuunnitelmassa: rajattu lämmön asetusarvo **Supervisorin kautta**, kuittaus, todellinen palautus. Ei sähkökatkaisua releellä.

**Tähän commit-sarjaan kuuluu vain dokumentaatio**; mitään automaattista aktivointia, paritusta, firmwareflashia, pumpun ohjauskomentoa tai Home Assistant -muutosta ei tehdä.

Yhteensopivuus: [LOCAL_WIFI_COMPATIBILITY.md](LOCAL_WIFI_COMPATIBILITY.md) · muut merkit: [LOCAL_WIFI_CONNECTION_GUIDE.md](LOCAL_WIFI_CONNECTION_GUIDE.md).
