# Instrukcja Archiwizacji Błędów

Po każdej naprawionej usterce (bugu), AI Agent ma obowiązek dopisać nowy wpis na **GÓRĘ** pliku: `ERROR_ARCHIVE/ERROR_LOG.md`.

## Zasady ogólne:
1. **Format:** Jeden plik zbiorczy `ERROR_LOG.md`.
2. **Styl:** Kondensowany, techniczny, rozdzielany liniami horyzontalnymi `---`.
3. **Porządek:** Najnowsze błędy zawsze na samej górze (pod nagłówkiem głównym).

## Szablon wpisu do dopisania:

```markdown
---
### [ID: ERR_XXX] | [MODUŁ] | IMPACT: [LOW/CRITICAL]
**Files:** `[Plik1.cpp, Plik2.h]`

**PROBLEM:** [Opis]
**CAUSE:** [Analiza techniczna]
**LOGIC_CHANGE:**
- [Punkt 1]
- [Punkt 2]
**VERIFICATION:** [Test]
---
```

## Wytyczne do treści:
- **ID:** Kolejny numer błędu (sprawdź ostatni wpis w logu).
- **Krótkość:** Używaj pogrubień dla kluczowych słów, aby ułatwić skanowanie wzrokiem.
- **Logika:** Opisuj "dlaczego" i "jak", a nie tylko "poprawiono".
