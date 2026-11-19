# Enevina Lite — alkuperäisen valmistaja-Wi-Fin yhdistäminen (pilvetön LAN)

**Tutkimusohje 19.11.2026; mallikohtainen fyysinen yhteensopivuus ei ole vahvistettu.** Liten Midea V2/V3 -tilanlukusovittimet on jo toteutettu lähdekoodiin mutta ei kenttävarmennettu. Muut tässä kuvatut Wi-Fi-sovittimet ovat referenssitutkimusta, eivät Enevina Liten toimivia ajureita. Nämä ovat ulkoisten HA-/harrastajaprojektien dokumentoituja testipolkuja oman C++/ESP32-S3-toteutuksen kehitykseen. **Yksi Lite → yksi pumppu.** Ei HA:ta, erillistä ohjainta, relettä tai IR-lähetintä valmiin Lite-tuotteen käytössä.

## Yleinen yhteyspolku

1. Tarkista pumpusta **sisäyksikkö, Wi-Fi-moduulin malli, moduulin firmware ja käytetty sovellus**. Pelkkä kyltti "Wi-Fi" tai merkin nimi ei riitä.
2. Wi-Fi-moduulin on oltava yhteydessä **omaan 2,4 GHz lähiverkkoon**. Lite ja pumppu saavat puhua toistensa kanssa: client isolation pois tältä yhteydeltä; VLAN-palomuuri avaa vain tarvittavat portit. Anna pumpulle tarvittaessa **DHCP-varaus**.
3. Löydä IP reitittimen DHCP-listalta; aloita **valmistajakohtaisella read-only-discoveryllä**, ei sokkona tehtävillä ohjauskomentokokeilla.
4. Selvitä laitteen **protokollaversio**, mahdollinen salasana, token/key, sertifikaatti ja niiden alkuperä (LAN vai pilvi). Tunnukset pidetään vain paikallisessa salatussa/turvallisesti rajatussa laitesäilössä, ei Gitissä eikä UI-lokissa.
5. Testaa **LAN-tilaluku internetin ollessa katkaistu pumpulta** (ei pumppuun menevän WLANin katkaisua). Jos onnistuu vain jo aiemmin haetulla tokenilla, merkitse **L1**, ei aidosti pilvetöntä L0:aa.
6. Enevina-ajuri: discovery, tunnistus, salattu paikallinen read-only-tilakysely, kuittausten/parserin virheet, tuoreusaika ja offline-palautuminen. Kirjoitukset ovat erillinen, myöhempi mallikohtainen hyväksyntävaihe.

## Malliperhekohtainen käytännön polku

### 1. Midea (kellarin pumppu on ensisijainen pilotti)

