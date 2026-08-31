# Timer, preemption i sleep: detaljan vodic

Ovaj dokument objasnjava timer implementaciju uvedenu kroz RCA 008 i RCA 009.
Cilj je da svaka funkcionalna izmena bude razumljiva od hardverskog tick-a do
izvrsavanja javnih Testova 5 i 6.

## 0. Osnove od nule: procesor, assembler i prekidi

Ovaj deo namerno koristi jednostavne modele. Cilj nije ucenje celog RISC-V
standarda, vec razumevanje konkretnih linija ovog projekta.

### 0.1 Procesor izvrsava jednu instrukciju za drugom

Program u memoriji izgleda kao niz instrukcija:

```text
instrukcija na adresi 100
instrukcija na adresi 104
instrukcija na adresi 108
...
```

Procesor mora da zna koju instrukciju sledecu izvrsava. Za to koristi registar
koji konceptualno zovemo **PC** (`program counter`):

```text
PC = adresa sledece instrukcije
```

Normalan tok:

```text
PC=100 -> izvrsi instrukciju
PC=104 -> izvrsi instrukciju
PC=108 -> izvrsi instrukciju
```

Poziv funkcije, povratak iz funkcije, `if`, petlja, context switch i trap na
razlicite nacine menjaju ovaj tok.

### 0.2 Obicni RISC-V registri

Registri su veoma mala i brza mesta unutar procesora. U projektu su
najvazniji:

| Registar | Prosto znacenje |
| --- | --- |
| `a0-a7` | argumenti funkcije i syscall-a |
| `a0` | prvi argument i povratna vrednost |
| `sp` | pokazuje na vrh trenutnog steka |
| `ra` | adresa na koju se funkcija vraca |
| `t0-t6` | privremene vrednosti |
| `s0-s11` | vrednosti koje funkcije moraju da sacuvaju |

Na primer:

```asm
mv a1, t0
```

znaci:

```text
kopiraj vrednost iz t0 u a1
```

Instrukcija:

```asm
li a0, 0x31
```

znaci:

```text
upisi konstantu 0x31 u a0
```

### 0.3 `sp`: stek trenutne niti

Svaka nit ima svoj stek. `sp` pokazuje gde se ta nit trenutno nalazi na svom
steku.

Funkcija cesto na ulasku uradi:

```asm
addi sp, sp, -32
```

To znaci:

```text
rezervisi 32 bajta na steku
```

Zatim moze da sacuva registar:

```asm
sd ra, 24(sp)
```

To znaci:

```text
sacuvaj ra na adresu sp + 24
```

Pri context switch-u menjamo `sp`, pa procesor od tog trenutka koristi stek
druge niti.

### 0.4 `ra`: gde se funkcija vraca

Kada assembler pozove funkciju:

```asm
call neka_funkcija
```

procesor:

1. sacuva adresu sledece instrukcije u `ra`;
2. postavi PC na adresu pozvane funkcije.

Kada funkcija uradi:

```asm
ret
```

PC dobija vrednost iz `ra`.

Kod potpuno nove niti mi namerno postavljamo njen sacuvani `ra` na:

```cpp
TCB::thread_wrapper
```

Zato njen prvi context switch izgleda kao povratak u `thread_wrapper`, iako ta
nit nikada ranije nije radila.

### 0.5 Tri privilegijska rezima

Procesor ima tri relevantna nivoa:

```text
M-mode: najvisi nivo, pokretanje i kontrola hardvera
S-mode: kernel
U-mode: korisnicki kod i testovi
```

Mozemo ih zamisliti kao spratove:

```text
M-mode  kontrolna soba zgrade
S-mode  administracija
U-mode  obican korisnik
```

Korisnik ne sme direktno da menja privilegovane registre. Zato U-mode funkcija
ne poziva direktno `TCB::thread_dispatch` ili `semaphore::sem_wait`.

Ona poziva C API i izvrsi:

```asm
ecall
```

To je kontrolisan zahtev kernelu.

### 0.6 Sta je trap?

**Trap** je zajednicki naziv za dogadjaj koji prekida normalan tok programa i
prebacuje procesor u posebnu handler rutinu.

Postoje dve glavne vrste:

#### Sinhroni exception

Nastaje zbog instrukcije koju procesor upravo izvrsava.

Primeri:

```text
ecall
illegal instruction
nevalidan pristup memoriji
```

Zovemo ga sinhronim zato sto se uvek desava na konkretnoj instrukciji.

#### Asinhroni interrupt

Dolazi spolja u odnosu na trenutnu instrukciju.

Primeri:

```text
timer tick
stisnut taster
UART interrupt
```

Moze da stigne izmedju bilo koje dve instrukcije.

### 0.7 Sta znaci „vector“?

U ovom kontekstu **vector nije C++ kontejner i nije niz**.

Trap vector je samo:

```text
adresa funkcije na koju procesor treba da skoci kada nastane trap
```

Dva vazna CSR registra:

| Registar | Znacenje |
| --- | --- |
| `mtvec` | adresa M-mode trap rutine |
| `stvec` | adresa S-mode trap rutine |

Na primer:

```cpp
Riscv::w_stvec((uint64)&Riscv::supervisorTrap);
```

znaci:

```text
kada trap pripadne S-mode-u, skoci na supervisorTrap
```

`timerinit` radi slicnu stvar:

```text
mtvec = timervec
```

Zato se funkcija zove `timervec`: ona je ulazna adresa machine timer trap-a.

### 0.8 Sta su CSR registri?

CSR znaci `Control and Status Register`.

To su posebni procesorski registri za kontrolu privilegija, prekida i trapova.
Ne koriste se kao obicne C promenljive.

Najvazniji:

