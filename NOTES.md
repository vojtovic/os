# OS Notes

## Cile projektu
- Mít malý, přehledný systém na ESP32-S3.
- Uložit konfiguraci přímo do ESP přes LittleFS.
- Pracovat se SD kartou jako s druhým uložištěm pro data, média a exporty.
- Mít oddělené vrstvy: hardware, storage, UI, logika a plánování úloh.

## Co by měl pořádný OS obsahovat

### 1. Boot a start systému
- Inicializace serial logu.
- Inicializace GPIO a sběrnic.
- Mount interního filesystemu.
- Načtení konfigurace.
- Ověření periferií a fallback při chybě.

### 2. Hardware abstraction
- Jedno místo pro piny a periferie.
- Oddělené init funkce pro displej, SD, I2C, RTC, audio a input.
- Žádná aplikační logika přímo v low-level kódu.

### 3. Storage vrstva
- LittleFS pro:
  - konfiguraci
  - stav aplikace
  - malé textové soubory
  - cache a metadata
- SD karta pro:
  - větší soubory
  - audio
  - exporty
  - logy nebo data pro uživatele

### 4. Konfigurace
- Textový nebo INI formát.
- Default hodnoty v kódu.
- Automatické vytvoření config souboru, když chybí.
- Validace hodnot po načtení.

### 5. Task manager / scheduler
Tohle je v praxi jádro OS logiky.

Co má řešit:
- Seznam úloh nebo procesů.
- Stav úlohy:
  - ready
  - running
  - blocked
  - sleeping
  - finished
- Prioritu úlohy.
- Časovače a periodické úlohy.
- Přepínání mezi úlohami.
- Fronty zpráv nebo událostí.
- Možnost stopnout, restartovat nebo znovu načíst úlohu.

Pro tenhle projekt je rozumné začít jednoduše:
- hlavní loop jako scheduler
- seznam tasků v poli nebo vektor podobném seznamu
- každá task má `name`, `enabled`, `periodMs`, `lastRunMs`, `run()`
- později přidat priority a event queue

### 5a. Dual-core model na ESP32-S3
- ESP32-S3 ma dve jadra.
- Jedno jadro muze obsluhovat systemove nebo storage tasky.
- Druhe jadro muze obsluhovat UI, input nebo periodic debug.
- V Arduino pro ESP32 je prakticky reseni pres `xTaskCreatePinnedToCore`.
- Prvni verze OS nemusi mit plnohodnotny multitasking, ale ma mit jasne rozdeleni na core0 a core1.
- `loop()` muze zustat jako idle nebo watchdog/supervisor task.

### 5b. Serial debug shell
- `help` zobrazi seznam prikazu.
- `info` vypise stav systemu, heap a uptime.
- `tasks` vypise stav TaskManageru.
- `fs info` a `fs ls` slouzi pro LittleFS.
- `sd info` a `sd ls` slouzi pro SD kartu.
- `config show`, `config reload`, `config save` a `config set ...` slouzi pro spravu boot konfigurace.
- `reboot` provede restart.
- Cilem shellu je mit rychly a opakovatelny debug bez potreby prekladat firmware pri kazde drobnosti.

### 5c. Serial prikazy a co delaji
- `help` - vypise vsechny dostupne prikazy.
- `info` - ukaze zakladni stav systemu: device name, uptime, free heap, nejvetsi volny blok RAM, CPU frekvenci a core ID.
- `tasks` - vypise registrovane tasky, jejich jadro, periodu, prioritu a stav startu.
- `fs info` - ukaze, jestli je LittleFS mountnuty, a pokud ano, vypise jeho velikost a obsazeni.
- `fs ls [path]` - vypise obsah LittleFS adresare, defaultne `/`.
- `sd info` - ukaze, jestli je SD karta dostupna, jaky ma typ a kolik ma prostoru.
- `sd ls [path]` - vypise obsah SD adresare, defaultne `/`.
- `config show` - vypise aktualni runtime konfiguraci nacitanou z LittleFS.
- `config reload` - znovu nacte konfiguraci z `config.ini`.
- `config save` - ulozi aktualni runtime konfiguraci zpet do `config.ini`.
- `config set name <value>` - zmeni jmeno zarizeni v runtime konfiguraci.
- `config set sd on|off` - zapne nebo vypne SD kartu v runtime konfiguraci.
- `config set sd_speed <hz>` - nastavi inicializacni rychlost SD karty v Hz.
- `reboot` - restartuje ESP32.
- `prompt` - znovu vypise prompt `os>` po ruce, kdyz se ztrati v serial monitoru.

### 5d. Prakticke pouziti shellu
- Nejdriv dej `help`.
- Pak `info` pro kontrolu systemu.
- `tasks` hned ukaze, co bezi na kterem jadre.
- `fs info` a `sd info` jsou nejrychlejsi testy storage vrstev.
- `config set ...` + `config save` pouzijes, kdyz chces zmenit chovani bez recompile.

### 6. Event systém
- Tlačítka, klávesnice, timer, SD změny, chyba periferie.
- Jednotné předávání událostí do tasků.
- Lepší než mít všechno natvrdo v `loop()`.

### 7. UI vrstva
- Samostatná logika pro OLED nebo e-ink.
- Render funkce nesmí řešit hardware init.
- UI má číst stav z modelu, ne z hardware přímo.

### 8. Logger a debug
- Serial log při startu.
- Úrovně logování.
- Možnost uložit log na SD nebo do LittleFS.

### 9. Error handling
- Každá init funkce vrací stav.
- Když něco selže, systém jede v omezeném režimu.
- Chybové stavy musí být viditelné na serialu i ideálně na displeji.

### 10. Power management
- Sleep režimy.
- Wake zdroj.
- Bezpečné vypnutí displeje nebo periferií.
- Šetření baterie, pokud bude později relevantní.

## Doporučená struktura projektu
- `src/main.cpp` jako bootstrap a hlavní smyčka.
- `lib/` pro opakovaně použitelné moduly.
- `include/` pro hlavičky a shared definice.
- `data/` pro LittleFS soubory a default config.
- později samostatné soubory pro:
  - `StorageManager`
  - `TaskManager`
  - `DisplayManager`
  - `InputManager`
  - `SystemConfig`

## Prakticky pro tento projekt
- Nejprve LittleFS config.
- Pak SD init a čtení souborů.
- Potom TaskManager jako jednoduchý scheduler.
- Nakonec UI a menu nad tím.

## Prvni rozumna verze task manageru
- Jeden centrální seznam tasků.
- Každá task má periodu.
- `loop()` jen volá scheduler.
- Scheduler rozhodne, co se má spustit.
- Žádné vláknování zatím není nutné.

## Co je zbytecne na zacatek
- Plnohodnotny multitasking.
- Preemptive scheduler.
- Slozity filesystem driver.
- Databaze nebo velke frameworky.

## Poznamka
Tento projekt je zatim vic "system shell" nez hotovy OS. Cilem je nejdriv mit stabilni architekturu, az pak pridavat funkce.
