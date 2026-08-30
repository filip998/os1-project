# RCA 006: Test 3 zaobilazi C API za semafore

## Simptom

Za ulaz:

```text
broj proizvodjaca = 3
velicina bafera = 8
```

Test 3 ispise potvrdu parametara, ali zatim ne proizvede nijedan znak i nikada
se ne zavrsi:

```text
Unesite broj proizvodjaca?
Unesite velicinu bafera?
Broj proizvodjaca 3 i velicina bafera 8.
```

Ponovljeni ESC ne menja stanje.

## Zasto je raniji izlaz za 10 i 8 izgledao kontradiktorno?

U zvanicnom testu postoji uslov:

```cpp
if (threadNum > n) {
    printString("Broj proizvodjaca ne sme biti manji od velicine bafera!\n");
    return;
}
```

Za `10 > 8` test pravilno radi early return, ali je tekst poruke pogresan:
trebalo bi da pise da broj proizvodjaca ne sme biti **veci** od velicine
bafera.

Posle povratka iz testa, `userMain` bezuslovno ispisuje:

```text
TEST 3 (zadatak 3., kompletan C API sa semaforima, sinhrona promena konteksta)
```

Zato taj ispis nije dokaz prolaza. Za nevalidan ulaz test uopste nije stigao
do kreiranja bafera i semafora. Typo u poruci postoji i u originalnom
`javniTestovi_2024_1_1.zip`.

## Minimalna reprodukcija stvarnog pada

Privremeno ukljuciti originalni Test 3, zatim uneti:

```text
3
8
```

Kada se niti pokrenu, poslati nekoliko znakova i ESC.

Za precizan dokaz isti kernel je pokrenut kroz `make qemu-gdb`, sa
breakpoint-ima na:

- `semaphore::block`;
- `context_switch`;
- `TCB::thread_wrapper`;
- adresu nula.

Sirovi logovi su sacuvani u gitignored `debug-artifacts/`.

## Konkretan GDB dokaz

Prvi context switch iz `thread_dispatch` syscall handlera pravilno pokrene
consumer nit:

```text
context_switch:
new_ra=TCB::thread_wrapper
new_sp=0x800123e0
thread_wrapper entry: sp=0x800123e0
```

Consumer zatim direktno pozove internu metodu semafora. Posto nema dostupnih
stavki u baferu, ulazi u:

```text
semaphore::block
```

`semaphore::block` direktno pozove `context_switch`. Sledeca nova nit zato
ulazi u `TCB::thread_wrapper` dok je procesor i dalje u U-mode-u.

GDB zatim pokazuje:

```text
pc       = 0
mcause   = 2
mepc     = 0x80001000
mstatus  = 0x20
MPP      = 0
instruction: csrr t0,sstatus
```

`MPP=0` dokazuje da je prethodni rezim bio U-mode. `mcause=2` znaci illegal
instruction. Nova nit je iz U-mode-a pokusala da izvrsi privilegovano citanje
`sstatus` u `clear_sstatus`.

## Poredjenje sa zvanicnim testom

Originalni `ConsumerProducer_C_API_test.cpp` iz
`javniTestovi_2024_1_1.zip` koristi:

```cpp
sem_open(...)
sem_wait(...)
sem_signal(...)
sem_close(...)
```

Originalni `buffer.cpp` koristi iste javne C API funkcije.

Pre-fix radna kopija iz studentskog snapshot-a imala je te pozive promenjene u:

```cpp
semaphore::sem_open(...)
semaphore::sem_wait(...)
semaphore::sem_signal(...)
semaphore::sem_close(...)
```

To je funkcionalna promena testa, a ne samo prilagodjavanje include putanja.
Raniji provenance checkpoint je normalizovao samo ciljane testove 1, 2 i 7;
prosirenje opsega na Test 3 sada zahteva posebno vracanje njegove zvanicne
logike.

## Root cause prostim jezikom

C API treba da prebaci izvrsavanje u kernel:

```text
U-mode test
    |
    v
sem_wait
    |
    v
ecall
    |
    v
supervisorTrap
    |
    v
semaphore::sem_wait u S-mode-u
```

Pre-fix test je preskakao C API:

```text
U-mode test
    |
    v
semaphore::sem_wait direktno u U-mode-u
    |
    v
semaphore::block
    |
    v
context_switch i dalje u U-mode-u
    |
    v
nova nit pokusa privilegovani thread_wrapper
    |
    v
illegal instruction
```

Sama metoda `semaphore::block` je napisana pod pretpostavkom da je pozvana iz
S-mode syscall handlera. Test je poziva iz pogresnog privilegijskog rezima.

## Razmatrane popravke

### Dozvoliti direktni semaphore poziv iz U-mode-a

Ovo bi zaobislo zadati `C API -> ecall -> trap -> kernel` put i zahtevalo
redizajn context switch-a da radi iz dva privilegijska rezima.

### Menjati `thread_wrapper` da tolerise U-mode ulazak

Ovo bi samo sakrilo cinjenicu da je test preskocio syscall granicu.

### Vratiti zvanicne C API pozive

Ovo je izabrana minimalna popravka. Ne menja zvanicnu logiku, vec uklanja
lokalnu promenu koja ju je zaobilazila.

## Predlozena minimalna popravka

U `test/ConsumerProducer_C_API_test.cpp` vratiti:

```text
semaphore::sem_open   -> sem_open
semaphore::sem_wait   -> sem_wait
semaphore::sem_signal -> sem_signal
semaphore::sem_close  -> sem_close
```

U `src/buffer.cpp` uraditi isto i ukloniti nepotreban direktni include
`sem.h`. Raspored direktorijuma i prilagodjene include putanje ostaju.

Ne menjati implementaciju semafora na osnovu ovog pada. Tek kada zvanicni C
API test zaista prodje kroz syscall put, njegov rezultat moze da potvrdi novi
bug u semaphore implementaciji.

Takodje azurirati `docs/provenance.md` da evidentira vracanje zvanicne Test 3
logike.

## Planirana verifikacija

Posle odobrene izmene:

1. potvrditi diff prema originalnim fajlovima iz zvanicnog ZIP-a;
2. pokrenuti nezavisni code review;
3. ponoviti `make clean && make`;
4. ponoviti memorijsku dijagnostiku i testove 1, 2 i 7 kao regresije;
5. pokrenuti Test 3 sa validnim parametrima `3` i `8`;
6. poslati podatke i ESC tek nakon pokretanja niti;
7. ako Test 3 padne, sacuvati novi GDB dokaz pre bilo kakve izmene semafora.

## Pitanja za samoproveru

1. Zasto direktni poziv `semaphore::sem_wait` ne predstavlja C API test?
2. U kom privilegijskom rezimu treba da radi `semaphore::block`?
3. Zasto završna oznaka Testa 3 nije dokaz prolaza kada validacija radi early
   return?