| CSR | Prosto znacenje |
| --- | --- |
| `mtvec` | gde M-mode obradjuje trap |
| `stvec` | gde S-mode obradjuje trap |
| `mcause` | zasto je nastao M-mode trap |
| `scause` | zasto je nastao S-mode trap |
| `mepc` | instrukcija na kojoj je nastao M-mode trap |
| `sepc` | instrukcija na kojoj je nastao S-mode trap |
| `mtval/stval` | dodatna informacija, cesto nevalidna adresa |
| `mstatus/sstatus` | privilegijsko i interrupt stanje |
| `mie/sie` | koji tipovi prekida su dozvoljeni |
| `mip/sip` | koji prekidi trenutno cekaju obradu |
| `medeleg` | koje exception-e M-mode predaje S-mode-u |
| `mideleg` | koje interrupt-e M-mode predaje S-mode-u |

### 0.9 Osnovne CSR assembler instrukcije

#### Citaj CSR

```asm
csrr t0, sstatus
```

Znaci:

```text
t0 = sstatus
```

#### Upisi celu vrednost

```asm
csrw stvec, t0
```

Znaci:

```text
stvec = t0
```

#### Postavi odredjene bitove

```asm
csrs sie, t0
```

Znaci:

```text
sie = sie OR t0
```

Postojeci bitovi ostaju, a bitovi koji su 1 u `t0` postaju ukljuceni.

#### Obrisi odredjene bitove

```asm
csrc sip, t0
```

Znaci:

```text
sip = sip AND NOT t0
```

Koristi se da se ocisti pending interrupt.

### 0.10 Sta su bit maske?

Jedan registar sadrzi mnogo nezavisnih prekidaca. Svaki prekidac je jedan bit.

Na primer:

```text
bit 1 = supervisor software interrupt
bit 9 = supervisor external interrupt
```

Izraz:

```cpp
(1 << 1)
```

pravi broj kod kog je samo bit 1 postavljen:

```text
00000010
```

Izraz:

```cpp
(1 << 1) | (1 << 9)
```

pravi masku u kojoj su postavljena oba bita.

### 0.11 `mcause` i `scause`

Kada nastane trap, cause registar govori zasto.

Primeri iz projekta:

| Vrednost | Znacenje |
| --- | --- |
| `8` | `ecall` iz U-mode-a |
| `9` | `ecall` iz S-mode-a |
| `2` | illegal instruction |
| `0x800...001` | supervisor software interrupt |
| `0x800...009` | supervisor external interrupt |

Najvisi bit govori da li je dogadjaj interrupt:

```text
najvisi bit 0 -> exception
najvisi bit 1 -> interrupt
```

### 0.12 Sta su `mepc` i `sepc`?

Kada trap prekine program, procesor mora da zapamti gde je program bio.

Ako trap ide u M-mode:

```text
mepc = prekinuta instrukcija
```

Ako trap ide u S-mode:

```text
sepc = prekinuta instrukcija
```

Kod syscall-a `ecall` zelimo da nastavimo posle `ecall`, pa handler radi:

```cpp
sepc = sepc + 4;
```

Kod timer interrupt-a zelimo da nastavimo istu prekinutu instrukciju, pa
`sepc` ne povecavamo.

### 0.13 Sta su `mret` i `sret`?

To su specijalni povratci iz trap/privilegijskog rezima.

```asm
mret
```

vraca procesor iz M-mode-a na adresu iz `mepc`.

```asm
sret
```

vraca procesor iz S-mode-a na adresu iz `sepc`.

Status registar odredjuje u koji nizi rezim se vracamo.

### 0.14 Sta je delegacija?

Procesor prvo mora da odluci ko je odgovoran za dogadjaj.

`medeleg` i `mideleg` su tabela pravila:

```text
ako je odgovarajuci bit 0 -> obradi u M-mode-u
ako je odgovarajuci bit 1 -> prepusti S-mode-u
```

Analogija:

```text
M-mode je centrala
S-mode je lokalno odeljenje
delegacija je pravilo koje pozive centrala prosledjuje odeljenju
```

Za timer:

```text
machine timer prvo ulazi u M-mode timervec
timervec podize SSIP
mideleg bit 1 prosledjuje SSIP S-mode-u
stvec vodi do supervisorTrap
```

### 0.15 Zasto timer ima dva koraka?

Hardverski timer je machine-level uredjaj. Studentski kernel radi uglavnom u
S-mode-u.

ETF biblioteka zato pravi most:

```text
hardverski timer
      |
      v
timervec u M-mode-u
      |
      | postavi sip.SSIP
      v
supervisor software interrupt
      |
      v
supervisorTrap u S-mode-u
```

M-mode deo samo:

- potvrdi hardverski timer;
- zakaze sledeci tick;
- obavesti S-mode.

Studentska logika rasporedjivanja ostaje u S-mode-u.

### 0.16 Kompletan timer tok, korak po korak

Pretpostavimo da korisnicka nit A trenutno racuna:

```text
1. Nit A radi u U-mode-u.
2. Hardverski timer dostigne zadato vreme.
3. Procesor ulazi u M-mode na adresu mtvec, odnosno timervec.
4. timervec zakazuje sledeci tick.
5. timervec postavlja sip.SSIP.
6. Zbog mideleg bit 1, SSIP pripada S-mode-u.
7. Zbog sie.SSIE, S-mode prihvata interrupt.
8. Procesor cuva adresu niti A u sepc.
9. Procesor skace na stvec, odnosno supervisorTrap.
10. supervisorTrap sacuva registre niti A u trap frame.
11. C++ timer handler sacuva sepc i sstatus.
12. Handler ocisti sip.SSIP.
13. Handler smanji kvant.
14. Handler smanji sleep brojace.
15. Ako je kvant nula, scheduler izabere nit B.
16. context_switch zameni ra i sp.
17. Kada se A kasnije vrati, handler joj restaurira sepc i sstatus.
18. supervisorTrap restaurira ostale registre.
19. sret vraca A u U-mode na instrukciju na kojoj je prekinuta.
```

### 0.17 Zasto cuvamo i trap frame i TCB context?

Oni ne cuvaju istu stvar.

Trap frame cuva kompletno trenutno stanje registara:

```text
x0-x31
```

TCB context trenutno cuva samo:

```cpp
ra
sp
```

Kada se context switch dogodi unutar trap handlera:

