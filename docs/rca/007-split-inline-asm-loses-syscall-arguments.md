# RCA 007: Razdvojeni inline asm blokovi gube syscall argumente

## Simptom

Posle vracanja zvanicnog Testa 3 na javni C API put, validan slucaj:

```text
broj proizvodjaca = 3
velicina bafera = 8
```

ispise potvrdu parametara, ali zatim ne proizvede nijedan znak i ne zavrsi se:

```text
Broj proizvodjaca 3 i velicina bafera 8.
```

## Minimalna reprodukcija

Pokrenuti Test 3 kroz zvanicne `sem_*` C API funkcije i uneti:

```text
3
8
```

Test stane pri prvom `sem_wait` pozivu.

Za dokaz su sacuvani disassembly syscall wrappera i GDB registri na ulasku u
trap.

Sirovi logovi su u gitignored `debug-artifacts/`.

## Konkretan dokaz

Pre-fix `sem_wait` je napisan kao cetiri odvojena inline asm bloka:

```cpp
__asm__ volatile("li a0, 0x23");
__asm__ volatile("mv a1, %0" : : "r"(id));
__asm__ volatile("ecall");
__asm__ volatile("mv %0, a0" : "=r"(result));
```

Generisani RISC-V kod je:

```text
li a0,35
mv a1,a0
ecall
```

Prvi C argument funkcije `sem_wait`, pokazivac `id`, na ulazu je bio u `a0`.
Prvi asm blok nije deklarisao da menja `a0`, pa je compiler smatrao da se
vrednost `id` i dalje nalazi tamo. Instrukcija `li a0,35` ju je zapravo
pregazila, a sledeci blok je kopirao vec prepisanu vrednost u `a1`.

GDB na ulasku u trap potvrdjuje:

```text
semaphore syscall trap: code=0x23 a1=0x23
```

Umesto adrese pravog semaphore objekta, handler dobija pokazivac `0x23`.

Isti disassembly obrazac postoji u ostalim razdvojenim wrapperima:

```text
sem_signal:
    li a0,36
    mv a1,a0

sem_wait_n:
    li a0,37
    mv a1,a0
    mv a2,a1

sem_signal_n:
    li a0,38
    mv a1,a0
    mv a2,a1

time_sleep:
    li a0,49
    mv a1,a0
```

`sem_wait_n` i `sem_signal_n` zato gube i semaphore pokazivac i argument `n`.

## Root cause prostim jezikom

Svaki `asm` blok je poseban dogovor sa compilerom. Compiler ne cita assembler
tekst da bi zakljucio koji registri su promenjeni. To mora eksplicitno da mu se
kaze kroz ulaze, izlaze i clobber listu.

Pre-fix kod je compileru izgledao kao cetiri nepovezane operacije:

```text
blok 1: nema ulaza, nema izlaza, navodno ne menja registre
blok 2: procitaj C promenljivu id
blok 3: navodno ne menja registre
blok 4: napravi C rezultat
```

Stvarna namera je bila jedna nedeljiva syscall sekvenca:

```text
premesti argumente
      |
      v
postavi syscall kod
      |
      v
ecall
      |
      v
preuzmi rezultat
```

Kada se sve nalazi u jednom extended-asm bloku, compiler istovremeno vidi sve
ulaze, rezultat i registre koji se menjaju. Tada ne sme da smesti `id` u `a0`
ili `a1` i pregazi ga pre upotrebe.

## Zasto su thread testovi prolazili?

`thread_create`, `mem_alloc`, `mem_free` i ostali ranije korisceni wrapperi vec
su imali jednu povezanu asm sekvencu sa operandima i clobber listom.

`thread_dispatch` je takodje imao jednu asm sekvencu:

```cpp
__asm__ volatile(
    "li a0, 0x13\n\t"
    "ecall\n\t");
```

Zato njegov syscall kod nije bio razdvojen od `ecall`. Medjutim, compileru
nije bilo receno da sekvenca menja `a0` i memorijsko stanje. U ovoj maloj
funkciji to nije proizvelo vidljiv pad, ali je deklaracija nepotpuna i moze da
postane pogresna kada compiler promeni raspored koda.

## Razmatrane popravke

### Dodati `a0` clobber svakom pojedinacnom bloku

Ovo bi smanjilo jedan deo rizika, ali bi i dalje ostavilo jednu syscall
operaciju podeljenu na vise compiler granica. Bilo bi lako propustiti zavisnost
izmedju argumenata, `ecall` i rezultata.

### Koristiti C promenljive vezane za fiksne registre

Ovo moze da radi, ali nije potrebno i odstupalo bi od stila wrappera koji vec
ispravno rade u projektu.

### Koristiti jedan extended-asm blok po syscall-u

Ovo je izabrano resenje. Prati postojeci obrazac `mem_alloc`, `mem_free`,
`thread_create`, `sem_open` i `sem_close`.

## Predlozena minimalna popravka

Spojiti svaku kompletnu syscall sekvencu u jedan `asm volatile` blok za:

```text
sem_wait
sem_signal
sem_wait_n
sem_signal_n
time_sleep
```

Svaki blok navodi:

- C ulazne operande;
- C izlazni operand;
- registre `a0`, `a1` i po potrebi `a2`;
- `"memory"` clobber.

Za `thread_dispatch` ne menjati instrukcije ni ponasanje. Samo dopuniti
postojeci jedinstveni asm blok:

```cpp
:
:
: "a0", "memory"
```

To compileru znaci:

```text
ovaj blok menja a0
ovaj blok moze da promeni memorijsko stanje
```

Ne znaci da `thread_dispatch` vraca vrednost. Ne dodaje se novi argument niti
nova instrukcija.

## Planirana verifikacija

Posle odobrene izmene:

1. pregledati novi disassembly svih sest wrappera;
2. potvrditi da `a1` i `a2` dobijaju stvarne argumente pre postavljanja `a0`;
3. pokrenuti nezavisni code review;
4. ponoviti `make clean && make`;
5. ponoviti memorijsku dijagnostiku i testove 1 i 2;
6. ponovo pokrenuti validan Test 3 sa parametrima `3` i `8`;
7. ako Test 3 ponovo padne, sacuvati novi dokaz pre izmene interne semaphore
   implementacije.

## Pitanja za samoproveru

1. Zasto compiler ne zna da tekst `li a0, 0x23` menja `a0`?
2. Kako je pokazivac semafora postao broj `0x23`?
3. Zasto `thread_dispatch` treba clobber listu iako nema C rezultat?
