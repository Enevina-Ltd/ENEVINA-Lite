# Enevina Lite C2.5 – Premium Dashboard (16.11.2026)

## Tila

**Kehityshaara / draft. Ei laitehyväksyntää, ei automaattista lämmitysohjausta.**
Tämä PR perustuu C2.4:n PR #7:ään. PR #5–#7 ovat edelleen avoimia
luonnoksia ja fyysiset C2.4-turvaportit koskevat myös C2.5:tä.

## Toteutettu

- Paikallinen, suomenkielinen, responsiivinen premium-dashboard (/).
  Tyylit ja JavaScript ovat itse laitteen flashissa (PROGMEM), ei CDN-,
  internet-, Home Assistant-, tietokanta- tai pilviriippuvuuksia.
- Etusivu: **vain tuoreet** sisäilman RuuviTag-arvot, kosteustieto, kastepiste,
  mitattu Shelly-teho, ulkolämpötila/ennuste, nykyinen spot, kokonaishinnan
  arvio sekä hyväksytyn MPC-suunnitelman tilanne.
- Hintanäkymä: 15 minuutin spot-hintojen pylväät, edullisin saatavilla
  oleva jakso ja 48 seuraavan vartin hintataulukko.
- Optimointi: hyväksyttyjen 15 minuutin HEAT 16 / OFF -**ehdotusten**
  aikataulu, kustannus ja lämpötilaennuste. Ei hyväksyttyä suunnitelmaa →
  ei aikataulua, ei keksittyä ennustetta.
- Asetukset: kolme C2.3:n **laskennallista** tilaa, niiden paikallinen
  tallennus olemassa olevaan /api/air/config-rajapintaan
  (CSRF-tokenin tarkistus säilyy); laitetilan diagnoosit ja RuuviTagin
  lämpötilahistoria.
- Alkuperäinen tekninen näkymä säilytetty muuttamattomana osoitteessa
  /diagnostics.
- Uusi **vain luku** /api/prices tarkistaa luotettavan kellon ja kuluvan
  hintajakson, käyttää jo hyväksyttyä Nord Pool -välimuistia, laskee
  jokaisen jakson kokonaishinnan olemassa olevilla tariffeilla ja Suomen
  paikallisella ajalla sekä virtauttaa JSONin pienissä osissa.
  192 hintajakson väliaikainen taulukko varataan vain PSRAMista.

## Turvarajat

- Dashboardissa näkyy aina "VARJOTILA". Ohjelma **ei lähetä**
  CN105-, IR- tai Shelly-käskyjä; asetukset ovat vain laskennallisia.
- C2.4:n ennestään estämä /api/plan-hyväksyntä ja oletuksena estetty
  synteettinen lämpömalli jäävät ennalleen. **Älä** näytä simulaatiota
  käytännön ohjaussuunnitelmana.
- Vanhentuneesta, puuttuvasta tai epämääräisestä hinnasta ei tehdä
  datapistettä. Hintojen verollisuus näytetään selvästi.
- UI ei altista OTA-latausta. Secure Boot, Flash Encryption, paikallinen
  hallinnan todentaminen, OTA-allekirjoitukset/rollback ja tuotteen
  radiovaatimustenmukaisuus ovat erillisiä keskeneräisiä portteja.
- Nord Pool -datan kaupallinen käyttöoikeus tarkistettava ennen myyntiä.

## Testaus ja seuraavat portit

Uusi lähdetesti: python -m unittest discover -s tests -p test_premium_dashboard_source.py

**Ei vielä tässä PR:ssä suoritettu:** varsinainen PlatformIO-käännös,
selaimen automaattinen testi, fyysisen ESP32:n muisti-/CPU-mittaus
premium-sivua ladattaessa, 24–48 tunnin pitkäaikainen Wi-Fi + BLE +
CN105 + Nord Pool + FMI + MPC -rinnakkaistesti. Ei väitetä läpäistyiksi.

Ennen mergeä:
1. Testaa käännös ja regressiot; varmista PSRAMin vapaa reservi suuren
   sivu- ja hintapyynnön aikana.
2. Selaimessa tarkista 320 px, 800 × 480 px ja työpöytä sekä tyhjän
   anturin, vanhentuneen hinnan, verkkokatkon ja kesä-/talviajan tilanteet.
3. Korjaa ja todenna C2.4:n sidotun RuuviTagin tuoreus ja CN105:n
   lähdeaikaleimat, OTA ja muisti sekä fyysinen vakaus.
4. Lisää erillinen todennettu hallinnan autentikointi ja CSRF-käytäntö
   ennen kaupallista julkaisua.
