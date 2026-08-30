# Stanje testova

Poslednje azuriranje: 2026-08-30

Ovaj dokument prati rezultate ciljnih javnih testova i kratke napomene o
samim testovima. Status `PROLAZI` znaci da je originalni test pokrenut do
njegove zavrsne oznake, a ne samo da je zapoceo izvrsavanje.

## Pregled

| Test | Oblast | Status |
| --- | --- | --- |
| 1 | Niti kroz C API i sinhrona promena konteksta | PROLAZI |
| 2 | Niti kroz C++ API | PROLAZI |
| 7 | Provera izvrsavanja korisnickog koda u U-mode-u | PROLAZI (GDB) |

Testovi 3-6 nisu deo trenutnog opsega i iskljuceni su u `src/userMain.cpp`.

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
