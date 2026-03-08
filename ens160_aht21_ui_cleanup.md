# ENS160 + AHT21 UI Cleanup

## Zmodyfikowane miejsca UI

- `src/main.cpp`
  - uproszczono listę pozycji menu ENS160 + AHT21 do bezpośrednich ekranów:
    - `AQI`
    - `TVOC`
    - `eCO2`
    - `Temperature`
    - `Humidity`
    - `Status`

- `src/UI_Controller.cpp`
  - uproszczono nawigację ENS160 + AHT21,
  - usunięto przejścia do pośrednich podmenu `Gazy` i `Klimat`,
  - pozostawiono istniejący system nawigacji enkoderem,
  - długie przytrzymanie nadal wraca z ekranu szczegółowego do menu ENS160.

- `src/UI_Draw.cpp`
  - uproszczono ekran menu ENS160 + AHT21,
  - usunięto nadmiarowe wyświetlanie statusu z ekranów:
    - `AQI`
    - `TVOC`
    - `eCO2`
    - `Temperature`
    - `Humidity`
  - pozostawiono status wyłącznie na ekranie `Status`.

## Usunięte wyświetlania statusu

Status sensora został usunięty z następujących ekranów ENS160 + AHT21:

- `STATE_ENS160_AHT21_GAS_AQI`
- `STATE_ENS160_AHT21_GAS_TVOC`
- `STATE_ENS160_AHT21_GAS_ECO2`
- `STATE_ENS160_AHT21_CLIMATE_TEMP`
- `STATE_ENS160_AHT21_CLIMATE_HUM`

Status nadal jest pokazywany tylko w:

- `STATE_ENS160_AHT21_STATUS`

## Potwierdzenie

- logika sensora nie była zmieniana,
- nawigacja enkoderem została zachowana,
- zmiana objęła wyłącznie warstwę UI i mapowanie menu,
- projekt kompiluje się poprawnie po zmianach.