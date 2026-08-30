# Stanje testova

Poslednje azuriranje: 2026-08-30

Ovaj dokument prati rezultate ciljnih javnih testova i kratke napomene o
samim testovima. Status `PROLAZI` znaci da je originalni test pokrenut do
njegove zavrsne oznake, a ne samo da je zapoceo izvrsavanje.

## Pregled

| Test | Oblast | Status |
| --- | --- | --- |
| Memorijska dijagnostika | `mem_alloc` / `mem_free` kroz javni C API | PROLAZI |
| 1 | Niti kroz C API i sinhrona promena konteksta | PROLAZI |
| 2 | Niti kroz C++ API | PROLAZI |
| 3 | Producer-consumer kroz C API i semafore | PROLAZI |
| 4 | Producer-consumer kroz C++ API i semafore | PROLAZI |
| 7 | Provera izvrsavanja korisnickog koda u U-mode-u | PROLAZI (GDB) |

Testovi 5 i 6 nisu pokrenuti. U kanonskom `src/userMain.cpp` nivoi 3 i 4
ostaju iskljuceni; Testovi 3 i 4 su pokrenuti kroz privremene, gitignored
`userMain` harness-e u VM kopiji.

## Memorijska dijagnostika kroz C API

Privremeni dijagnosticki harness koristi samo javne funkcije:

```cpp
mem_alloc(size);
mem_free(pointer);
```

Zato svaka operacija prolazi kroz stvarni produkcioni put:

```text
C API -> ecall -> supervisorTrap -> Handlers -> Mem
```

Harness je odvojen od zvanicnih testova, nalazi se u gitignored
`debug-artifacts/` direktorijumu i ne ulazi u predajni kod.

### Rezultat

Status: **PROLAZI**

Potvrdjeno je:

- razumne alokacije vracaju pokazivac koji nije `nullptr`;
- pokazivaci su poravnati za `uint64`;
- svih trazenih korisnih bajtova moze da se upise i ponovo procita;
- aktivne alokacije se ne preklapaju;
- osnovno oslobadjanje vraca uspeh;
- oslobodjeni prostor se ponovo koristi;
- susedni slobodni fragmenti se spajaju;
- ponovljene velike alokacije na kraju vrate `nullptr`;
- svi blokovi iz exhaustion testa mogu da se oslobode;
- nova alokacija ponovo uspeva nakon exhaustion-a.

Zavrsna oznaka:

```text
MEMORY_DIAGNOSTICS_PASS
```

Lokalni harness i log:

```text
debug-artifacts/memory-diagnostic-userMain.cpp
debug-artifacts/memory-diagnostic-run.log
```

Posle testa VM kopija je vracena na kanonski `src/userMain.cpp` i kernel je
ponovo cisto izgradjen. Host izvorni kod nije menjan radi dijagnostike.

## Test 1: niti kroz C API

Izvor:

```text
test/Threads_C_API_test.cpp
```

### Kratak opis

Test kroz C API napravi cetiri niti:

```cpp
thread_create(&threads[0], workerBodyA, nullptr);
thread_create(&threads[1], workerBodyB, nullptr);
thread_create(&threads[2], workerBodyC, nullptr);
thread_create(&threads[3], workerBodyD, nullptr);
```

Tela niti vise puta pozivaju `thread_dispatch`, pa test proverava:

- kreiranje cetiri niti kroz `thread_create`;
- pokretanje odgovarajuceg tela svake niti;
- sinhronu, kooperativnu promenu konteksta;
- nastavak niti od mesta na kom je pozvala `thread_dispatch`;
- ocuvanje registra `t1` u niti C;
- zavrsetak rada svih cetiri testna tela.

Glavna test nit ceka sledeci uslov:

```cpp
finishedA && finishedB && finishedC && finishedD
```

### Rezultat

Status: **PROLAZI**

Pokrenut je originalni Test 1 do kraja. Log pokazuje:

