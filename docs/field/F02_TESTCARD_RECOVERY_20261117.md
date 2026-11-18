# F02 ESP32-S3 testikortti – ei-tuhoavat tarkistukset ja palautussuunnitelma (17.11.2026)

**Tämä on testikortin valmistelu, ei lupa fläshätä.** Kenttätestilaitteiden ESP32/CN105/Shelly ja muut tuotantolaitteet pysyvät muuttumattomina.

## Toteutunut laitetunnistus ja lukutilanne 17.11.2026

**ROM-download-kättely ja kaksi kokonaisen 16 MiB:n flashin lukuyritystä suoritettiin käyttäjän nimenomaisella vain-luku-hyväksynnällä.** Laitteeseen ei kirjoitettu mitään. Tämä lukuosa on **FAIL**, eikä firmwareasennusta ole hyväksytty.

- Piiri suoraan esptool 5.3.1:stä: **ESP32-S3 QFN56, revisio 0.2**, 40 MHz, sulautettu **8 MiB PSRAM**, todellinen **16 MiB Flash**, valmistajan ID `0x46`, flash device ID `0x4018`. USB CH340/CH341, `/dev/ttyUSB1`.
- Vain luku -tyyppinen `get-security-info`: **Secure Boot disabled**, **Flash Encryption disabled**. Ne eivät yksin todista palautettavuutta.
- **460800 baud:** koko flashin lukeminen keskeytyi `1 540 096 / 16 777 216` tavuun, `Corrupt data, expected 0x1000 bytes but received 0xfc4 bytes`.
- **230400 baud:** lukeminen keskeytyi `4 509 696 / 16 777 216` tavuun, `Corrupt data, expected 0x1000 bytes but received 0xff3 bytes`.
- Kumpikaan yritys ei luonut **edes talteen jäänyttä osittaista image-tiedostoa**. Täyden flash-kuvan kokoa, SHA-256:ta, tämänhetkistä osiotaulua eikä palautuksen toimivuutta voida todentaa.
- Esptool raportoi `Hard resetting via RTS pin` ja USB-portti jäi näkyviin; juuri tämän testikortin normaalista käynnistyksestä ei saatu suoraa sarjatodistetta. Muiden verkkopalvelujen HTTP-vastaus ei sitä yksilöi.
- Lokit ja `read_audit.json` pysyvät **yksityisessä** hakemistossa `<private-local-backup-directory>`. Älä kopioi flash-sisältöä, NVS:ää tai yksityisiä avaimia GitHubiin.
- Kaksi virhettä eri osoitteissa viittaavat mahdolliseen **lukusiirron epävakauteen**, mutta eivät yksilöi USB-kaapelia, sarjasovitinta, virransyöttöä tai flashia syyksi. Core-koneen kernel-lokissa ei näkynyt katsotulla lukuaikavälillä USB-irrotus- tai alijänniteilmoitusta; sekään ei poissulje lohkosiirtovirhettä.

**Seuraava askel:** tarkista fyysinen USB-kaapeli, liitännät, hubit ja virransyöttö. Vasta tämän jälkeen toista hyväksytty vain-luku-operaatio tarvittaessa hitaammin tai rajatuissa osissa. Yhdistä osat vasta kun offsetit, koot ja niiden eheys täsmäävät. Nykyinen osiotaulu voidaan tällöin todentaa jo ehjästä alkuosasta, mutta **täysi palautus vaatii yhä koko flashin**. Älä tee flash-, NVS-, OTA-, bootloader- tai eFuse-kirjoituksia. Käytössä olevia keräimiä ja OTA-palvelua ei pysäytetä.

## Aiemmat (ennen 17.11. tehtyjä lukuyrityksiä) lähtötiedot

- Core-koneen `/dev/ttyUSB1` on tällä hetkellä CH340/CH341 USB-sarjasovitin (USB ID `1a86:7523`). Sovitin **ei todenna** sen takana olevan ESP32:n mallia, flash-kapasiteettia tai kytkentää.
- Vanha, yksityiseen hakemistoon tallennettu **16.11.2026 klo 21:52** esptool v5.3.1 -lukuloki vahvistaa, että samalla portilla oli silloin **ESP32-S3 QFN56 revision v0.2, sulautettu 8 Mt PSRAM, 40 MHz kide**. Luku päättyi merkintään `Hard resetting via RTS pin`. Tämä on **historiallinen laitetunnistus**, ei uusi 17.11. laitehandshake tai todiste siitä, ettei laitetta olisi vaihdettu.
- Alkuperäisen flash-prefiksin ESP32-kuvaotsake ilmoittaa ohjelmiston **konfiguroiduksi flash-kooksi 16 Mt** (kokokoodi 4). Todellista nykyistä JEDEC-flash-tunnistetta ja piirin fyysistä kokoa ei ole vielä luettu ilman laitteen resetointia.
- USB:n by-id-polku `/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0` on yleisniminen eikä sisällä yksilöivää sarjanumeroa. Portin käyttäjää ei löytynyt `fuser`-tarkistuksessa, mutta saman Core-koneen verkon yli lukevat Ruuvi/CN105-kerääjät ja OTA-palvelu olivat käynnissä ja jätettiin ennalleen.
- Yksityisessä testikortin varmuuskopiohakemistossa on 64 KiB:n `factory_flash_prefix_64k.bin`, SHA-256 `09fd37f9d5e89b8d25680f5cd31ba26fdfd65f92172bb6fa04967f45809aa2d1`.
- Alkuperäisen prefiksin osoitteen `0x8000` osiotaulussa ovat mm. NVS `0x9000` / `0x4000`, otadata `0xd000` / `0x2000`, `ota_0` `0x100000` / `0x600000` ja `ota_1` `0x700000` / `0x600000`.
- Erillisessä C2.4:n 64 KiB:n prefiksissä `flash_prefix_64k.bin` on **eri** osiotaulu: NVS `0x9000` / `0x5000`, otadata `0xe000` / `0x2000`, `app0` `0x10000` / `0x640000`, `app1` `0x650000` / `0x640000`. Sen SHA-256 on `e96cbfa3ca38dbb778d32205e28f413a2108cf2ec8f2a14d65de180cc17c6c1f`.
- Nämä eivät ole täysiä flash-varmuuskopioita. Osiotaulujen ja NVS:n yhteensopivuutta nykyisen kortin kanssa ei ole todistettu. Edellä mainitut paikalliset polut/varmuuskopiot **eivät** kuulu GitHubiin.

