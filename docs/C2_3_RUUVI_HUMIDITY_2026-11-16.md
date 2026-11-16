# ENEVINA Lite C2.3 — RuuviTag, huoneilma ja kuivanapidon varjotila

**16.11.2026. Toteutushaara:** `feat/cottage-c23-ruuvi-dry-20261116`, jatkaa C2.2:n PR #5:tä. **Tila:** PC- ja cross-compile-toteutus, **fyysinen C2.3 BLE + Wi-Fi -koe avoin**.

## Kohde, rajat ja turvallisuus

Pihamökki on Lillevilla Kalajoki (~17,6 m²), 44 mm hirsiseinä, hirren sisäpuolella suoraan 100 mm Hunton-puukuitueriste, ilmansulkupaperi ja puukuitulevy, katossa 130 mm SPU, lattiassa 200 mm eristettä, ulkopinnassa Virtasen 4 Öljyn Maali. Ei vesiputkia, eikä suunniteltua ilmanvaihtoa. **Kosteuden kulkua rakenteissa, ilmavuotoja tai kylmien rajapintojen lämpötiloja ei ole mitattu.** Yksittäinen huoneilma-anturi ei todista rakenteen turvallisuutta eikä sitä, tarvitaanko tuuletusta. Säännöllinen tarkastus ja erillinen kosteustekninen arvio ovat eri asia kuin ohjelmiston RH-varoitukset.

Mitsubishi Electric MSZ-FD25VA / MUZ-FD25VA:n normaali pienin lämmitysasetus on +16 °C. `+12 °C`-ylläpitotavoite ja `ulkoilma +3–5 °C` -kuivanapitokokeilu ovat **vain laskennallisia tavoitteita**. C2.3 ei kytke lämpöpumppua, lähetä CN105- tai IR-komentoja, aseta alle 16 °C:n lämmityspyyntiä eikä katko Shellyn relettä. Mitään alle 16 °C:n tavoitetta ei yritetä toteuttaa toistuvalla HEAT/OFF-kytkennällä.

## RuuviTag RAWv2 ja sidonta

