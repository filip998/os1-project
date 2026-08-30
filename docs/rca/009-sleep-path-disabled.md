# RCA 009: Sleep putanja je iskljucena

## Simptom

Posle povezivanja timer preemption-a, sleep niti pocinju da rade, ali
`time_sleep(10)` i `time_sleep(20)` ne uspavljuju nit. Poziv se odmah vraca i
ispisi nemaju trazeni razmak u timer tick-ovima.

## Minimalna reprodukcija

Pokrenuti originalni Test 5 posle RCA 008 i zaustaviti prvi povratak iz C
wrappera `time_sleep`.

Sirovi logovi su u gitignored `debug-artifacts/`.

## Konkretan GDB dokaz

Na prvom `time_sleep` povratku:

```text
TIME_SLEEP_RETURNED_WITHOUT_BLOCKING
result=49
a0=0x31
sleep_head=(nil)
```

`49`, odnosno `0x31`, nije rezultat sleep operacije, vec syscall kod koji je
ostao u sacuvanom `a0` slotu. Sleep red je prazan, pa trenutna nit nikada nije
blokirana.

Izvorni kod potvrdjuje ceo prekinuti lanac:

```cpp
case TIME_SLEEP:
    //handle_time_sleep(frame);
    break;
```

Timer handler ne azurira sleep red:

```cpp
//Scheduler::update_sleeping();
```

C++ API je takodje stub:

```cpp
int Thread::sleep(time_t time) {
    //return time_sleep(time);
    return 0;
}
```

## Root cause prostim jezikom

Kostur sleep implementacije postoji, ali nijedna ulazna veza nije aktivna:

```text
C time_sleep
    |
    v
ecall 0x31
    X  handler se ne poziva
TCB::time_sleep
    X  nit se ne stavlja u sleep red
timer tick
    X  sleep red se ne odbrojava
```

Za C++ Test 6 postoji jos jedan prekid na samom pocetku:

```text
Thread::sleep -> samo return 0
```

Zato sleep ne moze da radi iako `TCB::time_sleep`, `Scheduler::put_sleep` i
`Scheduler::update_sleeping` vec postoje.

## Zasto su raniji testovi prolazili?

Testovi 1-4 koriste rucni dispatch ili semaphore block za promenu niti. Ne
zavise od `time_sleep`.

Test 6 poziva `Thread::sleep`, ali do sada nije bio pokrenut. Njegov stub je
zato ostao neprimecen.

## Razmatrane popravke

### Implementirati novi sleep red

Nije potrebno, jer studentski scheduler vec sadrzi sleep listu i tick
azuriranje.

### Pretvoriti sleep u busy wait

Ovo bi blokiralo procesor i ne bi testiralo rasporedjivanje drugih niti.

### Povezati postojece karike

Ovo je izabrana minimalna popravka.

## Predlozena minimalna popravka

1. `TIME_SLEEP` syscall proslediti u postojeci `handle_time_sleep(frame)`.
2. Na svakom timer tick-u pozvati `Scheduler::update_sleeping()`.
3. `Thread::sleep(time)` povezati sa C API funkcijom `time_sleep(time)`.

Postojeci `TCB::time_sleep` vec:

```text
stavlja trenutnu nit u sleep red
bira sledecu ready nit
radi context switch
```

Postojeci scheduler vec:

```text
smanjuje preostalo vreme svakog sleep cvora
vraca probudjenu nit u ready red kada vreme postane nula
```

Failure putanje sleep reda ostaju zaseban nepotvrdjeni nalaz i ne menjaju se u
ovom RCA-u.

## Verifikacija popravke

Nezavisni code review nije pronasao problem u tri aktivirane veze. Inkrementalni
build je uspeo.

Originalni Test 5 je zavrsio sa po pet ispisa obe sleep niti:

```text
Hello 10 !
Hello 20 !
Hello 10 !
Hello 20 !
Hello 10 !
Hello 10 !
Hello 20 !
Hello 10 !
Hello 20 !
Hello 20 !
TEST 5 (zadatak 4., thread_sleep test C API)
```

GDB je posebno pratio prvu nit koja je pozvala `sleep(10)`:

```text
TRACKING_SLEEP_10 thread=0x8000f430
SLEEP_10_RESUMED ticks=14
```

Nit se nije vratila pre 10 timer tick-ova. Dodatna cetiri tick-a predstavljaju
normalno cekanje da probudjena ready nit ponovo dodje na red kod scheduler-a.

Test 6 i zavrsne regresije ostaju sledeci korak.

## Pitanja za samoproveru

1. Zasto je rezultat `0x31` dokaz da syscall handler nije obradio sleep?
2. Ko uklanja vreme sleep niti na svakom tick-u?
3. Zasto `Thread::sleep` mora da pozove C API umesto interne TCB metode?