- trap frame ostaje na steku svake niti;
- TCB cuva gde se kernel funkcija nastavlja;
- povratkom na staru nit nastavlja se njen stari handler;
- handler restaurira CSR registre;
- assembler restaurira opste registre;
- `sret` vraca korisnicki kod.

### 0.18 Jednostavna analogija

Zamisli dve osobe koje koriste jedan radni sto.

```text
timer tick = zvono na svakih nekoliko minuta
trap frame = fotografija svega na stolu
TCB context = marker na kom koraku u proceduri je osoba stala
scheduler = red osoba koje cekaju sto
context switch = jedna osoba ustaje, druga seda
sepc = broj stranice na kojoj je osoba citala
sstatus = dozvole koje je osoba imala
```

Kada zvono zazvoni:

1. fotografisemo sto;
2. zapisemo stranicu i dozvole;
3. stavimo trenutnu osobu na kraj reda;
4. sledeca osoba nastavi sa svojim markerom;
5. kada se prva vrati, vracamo njenu fotografiju, stranicu i dozvole.

To je sustina timer preemption-a u ovom projektu.

## 1. Sta smo zeleli da postignemo

Pre timer izmena sistem je podrzavao samo kooperativnu promenu konteksta:

```text
nit A
  |
  | eksplicitno pozove thread_dispatch
  v
scheduler izabere nit B
```

Ako aktivna nit nikada ne pozove `thread_dispatch`, druge niti ne mogu da
dobiju procesor.

Timer infrastruktura dodaje dve mogucnosti:

1. **preemption**: aktivna nit automatski gubi procesor kada joj istekne kvant;
2. **sleep**: nit se skloni iz ready reda na zadati broj timer tick-ova.

Zeljeni tok:

```text
hardverski timer
       |
       v
machine timer handler
       |
       v
supervisor software interrupt
       |
       v
studentski supervisorTrap
       |
       v
timer handler
       |
       +---- smanji trenutni kvant
       |
       +---- smanji sleep vremena
       |
       +---- probudi niti kojima je sleep istekao
       |
       `---- promeni nit ako je kvant istekao
```

## 2. Sta je vec postojalo pre izmene

### 2.1 ETF machine timer infrastruktura

`lib/hw.lib` vec sadrzi:

```text
timerinit
timervec
```

Nismo pisali CLINT timer rutinu od nule.

`timerinit`:

- cita trenutni hart ID;
- programira prvi `mtimecmp`;
- definise period od 1.000.000 ciklusa;
- postavlja `mtvec` na `timervec`;
- ukljucuje machine interrupt bit;
- ukljucuje machine timer interrupt.

`timervec` na svakom hardverskom tick-u:

1. sacuva privremene registre;
2. zakaze sledeci tick;
3. postavi `sip.SSIP`;
4. restaurira registre;
5. izvrsi `mret`.

ETF rutina, dakle, pretvara machine timer u supervisor software interrupt.

### 2.2 Studentski timer kostur

Vec su postojale funkcije:

```cpp
Handlers::handle_timer_interrupt();
TCB::decrease_time_slice();
TCB::reset_time_slice();
TCB::time_slice_expired();
```

Postojao je i globalni kvant:

```cpp
uint64 TCB::time_slice = DEFAULT_TIME_SLICE;
```

`DEFAULT_TIME_SLICE` je trenutno:

```cpp
2
```

To znaci da aktivna nit moze da radi dva timer tick-a pre automatskog
dispatch-a.

### 2.3 Studentski sleep kostur

Vec su postojali:

```cpp
TCB::time_sleep(time);
Scheduler::put_sleep(thread, time);
Scheduler::update_sleeping();
```

Scheduler sadrzi poseban jednostruko povezani sleep red:

```text
sleep_head -> SleepNode -> SleepNode -> ... -> sleep_tail
```

Svaki `SleepNode` cuva:

```cpp
TCB* thread;
uint64 time;
SleepNode* next;
```

Problem nije bio odsustvo tih funkcija, vec to sto nisu bile povezane sa
syscall i timer putanjom.

## 3. Baseline problem pre RCA 008

Originalni Test 5 pravi dve niti i zatim radi busy wait:

```cpp
while (!(finished[0] && finished[1])) {}
```

U toj petlji nema `thread_dispatch`.

Bez preemption-a:

```text
userMain busy loop
      |
      | nema dispatch-a
      v
sleep niti ostaju u ready redu
      |
      v
nijedna ne pocinje
```

GDB baseline:

```text
TEST5_BUSY_LOOP_REACHED
mtvec=0x0
mie=0x200
mideleg=0x200
sie=0x200
sip=0x0
scheduler_count=3
```

Znacenje:

- scheduler ima tri ready niti;
- machine timer nije inicijalizovan;
- SSIP nije delegiran;
- SSIE nije ukljucen;
- ne postoji pending timer signal.

Zato je RCA 008 prvo resio preemption, bez aktiviranja sleep logike.

## 4. RCA 008: povezivanje timer preemption-a

Commit:

```text
268a7eb fix: wire timer preemption
```

## 5. `src/entry.S`: machine-mode bootstrap

### 5.1 Deklaracija bazne funkcije

```asm
.extern timerinit
```

Assembleru i linkeru govori da funkcija `timerinit` postoji u drugom objektu,
konkretno u ETF `hw.lib`.

Bez ove deklaracije ne bismo mogli jasno da pozovemo baznu funkciju iz
studentskog assembler fajla.

### 5.2 Delegacija sinhronih izuzetaka

```asm
li t0, (1 << 2) | (1 << 8) | (1 << 9)
csrw medeleg, t0
```

`medeleg` govori M-mode-u koje sinhrone izuzetke treba da prepusti S-mode-u.

Postavljeni bitovi:

| Bit | Uzrok | Zasto je potreban |
| --- | --- | --- |
| 2 | illegal instruction | Test 7 mora zavrsiti u supervisor trap-u, a ne u timer-only `mtvec` |
| 8 | `ecall` iz U-mode-a | korisnicki C API pozivi |
| 9 | `ecall` iz S-mode-a | bootstrap syscall pozivi iz `main` |

Bit 2 je postao posebno vazan nakon `timerinit`, zato sto `timerinit` postavlja
`mtvec` na rutinu koja zna da obradi samo timer.

Bez delegacije illegal instruction-a:

```text
Test 7 csrr sepc
      |
      v