- **Edellytys:** Pumpussa on Midea M-Smart -protokollaa puhuva alkuperäinen Wi-Fi-moduuli. Esim. NetHome Plus / MSmartHome / Midea Air -sovelluksen käyttäminen **ei yksin** todista tukea.
- **Read-only testityökalu:** `msmart-ng` voidaan asentaa testi-PC:lle, mutta **älä käytä `msmart-ng discover` -CLI-komentoa ilman pilviautentikoinnin erillistä hyväksyntää**: se voi Midea V3:lla hakea NetHome Plus -token/key-parin automaattisesti. Käytä sen sijaan [kellarin pilotin korjattua pilvetöntä löytömenettelyä](MIDEA_CELLAR_PILOT.md#vaihe-b--vain-luku-pcllä), jossa kutsutaan `Discover.discover(auto_connect=False)`. Se selvittää lähiverkon laitetyypin/protokollan ilman V3-automaattiautentikointia. Pilviavainhaku sallitaan vain käyttäjän nimenomaisella luvalla. Työkalu on vain PC:n testiapu, ei osa Liten firmwarea.
- **Verkko:** discovery yleensä UDP 6445 (joissakin 20086), varsinainen AC LAN TCP 6444. Laite on samassa L2/WLANissa discoveryyn; staattinen IP ja reititetty unicast voidaan tutkia myöhemmin.
- **Tunnistus:** odota tyypiksi `0xAC` ja `supported=true`. Kirjaa vain sensuroitu laitetiedon kooste. V1/V2 eivät lähteiden mukaan tarvitse pilvitokenia; V3:ssa `token` + `key` vaaditaan normaalisti Midea-pilvestä (kerran, avain voi myöhemmin vanheta).
- **Pilvetön käyttöönotto:** V1/V2:n koko WLAN-provisionointi on erikseen testattava; V3:n **ensimmäisen token/keyn pilvetön haku on ratkaisematta**. V3 kelpaa aluksi vain L1-tutkimukseen, ei pilvettömän tuotteen L0-lupaukseen.
- **Älä aja** ohjelmaesimerkkien `set_target_temperature`, `set_swing`, `toggle` tai `setattr` kutsuja testivaiheessa.
- Lähteet: [msmart-ng-esimerkki](https://github.com/Tofee/midea-ac-HA-py#-getting-device-info), [ioBroker.midea protokollat V1–V3](https://github.com/TA2k/ioBroker.midea), [midea-local](https://github.com/midea-lan/midea-local), [Midea Local TypeScript](https://github.com/Torsten85/midea-local).
- Tarkka erillinen käyttö- ja näyttölomake: [MIDEA_CELLAR_PILOT.md](MIDEA_CELLAR_PILOT.md).

### 2. Gree / EWPE Smart ja mallikohtaiset OEM:t

- Etsi alkuperäisen WLANin sovellukseksi **EWPE Smart** tai **GREE+**, tarkista itse moduuli; sama merkki voi käyttää muuta protokollaa.
- Laite on jo WLANissa tai tehdään **moduulikohtaisesti testattu paikallinen provisioning**. [gree-ac](https://github.com/georgevgs/gree-ac) näyttää pilvettömän bindin ja AES-ECB/AES-GCM-transportin.
- Local discovery ja tilaluku **UDP 7000**. [EWPE Smart HA](https://github.com/anaryk/ewpe-smart-ha) näyttää paikallisen IP-pohjaisen polun. Varmista laitekohtaisesti, mitä eri komennot raportoivat; kaikkia ominaisuuksia ei löydy kaikista pumpuista.
- Viite-esimerkki: Toyotomi Umi UTN/UTG-12CH. Muut merkit (**Gree, Daitsu, Sinclair, Cooper&Hunter, EcoAir, ProKlima, Tosot**) ovat **ehdokkaita**, eivät automaattisesti testattuja malleja.

### 3. Daikin Local API

- Selvitä ensin **adapterin tunnus**: BRP069A/B; BRP072A42/A43; BRP084Cxx; muut erikseen. **Älä** oleta että Onecta-tili tai Wi-Fi tarkoittaa paikallis-API:n saatavuutta.
- [Home Assistantin Daikin-integraatio](https://www.home-assistant.io/integrations/daikin/) antaa malliesimerkit, tukimatriisin, tarvittaessa API-avaimen/salasanan ja verkon discovery-portit UDP 30000/30050.
- Vanhoissa sovittimissa paikallinen **HTTP-API**; uusille tietyillä firmwareilla **REST/JSON** ([BRP069C4x-tutkimus](https://github.com/daimik/Daikin-BRP069C4x-Local-API)). Jos API ei vastaa, älä muuta firmwarea sokkona. Avoimet ristiriidat kirjataan matriisiin.
- Vasta paikallisen tilaluvun, API:n aitouden ja internet-estotestin jälkeen protokolla voidaan sovittaa ESP-IDF:ään.

### 4. Mitsubishi Heavy Industries WF-RAC

- Eri valmistaja kuin **Mitsubishi Electric**. Vain Smart M-Air / **WF-RAC**, **WF-RAC-HTTPS** tai **WCBN4612L**.
- Pumpun valmistajan Wi-Fi-moduuli liittyy omaan WLANiin. Etsi DHCP:stä tai mDNS-palvelusta `_beaver._tcp`; yhteys moduulin paikalliseen HTTP(S)-API:in **portissa 51443**.
- Lähde [WF-RAC HA](https://github.com/blues-sechseck/Mitsubishi-WF-RAC-Integration); itsenäinen LAN-esimerkki [AirLAN](https://github.com/erdtsieck/airlan). Laitteen käyttäjä-/write-lock-käytös tutkittava ennen komentoja.
- Tunnettu harrastajamallisarja SRK20ZS-WF / SRK35ZS-WF / SCM45ZS-W; ei Enevina-hyväksyntää.

### 5. Haier SmartAir2 KZW-W002

- Vain **KZW-W002 / MK-QTWIFI-03**, ei kaikkia Haier hOn -malleja.
- Täysin paikallisen ensiliitoksen polku [haier-kzw-w002-wifi-tool](https://github.com/BaptGosse/haier-kzw-w002-wifi-tool): moduulin **jo valmiiksi paritustilassa** lähettämä `U-AC` / `U-AIC` / `Haier-uAC` -SoftAP, paikallinen portti **192.168.0.1:56797**; wlan-asetus löytyy projektin ohjeista. **Paritus muuttaa pumpun WLAN-asetuksia**, joten älä käynnistä sitä ilman erillistä laitekohtaista hyväksyntää.
- WLANin jälkeen paikallinen tilaluku ja ohjaus **TCP 56800**, esim. [haier-kzw-w002-local](https://github.com/ddnlv/haier-kzw-w002-local). Lähdemalli HSU-09HNH103/R2.

### 6. Hisense Ayla / osa Fujitsu FGLair

- Tarkista moduuli: **AEH-W4B1 / AEH-W4E1**; AEH-W4A1 ei välttämättä toimi. **ConnectLife ei ole sama integraatio**.
- [Hisense Ayla HA](https://github.com/szupi-ipuzs/hassio-hacs-hisense-aircon) käyttää paikallisia salattuja Ayla LAN -viestejä ja moduulin HTTP-callbackia. Integraatio hakee avaimen yleensä valmistajan pilvestä tai hyväksyy manuaalisesti syötetyn LAN-avaimen. Tämä ei todista pilvetöntä avainprovisionointia.
- ESP32-S3-adapterin callback-palvelimen portit ja salaus on toteutettava referenssin protokollan, ei oletettujen porttien perusteella.

### 7. AUX / BroadLink DNA

- Vain BroadLink-moduulia käyttävä **AUX**, laitetunniste **0x4e2a**. Paikallinen UDP **80**, AES-128-CBC; lähde [AUX LAN](https://github.com/renzo-ezagui/homelab-ha-aux-lan).
- Varmista Wi-Fi-moduulin tunnistus read-only ennen minkään muun AUX-mallin hyväksymistä. Tuya-/muut AUX-variaatiot voivat olla eri protokollia.

### 8. Samsung SmartThings / LocalThings

- [LocalThings](https://github.com/mbillow/localthings) toteuttaa osalle laitteista paikallisen CoAP/DTLS-polun; osa vanhoista moduuleista käyttää **TCP 8888**.
- **Huom:** ensimmäinen käyttö voi hakea julkisen tunnisteen Samsung-palvelimelta, osa laitteista tarvitsee sertifikaatin tai tokenin. Pilvetöntä ensiliitosta ja yksittäistä AC-mallia ei ole vielä todistettu. L1/L?, ei Enevina L0.

## Liitäntä Enevina Liten arkkitehtuuriin (suunnittelun ehdot)

- `PumpAdapter` toteuttaa valmistajakohtaiset `discover`, `connect`, `read_status`, `capabilities`, `source_timestamp` ja vasta myöhemmässä vaiheessa `write_command` / todellisen kuittauksen.
- Tilalämmityksen anturitiedot voivat puuttua tai olla arvioita: älä väitä kompressorin tehoa, COPia, sulatusta tai energiamittaria, ellei juuri tämä malli välitä niitä.
- `Supervisor` omistaa kaiken kirjoitusluvan. Ohjausta ei saa aktivoida pelkällä discovery- tai HA-protokollalöydöllä. Älä ohita pumpun omia sulatus-/jäätymis-/kompressorisuojia.
- Salaukseen mallikohtaiset testivektorit; pidä Wi-Fi-salaisuudet ja protokollan avaimet pois debug-lokeista. HTTP-/UDP-palveluja ei avata suoraan internetiin.
- Liten hinnan ja sään verkkohaku ei ole sama kuin **pumpun ohjaus**: viimeksi tallennetuilla ennusteilla varautuminen internetkatkoon on eri kysymys kuin valmistajan pilvettömyys.

Laitetukimatriisi: [LOCAL_WIFI_COMPATIBILITY.md](LOCAL_WIFI_COMPATIBILITY.md).
