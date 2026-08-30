# Nepotvrdjeni nalazi

Poslednje azuriranje: 2026-08-30

Ovaj dokument prati sumnjiva mesta koja jos nisu dokazana minimalnim runtime,
GDB ili PDF dokazom. Stavke ovde nisu odobrene za popravku.

Kada se nalaz potvrdi:

1. sacuvati dokaz u `debug-artifacts/`;
2. napisati zaseban RCA u `docs/rca/`;
3. traziti odobrenje;
4. primeniti jednu minimalnu popravku i napraviti zaseban commit;
5. ukloniti stavku iz ovog dokumenta ili je oznaciti kao prebacenu u RCA.

## Brzi pregled

| ID | Oblast | Sumnja | Moguca posledica | Status |
| --- | --- | --- | --- | --- |
| SEM-001 | `sem_open` | Cita neinicijalizovan `*handle` | Undefined behavior | Nepotvrdjeno |
| SEM-002 | Interne alokacije | `sizeof` se prosledjuje kao broj blokova | Veliko rasipanje heap-a | Nepotvrdjeno |
| SEM-003 | `semaphore::block` | Neuspesna alokacija moze izgledati kao uspesan wait | Nit nastavi bez resursa | Nepotvrdjeno |
| SEM-004 | `semaphore::block` | Ne proverava prazan scheduler | Null dereference | Nepotvrdjeno |
| SEM-005 | Brojac semafora | `unsigned` vrednost se cuva u `int` | Overflow ili negativan brojac | Nepotvrdjeno |
| SEM-006 | Prosireni API | `sem_wait_n`, `sem_signal_n` i close sa waiter-ima nisu testirani | Skrivena greska u edge-case putanji | Nema pokrica |
| CON-001 | `getc` i scheduler | Blokirajuci `__getc` zadrzava ceo kooperativni sistem | Ostale niti ne napreduju dok nema unosa | Primeceno, zahtev nije potvrdjen |

## SEM-001: citanje neinicijalizovanog handle-a

Kod:

```cpp
sem->handle = *handle;
```

Kod clanova kao sto je `Buffer::itemAvailable`, `*handle` moze biti
neinicijalizovan. Vrednost se cuva u privatno polje `semaphore::handle`, koje
se nigde ne koristi.

### Zasto testovi prolaze?

Vrednost se ne dereferencira niti utice na semaphore logiku. Testovi 3 i 4
zato ne vide vidljivu posledicu.

### Potreban dokaz

Pokrenuti mali `sem_open` reproduktor pod UBSan-om ako je podrzan u zvanicnom
okruzenju, ili dokazati iz PDF/API zahteva da objekat ne sme da cita prethodnu
vrednost izlaznog handle-a.

### Moguci smer popravke

Ukloniti neupotrebljeno `handle` polje i ovu dodelu. Ne menjati bez zasebnog
RCA-a.

## SEM-002: bajtovi se koriste kao broj blokova

Kod:

```cpp
Mem::mem_alloc(sizeof(semaphore));
Mem::mem_alloc(sizeof(Blocked));
```

Interni `Mem::mem_alloc` ocekuje broj blokova, dok `sizeof` vraca bajtove.
Slican obrazac postoji u `Scheduler::put`.

### Zasto testovi prolaze?

Alocira se mnogo vise memorije nego sto je potrebno, ali pokazivac je validan.
Heap je dovoljno veliki za javne testove.

### Potreban dokaz

Izmeriti pomeranje heap fragmenata ili broj uspesnih ponovljenih
`sem_open`/block operacija i uporediti ga sa ocekivanim brojem blokova.

### Moguci smer popravke

Proslediti:

```cpp
Mem::calculate_blocks(sizeof(T))
```

uz kontrolisan pristup helperu. Semaphore i Scheduler nalaze razdvojiti ako
reprodukcije pokazu razlicite posledice.

## SEM-003: neuspesna alokacija blocked cvora

Kod:

```cpp
TCB::running->on_semaphore = 1;
auto new_blocked = (Blocked*)Mem::mem_alloc(sizeof(Blocked));
if (!new_blocked) return;
```