M-mode trap
      |
      v
timervec pogresno tretira izuzetak kao timer
```

Sa delegacijom:

```text
Test 7 csrr sepc
      |
      v
supervisorTrap
      |
      v
scause = 2
```

### 5.3 Delegacija prekida

```asm
li t0, (1 << 1) | (1 << 9)
csrw mideleg, t0
```

`mideleg` govori M-mode-u koje prekide prosledjuje S-mode-u.

Postavljeni bitovi:

| Bit | Prekid |
| --- | --- |
| 1 | supervisor software interrupt, koristi ga timer |
| 9 | supervisor external interrupt, koristi ga konzola |

ETF `timervec` ne podize supervisor timer interrupt direktno. On postavlja
`sip.SSIP`, odnosno bit 1.

### 5.4 Pokretanje timer-a

```asm
call timerinit
```

Poziv se izvrsava dok je procesor jos u M-mode-u.

To je neophodno zato sto `timerinit` pristupa machine CSR registrima:

```text
mtvec
mstatus
mie
mscratch
```

Obican S-mode `main` ne sme da ih menja.

## 6. `h/riscv.h`: supervisor timer enable maska

Dodato je:

```cpp
SIE_SSIE = (1 << 1),
```

`mideleg` samo dozvoljava da interrupt dodje do S-mode-a.

`sie.SSIE` je poseban prekidac koji dozvoljava S-mode-u da zaista primi
supervisor software interrupt.

Potrebna su oba:

```text
mideleg bit 1 = M-mode prosledjuje SSIP
sie bit 1     = S-mode prihvata SSIP
```

## 7. `src/main.cpp`: trenutak ukljucivanja SSIE

Dodato je:

```cpp
Riscv::ms_sie(Riscv::SIE_SSIE);
```

Poziv se nalazi tek nakon:

```cpp
TCB::running = m;
thread_create(&t, wrapperUserMain, nullptr);
```

Redosled je nameran.

Da smo timer interrupt ukljucili ranije, handler bi mogao da pozove scheduler
pre nego sto postoje:

- validan `TCB::running`;
- glavna nit;
- korisnicka nit;
- stabilan ready red.

## 8. `src/handlers.cpp`: jedan timer tick

Timer handler sada radi:

```cpp
uint64 volatile sepc = r_sepc();
uint64 volatile sstatus = r_sstatus();
```

### Zasto se cuvaju `sepc` i `sstatus`?

Timer je asinhron. Moze da prekine bilo koju nit na bilo kojoj instrukciji.

`sepc` sadrzi adresu na koju ta nit treba da se vrati.
`sstatus` sadrzi privilegijsko stanje povratka.

Ako handler promeni nit, druga nit moze promeniti iste CSR registre. Zato
prekidana nit mora sacuvati svoje vrednosti kroz ceo context switch.

Zatim:

```cpp
mc_sip(1 << 1);
```

Cisti pending SSIP bit. Bez toga bi procesor odmah ponovo usao u isti handler.

```cpp
TCB::decrease_time_slice();
```

Smanjuje globalni kvant:

```text
2 -> 1 -> 0
```

```cpp
Scheduler::update_sleeping();
```

Ova linija je aktivirana tek kroz RCA 009. Svaki timer tick smanjuje preostalo
vreme uspavanih niti.

```cpp
if (TCB::time_slice_expired()) {
    TCB::thread_dispatch();
}
```

Kada kvant postane nula, aktivna nit se stavlja na kraj ready reda, bira se
sledeca nit i menja kontekst.

Na kraju:

```cpp
w_sstatus(sstatus);
w_sepc(sepc);
```

Kada se ova prekinuta nit jednog dana ponovo vrati iz context switch-a, CSR
registri se vracaju na njene sacuvane vrednosti.

## 9. Zasto se kvant resetuje pre context switch-a

U `TCB::thread_dispatch`:

```cpp
if (!running || running == old) {
    reset_time_slice();
    return;
}
```

Ako nema druge niti ili je scheduler vratio istu nit, nema pravog switch-a.
Ipak se zapocinje novi puni kvant.

Pre stvarnog switch-a:

```cpp
reset_time_slice();
context_switch(&old->context, &running->context);
```

Reset mora biti pre `context_switch`.

Razlog:

```text
context_switch
      |
      v
izvrsavanje nastavlja na steku druge niti
```

Kod posle poziva nece nastaviti odmah. Nastavice tek kada se stara nit kasnije
ponovo aktivira. Zato bi reset posle switch-a bio vremenski pogresan.

## 10. Ostale context switch putanje

Kvant je globalan, pa se resetuje pre svakog prelaska na drugu nit.

### `thread_exit`

```cpp
reset_time_slice();
context_switch(&old_context, &running->context);
```

Nova nit ne sme da nasledi ostatak kvanta zavrsene niti.

### `semaphore::block`

```cpp
TCB::reset_time_slice();
context_switch(&old->context, &TCB::running->context);
```

Nit koja se pokrece zato sto se prethodna blokirala dobija puni kvant.

### `TCB::time_sleep`

```cpp
reset_time_slice();
context_switch(&old->context, &running->context);
```

Nit pokrenuta nakon uspavljivanja prethodne takodje dobija puni kvant.

## 11. RCA 008 dokaz

GDB je zabelezio:

```text
timer_tick=1 scause=0x8000000000000001 time_slice=2
timer_tick=2 scause=0x8000000000000001 time_slice=1
timer_tick=3 scause=0x8000000000000001 time_slice=2
```

Treci tick vidi resetovan kvant zato sto je prethodni tick izazvao dispatch.

Zatim:

```text
SLEEPY_THREAD_STARTED_WITHOUT_MANUAL_DISPATCH
```

To dokazuje da je timer prekinuo userMain busy petlju i automatski pokrenuo
drugu ready nit.

## 12. Test 7 regresija posle timerinit

Testirana je ista instrukcija:

```asm
csrr t6,sepc
```

Odmah po ulasku u U-mode GDB je usmerio PC na tu instrukciju.

Rezultat:

```text
TEST7_ILLEGAL_INSTRUCTION_REACHED_SUPERVISOR
scause=0x2
spp=0x0
faulting instruction: csrr t6,sepc
```

To dokazuje:

- instrukcija je izvrsena iz U-mode-a;
- illegal instruction je delegiran supervisor trap-u;
- timer-only `mtvec` je nije pogresno obradio;
- Test 7 bezbednosno svojstvo ostaje ocuvano.

## 13. RCA 009: aktiviranje sleep putanje

Commit:

```text
c6bce1e fix: activate thread sleep scheduling
```

## 14. Baseline sleep bug

Pre RCA 009, prvi C `time_sleep` poziv je dao:

```text
TIME_SLEEP_RETURNED_WITHOUT_BLOCKING
result=49
a0=0x31
sleep_head=(nil)
```

`0x31` je syscall kod, a ne rezultat.

To je znacilo:

- handler nije pozvan;
- povratni slot nije promenjen;
- nit nije stavljena u sleep red;
- poziv se odmah vratio.

## 15. `src/handlers.cpp`: TIME_SLEEP dispatch

Promenjeno je:

```cpp
case TIME_SLEEP:
    handle_time_sleep(frame);
    break;
