# RCA 004: Bazna konzola nije inicijalizovana pre prvog ispisa

## Simptom

Posle ispravnog delegiranja U-mode `ecall` izuzetka, izvrsavanje stize do
`Handlers::handle_putc` i bazne funkcije `__putc`, ali se meni i dalje ne vidi.
Program ponovo zavrsi na adresi nula.

## Minimalna reprodukcija

U zvanicnoj VM:

```sh
make clean
make
make qemu
```

Kernel se pokrene, ali se tekst:

```text
Unesite broj testa? [1-7]
```

ne pojavi.

Za precizan dokaz isti kernel je pokrenut kroz `make qemu-gdb`, sa
breakpoint-om neposredno pre indirektnog poziva iz `console_write`.

Sirovi logovi su sacuvani u gitignored `debug-artifacts/`.

## Konkretan GDB dokaz

RCA 003 reprodukcija prvo potvrdjuje da korisnicki poziv stize do konzole:

```text
main: medeleg=0x300
supervisorTrap: scause=0x8 sepc=0x8000145c syscall=0x42
handle_putc: frame=0x8000e1a0 saved_char=0x55
__putc reached
```

Bazna funkcija `__putc` zatim poziva `console_write`. Ona iz `devsw` tabele
ucitava pokazivac na konkretnu funkciju za ispis i skace na njega.

GDB neposredno pre tog skoka pokazuje:

```text
console_write dispatch: target=0x0 devsw_write=0x0
```

Disassembly bazne biblioteke pokazuje da `consoleinit`:

1. inicijalizuje lock konzole;
2. poziva `uartinit`;
3. upisuje adresu `consoleread` u `devsw` read slot;
4. upisuje adresu `consolewrite` u `devsw` write slot.

Pretraga celog studentskog izvornog koda ne nalazi nijedan poziv
`consoleinit`. Zbog toga `devsw_write` ostane nula, a indirektni poziv odlazi na
adresu nula.

## Root cause prostim jezikom

`__putc` nije funkcija koja sama direktno pise znak na UART. Ona koristi opsti
interfejs:

```text
__putc
   |
   v
console_write
   |
   | procitaj devsw.write
   v
consolewrite
   |
   v
UART
```

Funkcija `consoleinit` treba da poveze `devsw.write` sa stvarnom funkcijom
`consolewrite`. Posto nije pozvana, veza izgleda ovako:

```text
devsw.write = nullptr
```

Prvi ispis zato pokusava da pozove funkciju na adresi nula.

```text
userMain
   |
   v
putc('U')
   |
   v
supervisorTrap
   |
   v
__putc
   |
   v
console_write
   |
   | devsw.write = 0
   v
pc = 0
```

## Zasto direktan dolazak do `__putc` nije dovoljan?

`__putc` je samo ulaz u bazni konzolni podsistem. Taj podsistem pre prve
upotrebe mora da formira svoje interne pokazivace i inicijalizuje UART.

RCA 003 je omogucio da poziv iz U-mode-a dodje do `__putc`. Ovaj bug nastaje
tek u sledecem koraku, unutar jos neinicijalizovanog konzolnog podsistema.

## Razmatrane popravke

### Direktno pisati na UART iz `handle_putc`

Ovo bi zaobislo zvanicnu `console.lib` infrastrukturu i dupliralo gotovu
niskonivošku logiku.

### Rucno popuniti `devsw` tabelu

Ovo bi zavisilo od internih detalja prekompajlirane biblioteke. Takodje bi
preskocilo `uartinit` i inicijalizaciju lock-a.

### Pozvati postojecu `consoleinit` funkciju

Ovo je izabrana minimalna popravka. Bazna biblioteka vec sadrzi celu funkciju
za inicijalizaciju; studentski bootstrap samo treba da je pozove jednom pre
prvog korisnickog ispisa.

## Predlozena minimalna popravka

U `src/main.cpp` deklarisati baznu C funkciju:

```cpp
extern "C" void consoleinit();
```

Zatim je pozvati posle postavljanja `stvec`, a pre ukljucivanja prekida i
kreiranja korisnicke niti:

```cpp
int main() {
    Riscv::w_stvec((uint64)&Riscv::supervisorTrap);
    consoleinit();
    Riscv::ms_sstatus(Riscv::SSTATUS_SIE);
    // ...
}
```

Ne menjaju se `console.lib`, UART implementacija, trap rutina, niti, scheduler
ili testovi.

## Verifikacija popravke

Nezavisni code review nije pronasao problem u diff-u.

Ponovljeni `make clean && make` je uspeo za 48 sekundi. Napravljeni su novi
`build/src/supervisorTrap.o` i `kernel`, pa je prosla RCA 001 regresija.

Post-fix GDB reprodukcija potvrdjuje i ranije trap-frame i delegacione
regresije, kao i novu konzolnu putanju:

```text
main: medeleg=0x300
consoleinit reached
user thread_create: body=0x80001870 stack=0x8000d320
thread_wrapper: sp=0x8000e320
supervisorTrap: scause=0x8 syscall=0x42
console_write dispatch target=0x80005918
consolewrite: length=1 char=0x55
uartputc: char=0x55
```

`console_write` vise ne skace na nulu. Njegov target je stvarna
`consolewrite` funkcija, a prvi znak `U` (`0x55`) stize do `uartputc`.

Realni `make qemu` sada prvi put ispisuje kanonski meni:

```text
Unesite broj testa? [1-7]
```

Uneta cifra `0` jos nije obradjena. To ne ponistava RCA 004: bazna konzola je
inicijalizovana i izlaz radi. Ulaz zavisi od zasebne interrupt putanje koja
mora biti dokazana i popravljena kroz novi RCA pre zavrsetka prvog
milestone-a.

## Pitanja za samoproveru

1. Zasto `__putc` moze biti dostignut, a da se nijedan znak ipak ne ispise?
2. Ko postavlja `devsw.write` na stvarnu funkciju `consolewrite`?
3. Zasto je poziv postojeceg `consoleinit` bolji od direktnog pisanja na UART?
