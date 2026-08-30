# RCA 008: Timer preemption putanja nije povezana

## Simptom

Originalni Test 5 napravi dve niti, ali nijedna ne pocne da radi. Ne pojavljuje
se nijedan `Hello` ispis i userMain ostane zauvek u busy petlji.

## Minimalna reprodukcija

Privremeno pokrenuti originalni `ThreadSleep_C_API_test.cpp` iz zvanicnog ZIP-a
na trenutnom kernelu. Test stane pre prvog ispisa iz sleep niti.

Sirovi logovi su sacuvani u gitignored `debug-artifacts/`.

## Konkretan GDB dokaz

GDB je zaustavio userMain nakon kreiranja obe sleep niti:

```text
TEST5_BUSY_LOOP_REACHED
pc=0x80005378
mtvec=0x0
mie=0x200
mideleg=0x200
sie=0x200
sip=0x0
scheduler_count=3
```

Tri niti su spremne u scheduleru, ali:

- `mtvec=0` pokazuje da machine timer rutina nije instalirana;
- `mie` nema bit 7 (`MTIE`);
- `mideleg` nema bit 1 (`SSIP`);
- `sie` nema bit 1 (`SSIE`);
- nijedan supervisor timer signal nije pending.

ETF `hw.lib` vec sadrzi `timerinit` i `timervec`, ali ti simboli nisu ulinkovani
u trenutni kernel zato sto ih studentski bootstrap nigde ne koristi.

## Root cause prostim jezikom

Test 5 namerno ne poziva `thread_dispatch` u svojoj busy petlji. Ocekuje da
hardverski timer prekine trenutnu nit i pokrene neku od dve spremne niti.

Trenutni tok nema nijednu vezu:

```text
machine timer
    X  timerinit nije pozvan
timervec
    X  SSIP nije delegiran
supervisorTrap
    X  SSIE nije ukljucen
decrease time slice
    X  poziv je zakomentarisan
thread_dispatch
```

Zato userMain moze da se vrti zauvek, iako scheduler vec sadrzi druge niti.

## Sta vec postoji?

ETF biblioteka daje:

```text
timerinit
timervec
```

`timerinit` programira CLINT timer, postavlja `mtvec=timervec` i ukljucuje
machine timer interrupt. `timervec` zakazuje sledeci tick i postavlja
supervisor software interrupt (`sip.SSIP`).

Studentski kod vec ima:

```text
Handlers::handle_timer_interrupt
TCB::decrease_time_slice
TCB::reset_time_slice
TCB::time_slice_expired
```

Nedostaje njihovo povezivanje.

## Razmatrane popravke

### Dodati `thread_dispatch` u Test 5

Ovo bi pretvorilo asinhroni test u kooperativni i sakrilo nedostatak timer-a.

### Pisati novu CLINT rutinu

Nije potrebno, jer ETF `hw.lib` vec daje proverene `timerinit` i `timervec`
funkcije.

### Povezati postojecu timer putanju

Ovo je izabrana minimalna popravka.

## Predlozena minimalna popravka

1. Iz `entry.S`, dok je procesor jos u M-mode-u, pozvati bazni `timerinit`.
2. Delegirati `SSIP` bit 1 u `mideleg`, uz postojeci console bit 9.
3. U `sie` ukljuciti `SSIE` tek nakon sto postoje `TCB::running` i korisnicka
   nit.
4. U timer handleru sacuvati `sepc/sstatus`, ocistiti SSIP, smanjiti kvant,
   uraditi dispatch kada kvant istekne i zatim restaurirati CSR vrednosti.
5. Resetovati kvant pre svakog context switch-a na drugu nit: dispatch,
   thread exit, semaphore block i postojecu sleep putanju.
6. Delegirati illegal-instruction exception S-mode-u, jer `timerinit` koristi
   `mtvec` iskljucivo za machine timer. Time Test 7 ne sme pasti u
   `timervec`.

Sleep red i `time_sleep` se jos ne aktiviraju u ovom RCA-u.

## Verifikacija popravke

Nezavisni code review je pronasao da se globalni kvant mora resetovati pre
svakog context switch-a, ne samo u `thread_dispatch`. Reset je zato dodat pre
postojecih switch putanja u dispatch-u, thread exit-u, semaphore block-u i
sleep-u. Ponovljeni review nije nasao probleme.

`make clean && make` je uspeo i u kernel su ulinkovani bazni `timerinit` i
`timervec`.

GDB je potvrdio ponovljene supervisor timer tick-ove i promenu kvanta:

```text
timer_tick=1 scause=0x8000000000000001 time_slice=2
timer_tick=2 scause=0x8000000000000001 time_slice=1
timer_tick=3 scause=0x8000000000000001 time_slice=2
```

Ready `sleepyRun` nit je zatim pocela bez rucnog dispatch-a iz Testa 5:

```text
SLEEPY_THREAD_STARTED_WITHOUT_MANUAL_DISPATCH
```

Minimalna Test 7 regresija je iz U-mode-a izvrsila istu privilegovanu
instrukciju kao originalni test:

```text
csrr t6,sepc
```

Dobijen je ocekivani supervisor trap:

```text
scause=0x2
sepc=0x800056bc
spp=0x0
```

`SPP=0` potvrdjuje da je instrukcija dosla iz U-mode-a. Timer-only `mtvec`
rutina nije pogresno obradila illegal instruction.

Sleep red i `time_sleep` jos nisu aktivirani u ovom RCA-u.

## Pitanja za samoproveru

1. Zasto Test 5 ne moze da pokrene sleep niti bez timer preemption-a?
2. Zasto treba ukljuciti bas `SSIE`, a ne samo console `SEIE`?
3. Zasto se kvant resetuje pre context switch-a?