```

`handle_time_sleep`:

1. cita vreme iz sacuvanog `a1` slota;
2. poziva `TCB::time_sleep`;
3. rezultat upisuje u sacuvani `a0` slot.

Kompletan put:

```text
time_sleep C API
      |
      v
ecall 0x31
      |
      v
supervisorTrap
      |
      v
handle_time_sleep(frame)
      |
      v
TCB::time_sleep
```

## 16. `src/handlers.cpp`: update sleep reda

Aktivirano je:

```cpp
Scheduler::update_sleeping();
```

Na svakom tick-u funkcija prolazi kroz sleep red:

```cpp
if (curr->time > 0) {
    curr->time--;
}
```

Kada vreme postane nula:

```cpp
Scheduler::put(thread);
```

Nit se uklanja iz sleep reda i vraca u ready red.

## 17. `src/syscall_cpp.cpp`: C++ sleep

Promenjeno je:

```cpp
int Thread::sleep(time_t time) {
    return time_sleep(time);
}
```

C++ API sada koristi javni C syscall.

Nije pozvan:

```cpp
TCB::time_sleep
```

direktno iz U-mode-a, jer bi to preskocilo trap i ponovilo problem koji smo
ranije imali sa direktnim semaphore pozivima.

Putanja:

```text
Thread::sleep
      |
      v
time_sleep
      |
      v
ecall
      |
      v
