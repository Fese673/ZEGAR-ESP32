Status diagnostyczny AHTxx

Biblioteka nie interpretuje juz nieudokumentowanych dwoch najmlodszych bitow jako osobnej sekwencji stanow.

Aktualna logika jest konserwatywna i opiera sie tylko na flagach udokumentowanych w bajcie statusu:

- `NO_VALID_OUTPUT` — sensor jest zajety albo nie potwierdzil jeszcze kalibracji.
- `NORMAL_OPERATION` — sensor nie jest zajety i ma ustawiony bit `CAL_ON`.

W praktyce ten status nalezy traktowac jako pomoc diagnostyczna, a nie pelna maszyne stanow czujnika.
