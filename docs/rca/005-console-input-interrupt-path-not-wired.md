# RCA 005: Konzolna ulazna interrupt putanja nije povezana

## Simptom

Posle inicijalizacije bazne konzole, meni se ispravno prikazuje:

```text
Unesite broj testa? [1-7]
```

ali uneta cifra ne stize do `getc`. Program ostaje u `consoleread` i ceka
znak zauvek.

## Minimalna reprodukcija

U zvanicnoj VM:

```sh
make clean
make
make qemu
```

Kada se pojavi meni, uneti:

```text
0
```

Meni ne reaguje na unos.

Za ponovljiv GDB dokaz napravljen je FIFO povezan sa QEMU standardnim ulazom.
GDB je poslao `0` tacno kada je `consoleread` poceo da ceka, bez oslanjanja na
procenu vremena pokretanja.

Sirovi logovi su sacuvani u gitignored `debug-artifacts/`.

## Konkretan dokaz

GDB potvrdjuje da je izlazna konzolna putanja ispravna i da ulaz dolazi do
funkcije za citanje:

```text
__getc reached; waiting for menu input
consoleread reached; buffer read begins
sent menu input through QEMU stdin
```

I pored poslatog znaka, `consoleread` ostaje u petlji koja ceka da UART
interrupt handler upise znak u konzolni bafer.

Kontrolni registri u tom trenutku imaju vrednosti:

```text
mideleg = 0
sie     = 0
mip     = 0
sip     = 0
sstatus = 0x2
```

To znaci:

- supervisor external interrupt nije delegiran iz M-mode-a;
- supervisor external interrupt nije ukljucen u `sie`;
- samo globalni `sstatus.SIE` jeste ukljucen.

Bazna biblioteka sadrzi funkcije:

```text
plicinit
plicinithart
console_handler
```

Disassembly pokazuje da `plicinit` postavlja prioritet UART izvora, a
`plicinithart` ukljucuje UART IRQ 10 za trenutni hart i postavlja PLIC prag.
Pretraga studentskog izvornog koda ne nalazi nijedan njihov poziv.

Pored toga, studentski supervisor handler je prazan:

```cpp
void Handlers::handle_console_interrupt() {

}
```

Zato ulazna putanja ima vise nepovezanih karika. Cak i kada bi interrupt stigao
do `supervisorTrap`, prazan handler ga ne bi predao baznom `console_handler`,
koji poziva `plic_claim`, obradi UART i upisuje primljeni znak u bafer.

## Root cause prostim jezikom

Ispis i unos ne rade na isti nacin.

Za ispis kernel moze odmah da posalje znak UART-u:

```text
putc -> consolewrite -> uartputc
```

Za unos procesor ne proverava tastaturu stalno. `getc` ceka da hardver javi:

```text
"Stigao je novi znak."
```

To obavestenje prolazi kroz nekoliko nivoa:

```text
QEMU tastatura
      |
      v
UART
      |
      v
PLIC
      |
      v
supervisor external interrupt
      |
      v
supervisorTrap
      |
      v
console_handler
      |
      v
znak se upise u konzolni bafer
      |
      v
getc vrati znak
```

U trenutnom bootstrap-u nedostaju veze izmedju ovih nivoa:

```text
PLIC nije inicijalizovan
mideleg bit 9 nije postavljen
sie.SEIE nije ukljucen
handle_console_interrupt je prazan
```

Zato `getc` ceka bafer koji niko nikada ne puni.

## Zasto `sstatus.SIE` nije dovoljan?

`sstatus.SIE` je glavni prekidac za supervisor prekide, ali svaki tip prekida
ima i svoj prekidac.

Analogija:

```text
sstatus.SIE = glavni osigurac zgrade
sie.SEIE    = osigurac za konzolni sprat
PLIC        = centrala koja prosledjuje konkretan UART poziv
mideleg     = dozvola da M-mode prepusti taj poziv S-mode-u
```

Ako bilo koja od tih karika nedostaje, supervisor trap ne dobija UART
interrupt.

## Razmatrane popravke

### Aktivno proveravati UART u petlji

Polling bi zaobisao isporucenu interrupt infrastrukturu i promenio arhitekturu
konzole samo da bi sakrio nepotpun bootstrap.

### Direktno citati UART registar u `getc`

Ovo bi zaobislo bazni `console.lib`, njegov bafer i `console_handler`.

### Povezati postojecu baznu interrupt putanju

Ovo je izabrano resenje. Biblioteka vec sadrzi UART, PLIC i konzolni handler.
Studentski bootstrap treba da ukljuci potrebne interrupt bitove, pozove
postojecu PLIC inicijalizaciju i preda supervisor external interrupt
postojecem `console_handler`.

## Predlozena minimalna popravka

1. U machine-mode `entry.S` delegirati supervisor external interrupt:

```asm
li t0, (1 << 9)
csrw mideleg, t0
```

2. U `Riscv` dodati masku i mali helper za ukljucivanje `sie.SEIE`:

```cpp
SIE_SEIE = (1 << 9)
```

```asm
csrs sie, mask
```

3. U `main` pozvati postojece bazne funkcije pre globalnog ukljucivanja
   prekida:

```cpp
consoleinit();
plicinit();
plicinithart();
Riscv::ms_sie(Riscv::SIE_SEIE);
Riscv::ms_sstatus(Riscv::SSTATUS_SIE);
```

4. Postojeci supervisor console handler povezati sa baznom rutinom:

```cpp
void Handlers::handle_console_interrupt() {
    console_handler();
}
```

Ne menja se implementacija `console.lib`, UART-a, PLIC-a, `getc`, bafera,
niti, scheduler-a ili testova. Povezuju se samo vec isporucene karike potrebne
za prijem jednog znaka u meniju.

## Verifikacija popravke

Nezavisni code review nije pronasao problem u diff-u.

Ponovljeni `make clean && make` je uspeo za 62 sekunde. Napravljeni su novi
`build/src/supervisorTrap.o` i `kernel`.

Post-fix GDB reprodukcija pokazuje:

```text
__getc: mideleg=0x200 sie=0x200
sent menu input: 0 and newline
supervisor external interrupt: scause=0x8000000000000009
console_handler reached
uartintr reached
userMain consumed menu input: test=0
```

`mideleg` i `sie` sada imaju postavljen bit 9. UART interrupt stize do
`supervisorTrap`, bazni `console_handler` ga preuzima, `uartintr` obradjuje
znak, a `userMain` dobija vrednost `0`.

Zavrsna `make qemu` provera nije slala ulaz prema proceni vremena. Workflow je:

1. pokrenuo QEMU sa FIFO ulazom;
2. sacekao da se u izlazu stvarno pojavi meni;
3. tek tada poslao `0` i novi red;
4. sacekao odgovor programa.

Dobijen je ocekivani izlaz:

```text
Unesite broj testa? [1-7]
Niste uneli odgovarajuci broj za test
```

Time je dokazano da se kernel cisto gradi, dolazi do kanonskog menija i prima
cifru bez pokretanja javnih testova. RCA 001-004 putanje su obuhvacene ovom
end-to-end regresijom: trap objekat je prisutan, korisnicka nit ima validan
stack, U-mode sistemski pozivi stizu u supervisor trap i izlazna konzola radi.

## Pitanja za samoproveru

1. Zasto ispis moze da radi iako unos ne radi?
2. Koja je razlika izmedju `sstatus.SIE` i `sie.SEIE`?
3. Koja funkcija treba da preuzme UART interrupt i upise znak u konzolni
   bafer?