kernel sleep
```

## 18. Kako `TCB::time_sleep` radi

Postojeci kod:

```cpp
if (time == 0) return 0;
```

Sleep od nula tick-ova se odmah zavrsava.

```cpp
TCB* old = TCB::running;
Scheduler::put_sleep(old, time);
```

Trenutna nit se upisuje u sleep red.

```cpp
running = Scheduler::get();
```

Scheduler bira sledecu ready nit.

```cpp
reset_time_slice();
context_switch(&old->context, &running->context);
```

Aktivira se sledeca nit sa punim kvantom.

Kada timer odbroji sleep vreme, stara nit se vraca u ready red. Kada ponovo
dobije procesor, `context_switch` se zavrsava i `time_sleep` vraca rezultat.

## 19. Test 5 rezultat

Originalni Test 5 je zavrsio:

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

Svaka od dve niti se izvrsila pet puta.

Posebna GDB provera:

```text
TRACKING_SLEEP_10
SLEEP_10_RESUMED ticks=14
```

Nit nije mogla da se vrati pre 10 tick-ova. Dodatni tick-ovi su vreme dok
probudjena ready nit ceka na scheduler.

## 20. Test 6 rezultat

Originalni Test 6 je pokrenut sa:

```text
producer-i: 5
bafer: 10
keyboard payload: 100 znakova
zavrsetak: Esc
```

Zavrsio je sa:

```text
Buffer deleted!
!!
TEST 6 (zadatak 4. CPP API i asinhrona promena konteksta)
```

Test 6 istovremeno proverava:

- C++ Thread objekte;
- C++ Semaphore objekte;
- `Thread::sleep`;
- timer preemption;
- producer-consumer bafer;
- cleanup i destruktore.

## 21. Trajno dodati testovi i meni

Iz zvanicnog ZIP-a su dodati:

```text
test/ThreadSleep_C_API_test.cpp
test/ThreadSleep_C_API_test.hpp
test/ConsumerProducer_CPP_API_test.cpp
test/ConsumerProducer_CPP_API_test.hpp
```

Logika testova nije menjana. Prilagodjena je samo putanja do
`printing.hpp`.

U `src/userMain.cpp` sada su:

```cpp
#define LEVEL_3_IMPLEMENTED 1
#define LEVEL_4_IMPLEMENTED 1
```

Zato kanonski meni moze da pokrene Testove 3-6.

Uklonjen je pogresan dupli include:

```cpp
#include "System_Mode_test.hpp"
```

Test 7 je vec ukljucen odgovarajucom putanjom u nivou 2.

## 22. Clean build dokaz

Kanonski clean build je napravio:

```text
kernel
build/test/ThreadSleep_C_API_test.o
build/test/ConsumerProducer_CPP_API_test.o
```

Time je potvrđeno da trajni repo, a ne samo privremeni VM harness, sadrzi
kompletne Testove 5 i 6.

## 23. Commit-i i checkpoint

```text
268a7eb fix: wire timer preemption
c6bce1e fix: activate thread sleep scheduling
4ce371c test: enable timer public tests
```

Checkpoint:

```text
timer-working
```

## 24. Nepotvrdjeni edge case-ovi

Sledece stavke nisu popravljene samo na osnovu staticke analize:

- neuspesna alokacija `SleepNode`;
- sleep kada nema druge ready niti;
- alokacija `SleepNode` koristi `sizeof` kao broj blokova;
- vrlo velike sleep vrednosti i overflow;
- puna integracija blokirajuceg `getc` poziva sa schedulerom.

One ostaju u:

```text
docs/unconfirmed-findings.md
```

Glavne javne putanje ipak prolaze:

```text
Test 5: PROLAZI
Test 6: PROLAZI
Test 7: GDB POTVRDJENO
```

## 25. Cetiri konkretna toka kroz kernel

Ovo poglavlje poredi cetiri slucaja:

1. nit dobrovoljno pozove `thread_dispatch`;
2. timer tick stigne i kvant je istekao;
3. timer tick stigne, ali kvant nije istekao;
4. stigne spoljasnji UART/keyboard interrupt.

Za svaki slucaj pratimo:

```text
ko je izazvao dogadjaj
koji trap cause nastaje
sta se desava sa PC/sepc
sta se desava sa registrima
koje funkcije se pozivaju
da li se menja aktivna nit
gde se izvrsavanje nastavlja
```

## 26. Slucaj 1: nit dobrovoljno pozove `thread_dispatch`

Ovo je **sinhrona, kooperativna promena konteksta**.

Nit sama kaze:

```text
"Sada dobrovoljno prepustam procesor."
```

### 26.1 Pocetno stanje

Pretpostavimo:

```text
trenutna nit = A
rezim        = U-mode
PC           = kod niti A
sp           = stek niti A
TCB::running = TCB niti A
```

Registri `a1-a7`, `t0-t6`, `s0-s11` mogu imati bilo koje trenutne vrednosti
niti A.

### 26.2 C API wrapper

Korisnicki kod pozove:

```cpp
thread_dispatch();
```

Wrapper:

```cpp
void thread_dispatch() {
    __asm__ volatile(
        "li a0, 0x13\n\t"
        "ecall\n\t"
        :
        :
        : "a0", "memory");
}
```

Posle prve instrukcije:

```text
a0 = 0x13
```

`0x13` je broj syscall operacije `THREAD_DISPATCH`.

### 26.3 `ecall`

Instrukcija:

```asm
ecall
```

ne poziva obicnu C++ funkciju. Procesor hardverski pravi trap.

Ako poziv dolazi iz U-mode-a:

```text
scause = 8
sepc   = adresa ecall instrukcije
SPP    = 0
PC     = stvec = Riscv::supervisorTrap
rezim  = S-mode
```

Ako isti wrapper pozove S-mode bootstrap kod:

```text
scause = 9
SPP    = 1
```

Za korisnicke niti je normalan slucaj `scause=8`.

Hardware pri ulasku u S-mode trap:

```text
sstatus.SPIE = prethodni sstatus.SIE
sstatus.SIE  = 0
sstatus.SPP  = prethodni privilegijski rezim
```

`SIE=0` privremeno sprecava novi supervisor interrupt dok se trenutni trap ne
stabilizuje.

### 26.4 Assembler trap frame

Procesor skace na:

```asm
Riscv::supervisorTrap
```

Prvo se rezervise prostor:

```asm
addi sp, sp, -256
```

Sada `sp` pokazuje na trap frame niti A.

Zatim:

```asm
sd x0,   0(sp)
sd x1,   8(sp)
sd x2,  16(sp)
...
sd x10, 80(sp)
...
sd x31,248(sp)
```

Cuva se svih 32 registra.

Posebno:

```text
frame[10] = sacuvani a0 = 0x13
frame[11] = sacuvani a1
...
```

Zivi `a0` zatim dobija adresu frame-a:

```asm
mv a0, sp
```

To je prvi argument C++ funkcije:

```cpp
Riscv::handle_supervisor_trap(frame);
```

### 26.5 C++ trap call stack

Call stack:

```text
Riscv::supervisorTrap
  -> Riscv::handle_supervisor_trap(frame)
    -> Handlers::handle_sys_call(frame)
      -> Handlers::handle_thread_dispatch()
        -> TCB::thread_dispatch()
          -> context_switch(oldContext, newContext)