- napravljene su niti A, B, C i D;
- A je izvrsila iteracije `0-9`;
- B je izvrsila iteracije `0-15`;
- C je izvrsila iteracije `0-5`;
- D je izvrsila iteracije `10-15`;
- C je posle `thread_dispatch` procitala `t1=7`;
- C je izracunala `fibonacci(12)=144`;
- D je izracunala `fibonacci(16)=987`;
- sva cetiri testna `finished` flag-a postala su `true`;
- ispisana je zavrsna oznaka:

```text
TEST 1 (zadatak 2, niti C API i sinhrona promena konteksta)
```

Lokalni sirovi izlaz:

```text
debug-artifacts/test1-full.log
```

### Napomene o testu

1. `workerBodyC` na liniji 65 pogresno ispisuje:

   ```cpp
   printString("A finished!\n");
   ```

   Trebalo bi da pise `C finished!`. Ovo je samo greska u tekstu: odmah zatim
   se pravilno postavlja `finishedC = true`. Zato log sadrzi dva ispisa
   `A finished!`.

2. Zavrsna oznaka testa dokazuje da su sva cetiri testna `finished` flag-a
   postavljena. Sam test ne proverava niti ispisuje interne `TCB::finished`
   vrednosti za sva cetiri TCB objekta.

3. Niti B, C i D postavljaju svoj testni flag pre poslednjeg
   `thread_dispatch`. Zato prolaz testa sam po sebi nije potpuni dokaz da su
   sva tela vec stigla nazad do `TCB::thread_wrapper` i da su svi interni
   resursi niti ocisceni.

4. Posle zavrsne oznake kernel ostaje u namernoj beskonacnoj dispatch petlji iz
   `main`. QEMU je zato rucno zaustavljen tek nakon sto je Test 1 zavrsen.

Test fajl nije menjan.

## Test 2: niti kroz C++ API

Izvor:

```text
test/Threads_CPP_API_test.cpp
```

### Kratak opis

Test definise cetiri klase izvedene iz `Thread`:

```cpp
class WorkerA : public Thread { /* ... */ };
class WorkerB : public Thread { /* ... */ };
class WorkerC : public Thread { /* ... */ };
class WorkerD : public Thread { /* ... */ };
```

Objekti se prave kroz `new`, a zatim se svaka nit pokrece metodom `start`:

```cpp
threads[0] = new WorkerA();
// ...
threads[i]->start();
```

C++ API ispod povrsine koristi isti C API i kernel put:

```text
Thread::start
    -> thread_create
    -> ecall
    -> supervisorTrap
    -> TCB::create_thread
```

Test proverava:

- globalni `operator new` koji koristi `mem_alloc`;
- konstrukciju cetiri izvedena `Thread` objekta;
- `Thread::start`;
- ulazak kroz `Thread::threadWrapper`;
- virtuelni poziv odgovarajuceg `run`;
- `Thread::dispatch` i sinhronu promenu konteksta;
- zavrsetak sva cetiri testna tela;
- `delete` sva cetiri objekta na kraju testa.

### Rezultat

Status: **PROLAZI**

Pokrenut je originalni Test 2 do kraja. Log pokazuje:

- napravljeni su objekti/niti A, B, C i D;
- sve cetiri niti su izvrsile iste predvidjene opsege iteracija kao u Testu 1;
- C je posle `dispatch` procitala `t1=7`;
- C je izracunala `fibonacci(12)=144`;
- D je izracunala `fibonacci(16)=987`;
- sva cetiri testna `finished` flag-a postala su `true`;
- test je prosao kroz zavrsne `delete` pozive i vratio se u `userMain`;
- ispisana je zavrsna oznaka:

```text
TEST 2 (zadatak 2., niti CPP API i sinhrona promena konteksta)
```

Lokalni sirovi izlaz:

```text
debug-artifacts/test2-full.log
```

### Napomene o testu

1. `WorkerC::workerBodyC` takodje pogresno ispisuje:

   ```cpp
   printString("A finished!\n");
   ```

   Odmah zatim pravilno postavlja `finishedC = true`, pa log ponovo sadrzi dva
   ispisa `A finished!`.

