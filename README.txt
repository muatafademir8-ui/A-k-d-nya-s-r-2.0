ACIK DUNYA SURUS - PSP PORTU (PPSSPP icin)

DERLEME (bilgisayarda, Docker ile):
  1) Bu klasoru ac, terminalde su komutu calistir:
     docker run --rm -v "$PWD":/src pspdev/pspdev bash -c "cd /src && make"
  2) Klasorde EBOOT.PBP olusur.

PPSSPP'DE CALISTIRMA:
  - PPSSPP uygulamasinda EBOOT.PBP dosyasini ac (Android'de dosya tarayicidan sec).
  - Ya da PPSSPP hafiza klasorunde PSP/GAME/AcikDunya/ icine EBOOT.PBP'yi koy.

KONTROLLER:
  Analog / D-pad sol-sag : direksiyon      X : gaz        Kare : fren / geri
  Daire : drift                             R : nitro      Ucgen : ucus ac/kapa
  Ucus modunda D-pad yukari / asagi : yuksel / alcal

NOT: Web surumunun sade bir portudur. Dahil: sonsuz sehir, drift, nitro,
ucus, trafik (carpinca arabalar ucar). Dahil degil: deniz/kopru, yeni sehir,
yayalar, ucak/helikopter, haber ekranlari, Lamborghini modeli, altinlar.
Kod PSPSDK uzerinde derleme testi yapilmadan yazildi; kucuk duzeltmeler gerekebilir.