Stanje niti se menja pre alokacije. Ako alokacija ne uspe, `block` je `void` i
pozivalac ne dobija jasnu gresku. `sem_wait` zatim moze da vrati uspeh iako
resurs nije uzet.

### Zasto testovi prolaze?

Javni testovi ne iscrpljuju heap pri pravljenju blocked cvora.

### Potreban dokaz

Namerno iscrpeti heap, zatim pozvati `sem_wait` nad semaforom sa vrednoscu
nula i zabeleziti rezultat i stanje niti.

### Moguci smer popravke

Neka `block` vraca status. Stanje niti menjati tek posle uspesne alokacije, a
gresku propagirati kroz `sem_wait` i `sem_wait_n`.

## SEM-004: scheduler nema spremnu nit

Kod:

```cpp
TCB::running = Scheduler::get();
context_switch(&old->context, &TCB::running->context);
```

Ako `Scheduler::get()` vrati `nullptr`, sledi dereferenciranje null
pokazivaca.

### Zasto testovi prolaze?

Testovi 3 i 4 u trenutku blokiranja imaju vise drugih spremnih niti.

### Potreban dokaz

Napraviti scenario u kom jedina aktivna nit radi `sem_wait` nad nulom i GDB-om
proveriti rezultat `Scheduler::get()`.

### Moguci smer popravke

Pre context switch-a proveriti sledecu nit i vratiti jasnu gresku uz rollback,
ili uvesti stalnu idle nit. Izbor zavisi od zahteva PDF-a.

## SEM-005: signed brojac za unsigned API

Kod:

```cpp
int init;
sem->init = (int)init;
```

Javni API prima `unsigned`, ali interna vrednost je `int`. Velike vrednosti
mogu postati negativne, a `sem_signal_n` moze prekoraciti opseg.

### Zasto testovi prolaze?

Koriste samo male vrednosti: nula, jedan i velicinu malog bafera.

### Potreban dokaz

Otvoriti semafor sa vrednoscu vecom od `INT_MAX`, zatim proveriti `sem_wait`,
ili signalizirati vrednost blizu granice i posmatrati overflow.

### Moguci smer popravke

Koristiti odgovarajuci unsigned ili `uint64` brojac i definisati ponasanje pri
overflow-u prema specifikaciji.

## SEM-006: nepokrivene semaphore putanje

Testovi 3 i 4 pokrivaju:

```text
sem_open
sem_wait
sem_signal
sem_close nakon zavrsetka svih niti
```

Ne pokrivaju:

```text
sem_wait_n
sem_signal_n
sem_close dok niti cekaju
vise waiter-a sa razlicitim n vrednostima
```

Ovo nije potvrda buga, vec rupa u test pokricu.

### Potreban dokaz

Napraviti male, odvojene dijagnosticke testove za svaku putanju kroz javni C
API. Ne koristiti direktne pozive interne klase `semaphore`.

## CON-001: `getc` zadrzava ostale niti

Primeceno ponasanje:

```text
producerKeyboard -> getc -> __getc ceka znak
```

Dok `__getc` ceka unutar syscall handlera, studentski scheduler ne bira druge
kooperativne niti. Zato producer-i sa ciframa pocinju vidljivo da napreduju tek
kada stigne unos ili se keyboard producer blokira na punom baferu.

### Zasto testovi prolaze?

Testovi 3 i 4 su interaktivni i svakako zahtevaju Esc za zavrsetak. Posle
unosa svi producer-i, consumer, semafori i destruktori zavrse.

### Potreban dokaz

Proveriti PDF zahtev za blokirajuci `getc`. Ako je zahtev da se blokira samo
pozivajuca nit, GDB-om dokazati da druge ready niti ne napreduju dok nema
ulaza.

### Moguci smer popravke

Povezati cekanje konzolnog ulaza sa schedulerom tako da se blokira samo
keyboard nit. Puna konzolna arhitektura je van prvobitnog opsega i ne treba je
menjati bez jasnog zahteva.