2. Zavrsna oznaka se ispisuje tek nakon:

   ```cpp
   for (auto thread: threads) { delete thread; }
   ```

   Zato prolaz potvrdjuje da su destruktori i sva cetiri `delete` poziva
   zavrsili bez vidljivog pada u ovom testu.

3. Kao i Test 1, Test 2 koristi testne `finishedA-D` flagove. Ne proverava
   eksplicitno unutrasnje stanje svakog TCB objekta nakon zavrsetka.

4. Posle zavrsne oznake kernel ostaje u beskonacnoj dispatch petlji iz `main`,
   pa je QEMU rucno zaustavljen tek nakon zavrsetka testa.

Test fajl nije menjan.

## Test 3: producer-consumer kroz C API

Izvori:

```text
test/ConsumerProducer_C_API_test.cpp
src/buffer.cpp
```

### Kratak opis

Test pravi jednog consumer-a i zadati broj producer niti. Producer sa ID-em
0 cita tastaturu, a ostali producer-i ubacuju cifre koje odgovaraju njihovim
ID vrednostima. Consumer vadi sve znakove iz zajednickog bafera.

`Buffer` koristi javne C API funkcije:

```text
sem_open
sem_wait
sem_signal
sem_close
```

Svaka od njih prolazi kroz `ecall`, trap i internu implementaciju semafora.
Esc (`0x1b`) zavrsava keyboard producer i postavlja zajednicki `threadEnd`.

### Rezultat

Status: **PROLAZI**

Validan scenario sa 3 producer-a i baferom velicine 8 zavrsio je sa:

```text
abc!
Buffer deleted!
!
TEST 3 (zadatak 3., kompletan C API sa semaforima, sinhrona promena konteksta)
```

Dodatni scenario sa 5 producer-a i baferom velicine 10 takodje je zavrsen kod
korisnika. U izlazu su se ocekivano mesali uneti znakovi i cifre `1-4`.

Lokalni log:

```text
debug-artifacts/test3-c-api-postfix.log
```

### Napomene o testu

1. Za ulaz `10/8` test radi early return zato sto je broj producer-a veci od
   bafera. Zvanicna poruka pogresno kaze „ne sme biti manji“, a treba „ne sme
   biti veci“. Zavrsna oznaka nakon tog early return-a nije dokaz testiranja
   semafora.

2. Test je interaktivan. Posle validnih parametara mora da primi znakove i Esc;
   bez Esc-a `threadEnd` ostaje nula i test nema uslov za zavrsetak.

3. Originalni ZIP koristi globalne `sem_*` C API funkcije. Pre-fix studentski
   snapshot ih je zamenio direktnim `semaphore::sem_*` pozivima, sto je
   zaobilazilo trap. Zvanicni pozivi su vraceni pre merodavnog testa.

4. Razdvojeni inline asm blokovi u semaphore wrapperima gubili su argumente.
   Posle RCA 007 svaki syscall koristi jedan extended-asm blok.

Test algoritam nije menjan u odnosu na zvanicni ZIP.

## Test 4: producer-consumer kroz C++ API

Izvori:

```text
test/ConsumerProducer_CPP_Sync_API_test.cpp
test/buffer_CPP_API.cpp
```

### Kratak opis

Test koristi isti producer-consumer scenario kao Test 3, ali kroz C++ klase:

```text
Thread
Semaphore
Console
BufferCPP
```

Time dodatno proverava `new/delete`, izvedene `Thread` klase, virtuelni `run`,
C++ semaphore omotace i destruktore svih napravljenih objekata.

### Rezultat

Status: **PROLAZI**

Prvi scenario sa 5 producer-a, baferom velicine 10, 100 ulaznih znakova i
Esc-om zavrsio je do oznake Testa 4.

Zatim je pokrenut stres scenario:

```text
producer-i: 5
velicina bafera: 10
keyboard unos: 500 znakova
ritam: 50 grupa po 10 znakova, najmanje 5 sekundi izmedju grupa
zavrsetak: Esc
```

Test je tokom celog unosa napredovao, ispraznio preostale znakove, izvrsio
destruktore i zavrsio sa:

