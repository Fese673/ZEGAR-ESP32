# ENS160 + AHT21 Menu Value Layout

## Opis zmian w UI

Sekcja `AHT21 + ENS160` została dopasowana do wzorca używanego w `PMS5003`.

- lista menu ENS160 pokazuje teraz bieżące wartości już na poziomie menu,
- kliknięcie w pozycję nadal otwiera ekran szczegółowy,
- ekrany szczegółowe mają układ zgodny z PMS5003:
  - tytuł parametru,
  - wiersz `Biezaca: ...`,
  - wiersz `Min: ... Max: ...` dla parametrów liczbowych,
  - wiersz `Dlugi -> Powrot`.

## Zmodyfikowane elementy menu

W `AHT21 + ENS160` zmieniono sposób renderowania pozycji:

- `AQI` pokazuje bieżące AQI już w menu,
- `TVOC` pokazuje bieżące TVOC już w menu,
- `eCO2` pokazuje bieżące eCO2 już w menu,
- `Temp` pokazuje bieżącą temperaturę już w menu,
- `Hum` pokazuje bieżącą wilgotność już w menu,
- `Status` pokazuje bieżący stan sensora już w menu.

Menu pozostaje paginowane tak jak inne submenu 20x4 i aktualizuje się dynamicznie wraz z nowymi danymi.

## Zgodność z PMS5003

Zmiana zachowuje wzorzec PMS5003:

- wartości są widoczne już w liście,
- szczegóły są otwierane kliknięciem,
- długie przytrzymanie wraca do menu ENS160,
- styl ekranów szczegółowych jest zgodny z już istniejącym UI projektu.

## Zakres zmian

- zmieniono tylko warstwę prezentacji w `src/UI_Draw.cpp`,
- nie zmieniano logiki sensora,
- nie zmieniano `runtimeData`,
- nie zmieniano systemu nawigacji enkoderem,
- projekt kompiluje się poprawnie po zmianach.