```

`Riscv::handle_supervisor_trap` vidi:

```cpp
scause == 8 || scause == 9
```

i zna da je u pitanju syscall.

`Handlers::handle_sys_call` cita:

```cpp
SYS_CALL = frame[REG_A0];
```

Dobija:

```text
SYS_CALL = 0x13
```

Pre dispatch-a cuva:

```text
lokalni sepc    = hardware sepc + 4
lokalni sstatus = hardware sstatus
```

Dodavanje 4 je vazno:

```text
ecall je zavrsena instrukcija
povratak mora da nastavi posle nje
```

### 26.6 Scheduler

Kod:

```cpp
TCB* old = running;
```

Sada:

```text
old = TCB niti A
```

Ako A nije zavrsena:

```cpp
Scheduler::put(old);
```

Ready red, na primer:

```text
pre:  B -> C
posle: B -> C -> A
```

Zatim:

```cpp
running = Scheduler::get();
```

Ako scheduler vrati B:

```text
TCB::running = TCB niti B
```

Pre switch-a:

```cpp
reset_time_slice();
```

Nova nit dobija puni kvant.

### 26.7 `context_switch`

Poziv:

```cpp
context_switch(&old->context, &running->context);
```

Assembler:

```asm
sd ra, 0(a0)
sd sp, 8(a0)
ld ra, 0(a1)
ld sp, 8(a1)
ret
```

Ovde argumenti znace:

```text
a0 = adresa Context-a niti A
a1 = adresa Context-a niti B
```

Prve dve instrukcije:

```text
A.context.ra = trenutni ra
A.context.sp = trenutni sp
```

`sp` niti A trenutno pokazuje unutar njenog trap/handler steka.

Sledece dve:

```text
ra = B.context.ra
sp = B.context.sp
```

Posle `ret`:

```text
PC = ra niti B
stek = stek niti B
```

### 26.8 Ako je B ranije bila suspendovana

Njeni `ra/sp` pokazuju u njen stari `context_switch` poziv.

Ona nastavlja:

```text
stari context_switch se vraca
stari handler restaurira B.sepc/B.sstatus
supervisorTrap restaurira B registre
sret vraca B u U-mode
```

### 26.9 Ako B nikada ranije nije radila

Pri kreiranju je postavljeno:

```text
B.context.ra = TCB::thread_wrapper
B.context.sp = vrh B steka
```

Zato `ret` iz `context_switch` prvi put ulazi u:

```cpp
TCB::thread_wrapper();
```

`thread_wrapper` pripremi U-mode i pozove telo niti B.

### 26.10 Sta se desava sa niti A?

Ona nije nestala. Njen:

```text
trap frame
handler call stack
sepc/sstatus
context ra/sp
```

ostaju na njenom steku.

Kada scheduler jednog dana ponovo izabere A:

```text
context_switch ucita A.ra/A.sp
handle_sys_call restaurira A.sepc/A.sstatus
supervisorTrap restaurira A registre
sret vraca A posle njenog ecall-a
```

### 26.11 Analogija

Radnik A dobrovoljno podigne ruku:

```text
"Zavrsio sam trenutni korak, neka sada radi B."
```

Sekretar:

1. fotografise A sto;
2. zapise gde je A stala;
3. stavi A na kraj reda;
4. donese B fasciklu;
5. B nastavlja svoj posao.

## 27. Slucaj 2: timer tick stigne i kvant je istekao

Ovo je **asinhrona, preemptivna promena konteksta**.

Nit nije trazila dispatch. Hardverski timer je prekida.

### 27.1 Pocetno stanje

Pretpostavimo:

```text
trenutna nit = A
rezim        = U-mode
time_slice   = 1
PC           = proizvoljna instrukcija niti A
sp           = stek niti A
```

### 27.2 Hardverski machine timer

CLINT timer dostigne `mtimecmp`.

Procesor prvo ide u M-mode:

```text
mcause = interrupt + machine timer cause
mepc   = mesto na kom je A prekinuta
PC     = mtvec = timervec
```

Ovo jos nije studentski `supervisorTrap`.

### 27.3 ETF `timervec`

`timervec` koristi `mscratch` da privremeno sacuva registre:

```asm
csrrw a0, mscratch, a0
sd a1, 0(a0)
sd a2, 8(a0)
sd a3, 16(a0)
```

Zatim cita staro `mtimecmp`, dodaje period i zakazuje sledeci tick.

Najvaznija linija:

```asm
li a1, 2
csrw sip, a1
```

Broj 2 ima postavljen bit 1:

```text
sip.SSIP = 1
```

Time M-mode govori:

```text
"S-mode ima timer posao."
```

`timervec` restaurira privremene registre i radi:

```asm
mret
```

### 27.4 Prelazak u supervisor trap

Zbog:

```text
mideleg bit 1 = 1
sie.SSIE      = 1
```

pending SSIP ulazi u S-mode.

Procesor postavlja:

```text
scause = 0x8000000000000001
sepc   = adresa na kojoj A treba da nastavi
SPP    = prethodni rezim
PC     = stvec = supervisorTrap
```

Za A koja radi u U-mode-u:

```text
SPP = 0
```

### 27.5 Trap frame

`supervisorTrap` ponovo cuva svih 32 registra.

Razlika u odnosu na syscall:

```text
a0 nije syscall kod
a0 je obicna trenutna vrednost niti A
```

Trap frame mora da je sacuva nepromenjenu.

### 27.6 Timer call stack

```text
timervec
  -> supervisorTrap
    -> Riscv::handle_supervisor_trap(frame)
      -> Handlers::handle_timer_interrupt()
        -> TCB::thread_dispatch()
          -> context_switch(...)
```

`Riscv::handle_supervisor_trap` prepoznaje:

```cpp
scause == 0x8000000000000001
```

### 27.7 Cuvanje CSR registara

Timer handler radi:

```cpp
uint64 sepc = r_sepc();
uint64 sstatus = r_sstatus();
```

Za razliku od syscall-a:

```text
sepc se NE povecava za 4
```

Timer nije instrukcija koju je A pozvala. On je stigao izmedju instrukcija.
A mora da nastavi tacno tamo gde ju je hardware prekinuo.

### 27.8 Obrada tick-a

```cpp
mc_sip(1 << 1);
```

Posle ovoga:

```text
sip.SSIP = 0
```

Zatim:

```cpp
TCB::decrease_time_slice();
```

Pocetna vrednost:

```text
time_slice = 1
```

Nova vrednost:

```text
time_slice = 0
```

Sleep red se takodje azurira:

```cpp
Scheduler::update_sleeping();
```

Svaki sleep cvor dobija:

```text
time = time - 1
```

### 27.9 Kvant je istekao

Uslov:

```cpp
TCB::time_slice_expired()
```

vraca:

```text
true
```

Timer handler zato poziva:

```cpp
TCB::thread_dispatch();
```

Dalji scheduler i `context_switch` tok je isti kao u Slucaju 1.

Glavna razlika:

```text
Slucaj 1: A je sama trazila promenu
Slucaj 2: timer je prinudno prekinuo A
```

### 27.10 Povratak A

Kada se A kasnije vrati:

```cpp
w_sstatus(sstatus);
w_sepc(sepc);
```

vracaju njene CSR vrednosti.

Assembler trap rutina zatim restaurira sve opste registre, ukljucujuci
originalni `a0`.

```asm
sret
```

vraca A na njen prekinuti PC.

### 27.11 Analogija

A nije dobrovoljno ustala. Zazvonilo je zvono:

```text
"Tvoje vreme za stolom je isteklo."
```

Sekretar fotografise sto, cuva stranicu i dozvole, pomera A na kraj reda i
daje sto B.

## 28. Slucaj 3: timer tick stigne, ali kvant nije istekao

Pocetak je potpuno isti kao u Slucaju 2:

```text
machine timer
timervec
sip.SSIP
supervisorTrap
handle_timer_interrupt
```

### 28.1 Pocetno stanje kvanta

Pretpostavimo:

```text
time_slice = 2
```

Posle:

```cpp
TCB::decrease_time_slice();
```

dobijamo:

```text
time_slice = 1
```

### 28.2 Provera

```cpp
TCB::time_slice_expired()
```

vraca:

```text
false
```

Zato se NE pozivaju:

```text
TCB::thread_dispatch
context_switch
```

### 28.3 Sta se ipak dogodilo?

Iako se nit nije promenila:

- sledeci machine tick je zakazan;
- SSIP je postavljen pa ociscen;
- kvant je smanjen;
- sleep cvorovi su smanjeni;
- niti kojima je sleep istekao mogu biti vracene u ready red.

### 28.4 Povratak

Handler vraca:

```cpp
w_sstatus(sstatus);
w_sepc(sepc);
```

`supervisorTrap` restaurira:

```text
a0-a7
t0-t6
s0-s11
ra
gp
tp
```

Zatim:

```asm
sret
```

Ista nit A nastavlja.

Pre i posle, sa stanovista niti A:

```text
PC      = isti nastavak programa
sp      = njen isti stek
ra      = njena ista povratna adresa
a0-a7   = iste vrednosti
running = i dalje A
```

Jedina bitna trajna promena za scheduler:

```text
time_slice: 2 -> 1
```

### 28.5 Analogija

Zvono zazvoni, ali sekretar pogleda sat i kaze:

```text
"Imas jos jedan interval. Nastavi."
```

Fotografija je napravljena i odmah vracena. Radnik A ne napusta sto.

## 29. Slucaj 4: spoljasnji prekid sa tastature

Ovo je asinhroni interrupt, ali obicno ne menja aktivnu studentsku nit.

### 29.1 Hardware tok

Korisnik pritisne taster.

```text
tastatura/QEMU
      |
      v
