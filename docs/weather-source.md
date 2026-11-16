# Ulkolämpötilan ennustelähde

Lite käyttää [Ilmatieteen laitoksen avointa WFS-rajapintaa](https://en.ilmatieteenlaitos.fi/open-data-manual-fmi-wfs-services?doAsUserLanguageId=en_US) ja HARMONIE-mallin `fmi::forecast::harmonie::surface::point::simple`-kyselyä. Sama kyselyperhe on käytössä Enevina Coren `fmi_wfs_client.py`-tiedostossa. Lite pyytää vain tuntikohtaisen `temperature`-parametrin annettuihin suomalaisiin WGS84-koordinaatteihin. Tunnusta ei tarvita. Sijaintia ei ole oletettu käyttäjän puolesta; tyhjä sijainti estää verkkokyselyn.

15.11.2026 tehty julkinen testikysely palautti HTTP 200:n, 50 tuntipistettä ja `BsWfs:BsWfsElement`-tietueet. Ohjelma tarkistaa jokaisen UTC-aikaleiman, lämpötilan järkevän rajan ja sarjan tunnittaisen jatkuvuuden. Hyväksytty sarja tallennetaan NVS-välimuistiin; samaa sisältöä ei kirjoiteta Flashiin toistuvasti. Yli 12 tuntia vanhaa ennustetta ei näytetä kelvollisena. Käyttöliittymä näyttää seuraavan ennustepisteen. Erillinen 15 minuutin interpolointifunktio hylkää puuttuvat alku- ja loppupisteet. **Sääsarjaa ei ole vielä kytketty optimointiin eikä fyysisen laitteen verkkohakua testattu.**

PC:llä ajettu sama C++-parseri hyväksyi myös 15.11.2026 tallennetun todellisen FMI-vastauksen (`ok=1`, 50 pistettä). Testi ei mittaa ESP:n verkkoyhteyttä.

Tietolähteen nimi näkyy käyttöliittymässä: Ilmatieteen laitos, CC BY 4.0. [FMI:n avoimen datan käyttöehdot](https://en.ilmatieteenlaitos.fi/open-data-manual) tulee säilyttää tuotteen dokumentaatiossa.