## Ennen yhtäkään laiteoperaatiota

1. Varmista **fyysinen kortti ja USB-portti uudelleen**: udev-tiedot, kortin merkinnät, piiriversio ja johdotus. Tarkista, ettei portti ole tuotantokäytössä tai seuranta kesken.
2. ROM-download ja koko flashin vain-luku-operaatio hyväksyttiin ja sitä kokeiltiin kahdesti; molemmat luvut epäonnistuivat. Tarkista ensin USB-yhteys ja virransyöttö, lue koko flash onnistuneesti yksityiseen paikkaan ja tarkista koko, SHA-256, osiotaulu/NVS/otadata/bootloader. **Asennus- tai palautuskirjoituksia ei ole hyväksytty.**
3. Vertaa todellista osiotaulua suunniteltuun ESP32-S3 N16R8 -buildiin. Jos NVS:n sijainti tai bootloader/ota-osiot eivät täsmää, **älä fläshää**. Suunnittele erillinen turvallinen, palautettava app-only-päivitys.
4. Määritä palautuspolku ja varmista sen käytettävyys ennen päivitystä. 64 KiB:n prefix yksin **ei riitä** palauttamaan sovellusta, asetuksia ja molempia OTA-osioita.
5. Vasta nimenomaisen hyväksynnän jälkeen saadaan aloittaa laitekohtainen F02-testitoteutus. Testikortti ei saa olla kytkettynä oikean pumpun kirjoittavaan ohjauspolkuun.

## F02-laitehyväksyntä, kun laiteoperaatio on erikseen hyväksytty

- Testaa oikealla, BLE-kantamalla olevalla RuuviTagilla.
- Mittaa **todelliset** skannauskierrokset ja näytteiden ikä, `low_heap_scan_skips`, sisäisen RAMin vapaa/suurin lohko, PSRAM, TLS+Wi-Fi, paikallisen HTTP:n latenssit, reset-reason ja uptime.
- Erottele anturin poissaolo, BLE-starvation, Wi-Fi-katko ja firmware-reset; älä selitä niitä samoilla oireilla.
- Ei uusia watchdogeja, reset-silmukoita eikä automaattisia palautuksia.
- Lämmityspumpulle tai Shellyn releelle ei lähetetä komentoja.

## Tämän työn havainto

Vanhan C2.3:n kerätyssä lokissa näkyi 14 uptime-pudotusta 16.11.2026 klo 16:03–18:33 UTC. Näytteissä ei ole `esp_reset_reason()`-arvoa tai ESP32:n boot-tekstiä; syy on **vahvistamatta**. Core-koneen Linux-kernel kirjasi CH341:n USB-irrotuksen 16.11.2026 klo 20:08:54 paikallista aikaa ja uuden kytkennän 21:48:44. Ne eivät todista 14 resetin juurisyytä.

## Kokonaisen flashin varmuuskopio – hyväksyntä edelleen avoin

Kahden suoritetun lukuoperaation tulos on dokumentoitu tämän tiedoston alussa. Molemmat olivat tiedonsiirron osalta **FAIL** eikä täyttä kuvaa ole. Se, että vanhojen 64 KiB:n prefiksien osiotaulut ovat MD5-eheitä, ei todista mikä niistä on nykyisessä kortissa.

Seuraavaa firmwareasennusta varten tarvitaan ehjä, salassa pidetty koko flash-kuva, sen SHA-256, tästä kuvasta tulkittu todellinen osiotaulu, NVS/otadata/bootloader sekä erillinen palautusmenettely ja nimenomainen asennuslupa. Pelkkä lukuoperaation hyväksyntä **ei** anna lupaa kirjoittaa firmwarea tai edes kokeilla palautusta kirjoittamalla.

## Käyttöajan nollaantumisten jatkodiagnostiikka

14 pudotusta tallentui **16.11. klo 16:03–18:33 UTC**, eli **18:03–20:33 Suomen aikaa**. Linuxin USB-irrotus klo **20:08 Suomen aikaa** (18:08 UTC) osuu tämän aikavälin sisään, mutta ei yksilöi kaikkien 14 pudotuksen syytä. Vanha näyteloki ei sisältänyt ESP32:n reset reason -tietoa tai alkuperäistä boot-tekstiä. Uuteen Lite-ehdokkaaseen lisätään vain kerran käynnistyessä luettava `esp_reset_reason()` ja read-only `boot_reset_reason_code`-API-kenttä; se ei muuta käynnistymistä, OTA:a tai watchdogia. Historiallista syytä tämä muutos ei voi jälkikäteen todistaa.

**Tulos 17.11.2026:** laiteyksilö ja flashin todellinen koko vahvistettu; kaksi kokolukua FAIL; täydellinen varmuuskopio, todellinen osiotaulu ja palautussuunnitelman todentaminen edelleen BLOCKED. F02:n fyysinen firmwareasennus odottaa erillistä hyväksyntää.
