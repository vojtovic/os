# OPEN ISSUES (os)

Datum: 2026-04-05

Tento soubor shrnuje aktualni stav po testech na HW, co nefunguje, a co je potreba dodelat.

## Co aktualne NEFUNGUJE

1. E-ink wake po 60 s timeoutu
- Po uspani se e-ink korektne vypne.
- Po stisku klavesy se e-ink nekdy (nebo casto) neprobere/zustane bez obrazu.

2. OLED sleep po 60 s timeoutu
- OLED se po timeoutu neuspava spolehlive (uzivatelsky vypada, ze zustava zapnuty).

3. Sleep/wake konzistence mezi displeji
- Chovani OLED a e-inku neni konzistentni: jeden displej se uspi/probudi, druhy ne.

4. Prehravani hudby ze SD karty
- Chybi realna audio playback pipeline (scan souboru -> vyber skladby -> play/stop).
- Bez toho nelze overit end-to-end prehravani skladeb z SD.

5. Ceska lokalizace (diakritika)
- CardKB vstup zatim neumi spolehlive zadavat ceske znaky.
- E-ink/OLED render zatim nepokryva ceske znaky v pouzitych fontech.

## Co uz funguje

1. Build projektu je OK (`platformio run`).
2. Upload FW je OK (kdyz neni zamceny serial port monitorem).
3. Launcher/Settings flow je rozpracovany.
4. OLED ma live feedback pro stisk klavesy (alespon v casti flow).
5. E-ink policy partial/full refresh je implementovana (full refresh po 10. partial refreshi).

## Co je potreba dodelat (TODO)

### P1 - Kriticke

1. Stabilni wake e-inku po sleep
- Zkontrolovat init/wake sekvenci panelu po `sleep()`.
- Overit, zda panel po wake vyzaduje specific clear + full refresh sekvenci.
- Pridat fallback sekvenci pri failed wake (napr. hard re-init + jednoznacny full redraw).

2. Spolehlive uspani OLED
- Overit podporu `setPowerSave(1)` pro konkretni SH1106 modul.
- Pokud powersave nefunguje fyzicky, implementovat alternativu:
  - trvale blank + minimalni kontrast,
  - pripadne periodicke drzeni blank stavu.

3. Jednotny sleep manager pro oba displeje
- Mit jeden stavovy mechanismus: `Awake`, `Sleeping`, `Waking`.
- Zabranit race condition: key event vs. timeout v tom samem cyklu.

### P2 - Diagnostika

4. Pridat detailni debug logy sleep/wake
- Logovat:
  - prechod do sleep (OLED/EINK),
  - key event detekci,
  - vysledek wake pokusu,
  - vysledek prvniho redraw po wake.

5. Pridat shell diagnosticky prikaz
- Napr. `display wake-test`:
  - forced sleep,
  - forced wake,
  - immediate render,
  - tisk internich flagu.

6. Overit CardKB event stream pri wake
- Potvrdit, ze key event opravdu prijde pri prvnim stisku po sleep.

### P3 - UX

7. Pri wake ukazat explicitni stav
- OLED: kratka hlaska `waking displays...`.
- E-ink: po wake okamzity redraw aktivni obrazovky.

8. Dotahnout launcher/settings feedback
- Konzistentni zobrazeni posledni klavesy na OLED.
- Zajistit, ze e-ink nereaguje na neakci klavesy.

### P2 - Funkcni backlog (media + lokalizace)

9. SD audio player MVP
- SD scan audio souboru (minimalne `.mp3`, `.wav`).
- UI vyber skladby a zakladni ovladani `play/stop/next`.
- Zobrazeni stavu prehravani na OLED/e-ink.

10. CZ lokalizace vstupu a vykresleni
- Definovat mapovani CardKB pro ceske znaky.
- Doplnit fallback transliteraci (napr. `c` -> `č`) pro psani pres klavesove sekvence.
- Overit/rozsirit fonty tak, aby OLED/e-ink korektne zobrazovaly ceske znaky.

## Jak reprodukovat aktualni bug

1. Nahrat firmware.
2. Nechat zarizeni bez vstupu 60+ sekund.
3. Overit, ze probehne timeout sleep.
4. Stisknout libovolnou klavesu na CardKB.
5. Ocekavani: oba displeje se probudi.
6. Realita: e-ink casto nezapne, OLED se casto ani nevypne.

## Poznamky k testu

- Pri uploadu muze byt port `/dev/ttyACM1` zamceny behem aktivniho monitoru.
- Pred uploadem zavrit monitor, pak znovu upload.