```text
Buffer deleted!
324!!
TEST 4 (zadatak 3., kompletan CPP API sa semaforima, sinhrona promena konteksta)
```

`324` su znaci koje su producer niti vec ubacile pre nego sto su primetile
`threadEnd`. Prvi `!` ubacuje keyboard producer na Esc, a poslednji `!`
bezuslovno ispisuje `BufferCPP` destruktor.

Lokalni logovi:

```text
debug-artifacts/test4-full.log
debug-artifacts/test4-stress-500.log
```

### Zasto Enter menja dozivljenu brzinu?

`producerKeyboard` poziva blokirajuci `getc`. Dok ceka sledeci znak unutar
bazne konzole, taj poziv nije povezan sa studentskim schedulerom, pa ostale
kooperativne niti ne dobijaju procesor.

Kada korisnik unosi jedan znak, ceka i zatim pritiska Enter:

- ljudske pauze ostavljaju `getc` dugo blokiranim;
- Enter dodaje jos jedan `'\n'` znak koji prolazi kroz bafer;
- terminal ili konzolna biblioteka mogu isporucivati unos u grupama;
- izlaz zato dolazi u vidljivim naletima umesto odmah posle svakog znaka.

Kada se mnogo znakova unese bez pauze, oni su vec dostupni narednim `getc`
pozivima. Keyboard producer brzo puni mali bafer, blokira se na
`spaceAvailable`, a scheduler tada daje procesor consumer-u i ostalim
producer-ima. Zato izlaz deluje mnogo brze.

Ovo ne menja rezultat testa: oba duza scenarija zavrsila su nakon Esc-a.

Test fajlovi nisu menjani.

## Test 7: provera korisnickog rezima

Izvor:

```text
test/System_Mode_test.cpp
```

### Kratak opis

Test koristi slicnu strukturu sa cetiri niti kao Test 1. Poseban dokaz se
nalazi u niti B:

```cpp
if (i == 10) {
    asm volatile("csrr t6, sepc");
}
```

`sepc` je privilegovani supervisor registar. S-mode kod sme da ga cita, ali
U-mode kod ne sme. Ako se test zaista izvrsava u U-mode-u, procesor mora da
prijavi illegal-instruction trap na ovoj instrukciji.

Test po zvanicnom uputstvu nema regularan kraj. Zato timeout ili odsustvo
zavrsne poruke sami po sebi nisu dokaz prolaza.

### Rezultat

Status: **PROLAZI (GDB POTVRDJENO)**

Live izlaz je stigao do:

```text
B: i=10
```

GDB je zatim potvrdio:

```text
about to execute privileged instruction: pc=0x80005734
0x80005734: csrr t6,sepc

pc      = 0
mcause  = 2
mepc    = 0x80005734
mstatus = 0x20
MPP     = 0
```

Znacenje:

- `MPP=0` dokazuje da je prethodni rezim bio U-mode;
- `mcause=2` znaci illegal instruction;
- `mepc` pokazuje tacno na `csrr t6,sepc`;
- izvrsavanje nije nastavilo posle privilegovane instrukcije;
- `pc=0` nastaje zato sto je machine trap vektor `mtvec=0`.

Time je dokazan ocekivani rezultat Testa 7: korisnicki kod nema dozvolu da
cita supervisor registar.

Lokalni logovi:

```text
debug-artifacts/test7-live.log
debug-artifacts/test7-gdb.log
```

### Napomene o testu

1. Test 7 se ne smatra prolaznim samo zato sto je prestao da ispisuje. GDB
   dokaz je obavezan da bi se iskljucio obican deadlock ili nepovezan pad.

2. Illegal instruction trenutno odlazi u M-mode. Posto je `mtvec=0`, procesor
   zavrsi na adresi nula. To je dovoljno da privilegovana instrukcija ne
   nastavi izvrsavanje, sto je uslov ovog javnog testa.

3. Test namerno nema zavrsnu `TEST 7...` oznaku jer se izvrsavanje prekida na
   privilegovanoj instrukciji.

Test fajl nije menjan.
