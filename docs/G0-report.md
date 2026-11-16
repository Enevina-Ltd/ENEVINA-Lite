# G0: PC-mallin alustava vertailu (2026-11-15)

**Tulos ei ole tuotantohyväksyntä.** Synteettisissä, toistettavissa kokeissa Lite pysyi annetuissa huone- ja käyttövesirajoissa. Mallin parametreja ei ole kalibroitu oikeasta rakennuksesta tai pumpusta, joten luvut eivät osoita toteutuvia säästöjä.

## Menetelmä

`tools/reference_benchmark.py` muodostaa lievän, kylmän ja patterilämmityksen menoveden rajoittaman tilanteen 24, 48, 96 ja 192 neljännestunnin jaksolle. Kolmas tapaus käyttää erilaista pumpun tehoa, COP-parametreja ja vaihtelevaa hintaprofiilia. Jokaisessa tapauksessa neljä menetelmää käyttää samoja lähtötiloja, rakennuksen ja varaajan energiataseita, COP-yhtälöä, hintasarjaa, rajoja ja vaatimusta palauttaa loppulämpötila ja -varaustaso vähintään lähtötasolle.

- **Lite:** C++:n rajattu eteenpäin etenevä dynaaminen haku yhdistää lähes samat tilat ja tarkistaa lopullisen aikataulun fysiikkamallilla. Alkuperäisessä G0-ajossa mukana oli myös taaksepäin etenevä haku, mutta sen poistaminen säilytti kaikkien 12 tapauksen kustannukset viiden desimaalin tarkkuudella ja nopeutti ESP-mittausta.
- **MILP:** Pythonin SciPy/HiGHS ratkaisee erillisen binaaristen toimintatilojen mallin. Jokainen ratkaisu simuloidaan uudelleen Pythonin fysiikkamallilla.
- **Sääkompensoitu perusohjaus:** menoveden tavoite nousee ulkolämpötilan laskiessa; huoneen ja käyttöveden lähtötavoitteita ylläpidetään ilman hintaennakointia. Tämä on yksinkertaistettu vertailu, ei Panasonicin todellisen säätimen mittaus.
- **Hintaraja:** sama välitön mukavuuden ylläpito, mutta alle 0,15 €/kWh jaksoilla huonetta ja varaajaa esilämmitetään kiinteään puskuriin. Tämä on yksinkertainen hintarajaohjaus, ei MPC.

MILP:n `solver_status=1` tarkoittaa 60 sekunnin aikarajaa. Taulukon MILP-luku on silloin paras löydetty *kelvollinen ratkaisu*, ei todistettu optimi. Esitetty prosenttiero on Lite-kustannus suhteessa tähän MILP-ratkaisuun. Täysi koneellisesti luettava [aineisto](benchmarks/g0-windows.json) sisältää sähkö- ja lämpöenergiat, COP:t, mukavuusminimit, loppuarvot, MILP:n ratkaisuvälin, suoritusajat ja muistin.

| Sää | Jaksot | Lite € | MILP € | Lite ero % | Sääkäyrä € | Hintaraja € | MILP-tila |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Lievä | 24 | 0,5038 | 0,5036 | 0,03 | 1,1291 | 0,967 | optimaalinen (0,1 % toleranssi) |
| Lievä | 48 | 2,1147 | 2,1059 | 0,42 | 3,3067 | 3,149 | optimaalinen (0,1 % toleranssi) |
| Lievä | 96 | 4,3420 | 4,3226 | 0,45 | 7,5738 | 7,183 | aikaraja; 2,44 % väli |
| Lievä | 192 | 7,9063 | 7,9042 | 0,03 | 15,0205 | 14,264 | aikaraja; 2,23 % väli |
| Kylmä | 24 | 1,4876 | 1,4858 | 0,12 | 2,3058 | 2,122 | optimaalinen (0,1 % toleranssi) |
| Kylmä | 48 | 5,7383 | 5,7374 | 0,02 | 6,8308 | 6,649 | aikaraja; 1,15 % väli |
| Kylmä | 96 | 12,4899 | 12,3979 | 0,74 | 15,2538 | 14,863 | aikaraja; 0,10 % väli |
| Kylmä | 192 | 24,5335 | 24,4775 | 0,23 | 30,2214 | 29,664 | aikaraja; 0,58 % väli |
| Patteri | 24 | 0,9103 | 0,8918 | 2,08 | 2,0694 | 1,933 | optimaalinen (0,1 % toleranssi) |
| Patteri | 48 | 2,7108 | 2,5872 | 4,78 | 4,8863 | 4,114 | optimaalinen (0,03 % toleranssi) |
| Patteri | 96 | 7,1497 | 6,8217 | 4,81 | 10,8827 | 9,790 | optimaalinen (0,1 % toleranssi) |
| Patteri | 192 | 13,7377 | 13,3227 | 3,12 | 21,9797 | 19,429 | aikaraja; 0,45 % väli |

Yksikään tämän aineiston Lite-tapaus ei hävinnyt sääkäyrälle tai hintarajalle. Patteritapauksessa ero MILP:hen oli 96 jaksolla 4,81 %, joten Lite ei aina saavuta vertailuratkaisua. Tutkimus ei kata kaikkia hintaprofiileja, laitteita tai rakennuksia, joten huonompia tapauksia ei voi sulkea pois.

Alkuperäisessä PC-ajossa C++-optimointi kesti 24 jaksolla noin 10–11 ms ja 192 jaksolla noin 113–172 ms. Erillisprosessin mitattu huippukäyttömuisti oli 5,3–5,9 MiB; tämä sisältää myös Windows-prosessin muun muistin eikä kuvaa ESP32:n sisäistä heapia tai PSRAMia. Uudistetun ytimen [ESP-sarjaporttimittaus](G1-report.md) täytti alle kahden sekunnin tavoitteen testatuissa tapauksissa, mutta samanaikainen verkkokuorma on vielä testaamatta.

## Jäljellä ennen G0-porttia

- Kalibroi rakennus, käyttövesivaraaja ja COP oikeilla mittauksilla; nykyiset luvut ovat synteettisen kokeen oletuksia.
- Varmista, että optimoidut lämpötehon toimintatilat voidaan toteuttaa käytännössä valitun Panasonic/HeishaMon-yhdistelmän sallituilla asetusarvoilla. Nykyinen malli ohjaa ideaalista lämpötehoa, jota laite ei ole luvannut tarjota.
- Lisää erilaisia hintaprofiileja, pumppujen teho- ja COP-käyriä, sulatus- ja minimikäyntiaikarajoja sekä vaikeita käyttövesitilanteita. Raportoi myös löydetyt tappiot ja epäkelvot tapaukset.
- Testaa nyt käännetyt Nord Pool -hinta- ja FMI-säähakijat sekä välimuistit fyysisellä Ethernetillä; kytke FMI:n testattu 15 minuutin interpolointi, sopimuskohtainen kokonaishinta ja turvallinen varatilan integraatio itsenäiseen optimointikäyttöön.

G1:n sarjaporttimittauksia on tehty, mutta G1:n verkkokuorma, G2:n laiteintegraatio ja G3:n tuotantotestit ovat tekemättä.