Protokollalähde ja testivektori: [Ruuvin virallinen Data Format 5 (RAWv2)](https://docs.ruuvi.com/communication/bluetooth-advertisements/data-format-5-rawv2).

- `cottage/ruuvi_v5.*` purkaa tarkasti 26 tavun manufacturer-datan: little-endian valmistajatunnus `99 04` (0x0499), formaatti 5, signed lämpötila ×0,005 °C, unsigned RH ×0,0025 %, paine 50000 Pa + raakaarvo, pariston 11 bittiä, TX-teho, liikelaskuri, 16-bittinen mittaussekvenssi ja mainosteen 6-tavuinen sisäinen MAC.
- Tarkistaa pituuden, 0x8000/0xFFFF-sentinelit, epätodennäköiset lämpötilat ja RH-arvot, paristojännitteen sekä MAC-kentän; hylkää vieraat, vanhentuneet, toistetut ja suuria äkkihyppyjä sisältävät pakettitiedot. Duplicate-paketti **ei päivitä mittauksen ikää**.
- BLE toteutetaan C2:n jo käytössä olevalla **Arduino-ESP32:n ESP-IDF BLE-kirjastolla**, ei lisäämällä vielä kokeilematonta NimBLE-riippuvuutta. NimBLE-siirtymä voidaan mitata erikseen RAM/Wi-Fi-rinnakkaiskuormalla.
- Firmware `cottage_air_service.cpp` käynnistää BLE-skannauksen **myös ilman Wi-Fiä tai etukäteen sidottua anturia**. Se havaitsee läheltä RAWv2-anturin paikalliseen näytölle, mutta **ei sido ensimmäistä löytämäänsä anturia automaattisesti**. Sisäanturi pitää valita ja varmistaa oikeaksi; valinnainen toinen ulko-RuuviTag voidaan sitoa erikseen.
- Anturi sidotaan paikallisesta mobiilinäkymästä anturin sisäiseen MAC-arvoon; sidonta ja kolme tilaa tallentuvat ESP32:n `Preferences`/NVS-muistiin namespaceen `cottage_air`. Jatkossakaan tunnistetta ei tallenneta GitHubiin. Konfiguraatiotiedoston vaihtoehto: `COTTAGE_RUUVI_MAC` `cottage_local_config.hpp`-tiedostossa (gitignore).
- C2.3 käyttää sekvenssilaskuria ja monotoniasta mittausikää; ilman synkronoitua UTC-kelloa BLE + RH- ja kastepistelaskenta toimivat silti, mutta EPC/MPC-ennustelaskelma pysyy turvallisesti pois käytöstä.

**Näin otat anturin käyttöön, kun se on fyysisesti mökissä:** anna C2:lle paikallinen Wi-Fi-konfiguraatio erilliseen `firmware/cottage/include/cottage_local_config.hpp`-tiedostoon ja testaa laite **vain erillisellä ESP32-S3-testikortilla** (ei toimivalla CN105-sillalla). Avaa testikortin `/` lähiverkosta, tarkista `Läheltä löytynyt RuuviTag` ja vertaa tunniste fyysisesti oikeaan anturiin. Valitse `Mökin sisäilma` ja paina `Sido anturi varjotilaan`. Käynnistä kortti uudelleen ja vahvista sama MAC ja uusien sekvenssien tuoreus. Ilman Wi-Fiä voit esisitoa anturin käännösaikaisesti paikallisella konfiguraatiolla ja tarkistaa myöhemmin lokit. Toisen, ulkona säältä suojassa olevan RuuviTagin voi sitoa kohtaan `Vapaaehtoinen ulkoilma`. Älä sijoita sisäanturia lämpöpumpun puhallusilmavirtaan; asenna myös rakenteiden kylmiin kulmiin tarvittaessa erillisiä mittauksia.

**MAC ei ole kryptografinen todennus:** RAWv2 on salaamaton BLE-mainos. Sitominen estää tavalliset muiden antureiden sekaantumiset muttei tarkoituksellista lähetyksen väärentämistä.

## Kosteuden suureet ja pitkäaikaisseuranta

`cottage/air_humidity.*` laskee Magnus-yhtälöllä vesihöyryn osapaineen, kastepisteen ja absoluuttisen kosteuden g/m³. Laskennassa käytetään mitattua huonelämpötilaa ja RH:ta, ei ulkolämpötilasta oletettua RH-arvoa. Ulko-RuuviTagin luotettavalla RH-mittauksella nähdään sisä-/ulkoabsoluuttikosteuden ero ja viitteellinen tuuletuksen kuivatuspotentiaali. Jos vain FMI:n ulkolämpötilaennuste on saatavilla, se kelpaa kuivanapidon **lämpötilaeron** viitteeksi, mutta **ei** tuuletuksen kosteusvertailun lähteeksi.

- Kosteusvaroitukset: oletuksena RH vähintään 70 % noin 6 h yhtäjaksoisesti, RH vähintään 80 % noin 24 h lämpötilan ollessa plussalla; 10 min mittauskatko, alle 0 °C sekä heikko paristo (oletusarvo 2,50 V). Rajoja voi muuttaa paikallisesta käyttöliittymästä (RH70 55–80 %, RH80 75–95 % ja vähintään RH70 +5 %, paristo 2,0–3,2 V).
- **Ei lämmitysohjausta hälytyksestä.** Mittausväli, hystereesi ja hälytyksen kuittaus erotetaan toisistaan; 24 h altistus ei "jatku" automaattisesti sähkönkatkon tai yli 10 min BLE-katkon yli.
- Tuntihistoria: 168 viimeistä **tuntipistettä** (7 päivää) RAM-renkaassa, lämpötila, RH, kastepiste ja absoluuttinen kosteus kvantisoituina. 6 h RH-/absoluuttikosteustrendi näytetään, kun riittävä historia on kertynyt. Tallennus NVS:n `cottage_hist/hourly_v1`-blobiksi **enintään kerran 6 h:ssa**, version ja CRC32-tarkistussumman kanssa. Tämä vähentää flash-kulumista mutta viimeiset 6 h voivat kadota sähkökatkossa.
- Jos UTC-aikaa ei ole, kerätään **vain RAMiin bootista laskettua suhteellista historiaa**. Se näkyy käyttöliittymässä erikseen eikä sitä tallenneta vääränä UTC-historiana. Kun kello myöhemmin synkronoituu, uusi UTC-sarja alkaa. Tuntihistorian NVS-palautus vahvistetaan tarkistussummalla.
- Hälytyshistoria on toistaiseksi **vain RAMissa** (viimeiset 48 tapahtumaa); kuittaukset eivät säily virrankatkon yli. Tämä on näkyvä rajoitus, ei väite pysyvästä hälytysarkistosta.

## Laskennalliset tilat

| Tila | Toteutus C2.3 | Fyysiset komennot |
|---|---|---|
| **Kylmillään** | Ei lämmityspyyntöä; BLE-mittaus, historia, kosteus-/kylmyysvaroitukset toimivat ilman internetiä | **Ei yhtään**; pumpun oikeaa tilaa ei muuteta |
| **Kuivanapito** | Ehdollinen RH:n pitkäaikaisseuranta, hystereesi ja viitteellinen `ulkoilma + 4 °C` (säädettävä 3–5 °C) -tavoite vain kelvollisesta ulkolämpötilasta. Ei lämpöpyyntöä yhden RH-piikin perusteella | **Ei yhtään**. Alle 16 °C:n tavoite näytetään toteuttamattomaksi suoralla Mitsubishi-pyynnillä |
| **Ylläpitolämpö** | +12 °C oletus, käyttäjän muutettavissa 5–24 °C; ilmoittaa laskennallisen lämmitystarpeen ja 16 °C minimiasetuksen | **Ei yhtään**. Suoran asetusarvoalueen *ylärajaa* ei ole vahvistettu |

Käyttäjä voi vaihtaa tilaa, asetettua tavoitetta, varoitusrajoja ja RuuviTag-sidontaa saman Lite-laitteen paikallisella `/api/air/*`-rajapinnalla. Vain nämä **laitteen omat NVS-asetukset** ovat kirjoitettavia; varjotilan laitekirjoituspolkua ei ole. Asetussivulla käytetään uudelleenkäynnistyksessä vaihtuvaa CSRF-tunnistetta. Se **ei korvaa** verkon käyttöoikeussuojauksen testausta eikä anna lupaa julkaista käyttöliittymää internetiin.

## Testaus ja laiteportti

- PC C++17 `cottage_climate_tests`: viralliset valid/invalid-RAWv2-testivektorit, valmistajatunnus ja pituus, lämpötila/RH/paine/paristo, MAC-sidonta, duplicate/old sequence/epäuskottavat hypyt, jäätymisalue, kastepiste ja abs RH, historialoki/CRC, offline-historia, hälytyshystereesi, 6/24 h kynnys, anturikatko ja kaikki 3 toimintatilaa, myös kielletyt alle +16 °C -tavoitteet.
- Native `ctest` Debug/Release ja Python-lähdetestit tarkistetaan ennen committia. `pio run -d firmware/cottage -e esp32s3_cottage_shadow` ristiinkääntää saman firmwaren BLE + Wi-Fi + aiempi C2.
- Tämä työ **ei flashaa ESP32-korttia uudelleen nyt**, koska C2.2:n todellinen 24 tunnin vakauskeruu on yhä käynnissä erillisellä kortilla. Siksi **C2.3:n fyysistä BLE-mainosta, Wi-Fi-rinnakkaiskuormaa, NVS-palautumista tai pitkää kosteusseurantaa EI ole vielä mitattu**. RuuviTagille ei ole vahvistettua mökkikohtaista tunnistetta eikä lukemia. Fyysisen kortin C2.2-lukemat eivät ole C2.3-todiste.

## Avoimet hyväksyntäesteet

1. Anturi hankittava/asennettava mökkiin ja tunniste sidottava fyysisesti varmasti, kalibroitava vertailumittauksella.
2. Todellinen ESP32-S3:n BLE+Wi-Fi+CN105+Shelly+FMI/Nord Pool+UI-yhteistesti ja RAM/stack/WDT, BLE-katko ja uudelleenkäynnistyksen NVS-palautustesti.
3. C2.2:n jatkuva 24 h seuranta on vielä valmistumatta; C2.3:lle tarvitaan myöhemmin oma yhtäjaksoinen testi.
4. Rakenteiden kosteus-/kondensaatiokäyttäytyminen ja ilmanvaihdon tarve eivät selviä yhdestä huonelämpötila-/RH-mittauksesta.
5. Rakennusmalli ja pumpun hyötysuhde ovat kalibroimatta; riippumaton pakkassuoja puuttuu. **C3-ohjaus estetty.**
6. C2.3:n käyttöönotto ei muuta C2.2:n olemassa olevan GitHub PR #5:n hyväksyntää. Uusi kehitys säilytetään erillisessä draft-PR:ssä.

**Laitteen fyysiset kirjoitukset pysyvät poissa C2.3:sta.**