UART
      |
      v
PLIC
      |
      v
supervisor external interrupt
```

PLIC je kontroler koji prikuplja spoljasnje interrupt izvore i govori
procesoru koji uredjaj trazi obradu.

Za UART:

```text
IRQ = 10
```

### 29.2 Ulazak u supervisor trap

Zbog:

```text
mideleg bit 9 = 1
sie.SEIE      = 1
```

procesor postavlja:

```text
scause = 0x8000000000000009
sepc   = mesto na kom je trenutni kod prekinut
PC     = stvec = supervisorTrap
```

Ako je prekinut U-mode kod:

```text
SPP = 0
```

Ako je konzolna biblioteka privremeno dozvolila interrupt dok `__getc` ceka u
S-mode-u:

```text
SPP = 1
```

Oba slucaja koriste isti `supervisorTrap`.

### 29.3 Trap frame

Assembler cuva svih 32 registra trenutne niti.

Opet:

```text
a0 nije syscall kod
a0 je trenutna vrednost prekinutog koda
```

Mora biti restauriran bez promene.

### 29.4 Call stack

```text
Riscv::supervisorTrap
  -> Riscv::handle_supervisor_trap(frame)
    -> Handlers::handle_console_interrupt()
      -> console_handler()
        -> plic_claim()
        -> uartintr()
        -> plic_complete()
```

### 29.5 `plic_claim`

```cpp
int irq = plic_claim();
```

PLIC vraca:

```text
irq = 10
```

To znaci da UART trazi obradu.

### 29.6 `uartintr`

`uartintr` cita pristigle znakove iz UART registra i predaje ih konzolnom
baferu.

Pojednostavljeno:

```text
UART RX registar -> console input buffer
```

Nit koja je pozvala `getc` kasnije cita isti bafer.

### 29.7 `plic_complete`

Posle obrade:

```cpp
plic_complete(10);
```

govori PLIC-u:

```text
"IRQ 10 je obradjen."
```

Bez toga bi PLIC mogao da nastavi da prijavljuje isti interrupt.

### 29.8 Da li se menja nit?

U studentskom console handleru nema:

```text
TCB::thread_dispatch
context_switch
```

Zato se obicno vraca ista nit.

Trap rutina restaurira registre i radi:

```asm
sret
```

Prekinuti kod nastavlja sa istim:

```text
sp
ra
a0-a7
PC nastavkom
TCB::running
```

Ako je kod cekao u `__getc`, sada moze da vidi znak u konzolnom baferu i vrati
ga pozivaocu.

### 29.9 Analogija

Radnik A radi za stolom. Kurir donese pismo.

Sekretar:

1. kratko fotografise A sto;
2. preuzme pismo;
3. stavi ga u A ulaznu korpu;
4. potvrdi kuriru prijem;
5. vrati A istu fotografiju.

A nastavlja da radi. Nije izgubila sto i druga nit nije automatski sela.

## 30. Poredjenje sva cetiri slucaja

| Osobina | Dobrovoljni dispatch | Timer, kvant istekao | Timer, kvant ostao | Tastatura |
| --- | --- | --- | --- | --- |
| Ko izaziva? | trenutna nit | hardware timer | hardware timer | UART/PLIC |
| Sinhrono/asinhrono | sinhrono | asinhrono | asinhrono | asinhrono |
| `scause` | `8` ili `9` | `0x800...001` | `0x800...001` | `0x800...009` |
| Syscall kod u `a0` | `0x13` | ne | ne | ne |
| `sepc + 4` | da | ne | ne | ne |
| Smanjuje kvant | ne | da, do nule | da, ali ostaje >0 | ne |
| Azurira sleep red | ne | da | da | ne |
| Poziva scheduler | da | da | ne | ne |
| Menja `sp/ra` | da | da | ne | ne |
| Menja aktivnu nit | da, ako druga postoji | da, ako druga postoji | ne | ne |
| Povratak | kasnije, posle `ecall` | kasnije, na prekinuti PC | odmah, ista nit | odmah, ista nit |

## 31. Najkraca mentalna slika

### Dobrovoljni dispatch

```text
Radnik: "Dajem sto sledecem."
```

### Timer, kvant istekao

```text
Zvono: "Vreme ti je isteklo, ustani."
```

### Timer, kvant nije istekao

```text
Zvono: "Provera vremena; mozes da nastavis."
```

### Tastatura

```text
Kurir: "Stiglo je pismo; primi ga i nastavi isti posao."
```
