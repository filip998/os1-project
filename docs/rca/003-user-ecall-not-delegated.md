# RCA 003: U-mode sistemski poziv ne stize do supervisor trap rutine

## Simptom

Posle popravke trap frame-a, nova korisnicka nit dobija validan stack i stize
do `userMain`, ali se meni i dalje ne ispisuje. Izvrsavanje stane na adresi
nula pri prvom pozivu `putc`.

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

Za precizan dokaz isti kernel je pokrenut kroz `make qemu-gdb`. Breakpoint-i su
postavljeni na `TCB::thread_wrapper`, `wrapperUserMain`, `userMain`,
`printString`, `putc` i adresu nula.

Sirovi logovi su sacuvani u gitignored `debug-artifacts/`.

## Konkretan GDB dokaz

Izvrsavanje uspesno prolazi kroz novu nit i stize do korisnickog programa:

```text
thread_wrapper: sp=0x8000e320
before user-mode sret: sepc=0x80002030 sstatus=0x0
wrapperUserMain reached
userMain reached
printString reached
putc wrapper reached: char=0x55
```

`0x55` je ASCII kod prvog slova `U` iz poruke `Unesite broj testa`.

Na `ecall` instrukciji u `putc` procesor prelazi u machine-mode trap:

```text
pc      = 0
mcause  = 8
mepc    = 0x8000145c  # ecall u putc
mtval   = 0
mtvec   = 0
medeleg = 0x200
```

`mcause=8` znaci `ecall from U-mode`. Vrednost `medeleg=0x200` ima postavljen
samo bit 9, odnosno delegiran je samo `ecall from S-mode`. Bit 8 nije
postavljen.

Posto izuzetak nije delegiran S-mode-u, procesor ne koristi `stvec` i ne ulazi
u `Riscv::supervisorTrap`. Umesto toga pokusava da ode na machine-mode trap
vektor. `mtvec` je nula, pa i `pc` postaje nula.

## Root cause prostim jezikom

`main` najpre radi u S-mode-u. Njegovi sistemski pozivi imaju broj izuzetka 9,
a `entry.S` je taj broj pravilno prosledio supervisor trap rutini:

```asm
li t0, (1 << 9)
csrw medeleg, t0
```

Korisnicka nit se zatim namerno prebaci u U-mode. Kada `userMain` pozove
`putc`, isti `ecall` sada ima broj izuzetka 8 zato sto dolazi iz U-mode-a.

Procesoru nikada nije receno da i broj 8 posalje S-mode kernelu.

```text
main u S-mode
      |
      | ecall, cause 9
      v
medeleg bit 9 postoji
      |
      v
supervisorTrap radi

userMain u U-mode
      |
      | ecall, cause 8
      v
medeleg bit 8 nedostaje
      |
      v
machine-mode trap
      |
      | mtvec = 0
      v
pc = 0
```

## Zasto je raniji S-mode put radio, a korisnicki put nije?

Oba puta koriste isti C API i istu `ecall` instrukciju. Razlika je samo u
rezimu iz kog je `ecall` pokrenut:

```text
S-mode ecall -> cause 9
U-mode ecall -> cause 8
```

`entry.S` je delegirao cause 9, pa su bootstrap pozivi `thread_create`,
`mem_alloc` i `thread_dispatch` iz `main` funkcije stizali do naseg handlera.

Posle prelaska u U-mode, prvi `putc` proizvodi cause 8. Bez delegacije tog bita
procesor zaobilazi celu supervisor trap putanju.

## Razmatrane popravke

### Ostaviti korisnicki program u S-mode-u

Ovo bi sakrilo problem, ali bi prekrsilo cilj projekta i obesmislilo test
korisnickog rezima. `userMain` i testovi treba da rade u U-mode-u.

### Pozivati `__putc` direktno iz korisnickog koda

Ovo bi zaobislo propisani `C API -> ecall -> trap -> kernel` put. I ostali
sistemski pozivi iz U-mode-a bi i dalje padali.

### Napraviti poseban machine-mode trap handler

Za studentske sistemske pozive nije potreban jos jedan handler. Postojeci
supervisor handler vec prepoznaje i cause 8 i cause 9. Potrebno je samo pravilno
usmeriti oba izuzetka do njega.

### Delegirati i U-mode i S-mode ecall

Ovo je izabrana minimalna popravka. U machine-mode bootstrap kodu postavljaju
se bitovi 8 i 9 u `medeleg`.

## Predlozena minimalna popravka

U `src/entry.S`:

```diff
-    # Delegiraj exception "ecall from S-mode" u S-mode
-    li t0, (1 << 9)
+    # Delegiraj U-mode i S-mode ecall izuzetke u S-mode
+    li t0, (1 << 8) | (1 << 9)
     csrw medeleg, t0
```

Posle toga:

```text
U-mode putc
    |
    | ecall, cause 8
    v
medeleg bit 8
    |
    v
stvec
    |
    v
Riscv::supervisorTrap
    |
    v
Handlers::handle_putc
```

Ne menjaju se trap rutina, syscall handleri, konzola, niti, scheduler ili
testovi.

## Verifikacija popravke

Nezavisni code review nije pronasao problem u diff-u.

Ponovljeni `make clean && make` je uspeo za 44 sekunde. Napravljeni su novi
`build/src/supervisorTrap.o` i `kernel`, pa je prosla RCA 001 regresija.

Ponovljena RCA 002 GDB provera potvrdjuje da trap-frame rezultat i stack nove
niti ostaju ispravni:

```text
mem_alloc result: live_a0=0x8000d320 frame_a0=0x8000d320
after restore: a0=0x8000d320
thread_create receives: stack=0x8000d320
new TCB: context.sp=0x8000e320 stack=0x8000d320
thread_wrapper entry: sp=0x8000e320
```

Post-fix GDB reprodukcija za ovaj bug daje:

```text
main: medeleg=0x300
supervisorTrap: scause=0x8 sepc=0x8000145c syscall=0x42
handle_putc: frame=0x8000e1a0 saved_char=0x55
__putc reached
```

`medeleg=0x300` ima postavljene bitove 8 i 9. U-mode `ecall` sada ulazi u
`supervisorTrap` sa `scause=8` i stize do `handle_putc` i bazne `__putc`
funkcije. Time je reprodukovani problem delegacije uklonjen.

Kanonski meni jos nije vidljiv zbog sledeceg, odvojenog bootstrap problema.
`console_write` pokusava da pozove neinicijalizovan pokazivac:

```text
console_write dispatch: target=0x0 devsw_write=0x0
```

Ovaj novi dokaz ne ponistava RCA 003: U-mode trap sada pravilno stize do S-mode
handlera. Inicijalizacija bazne konzole mora dobiti zaseban RCA, odobrenje i
popravku pre zavrsetka prvog milestone-a.

## Pitanja za samoproveru

1. Zasto isti `ecall` ima cause 9 u `main`, a cause 8 u `userMain`?
2. Zasto procesor koristi `mtvec`, a ne `stvec`, kada bit 8 nije delegiran?
3. Zasto ostavljanje `userMain` u S-mode-u nije ispravna popravka?
