# Plan: evidence-first debugging OS1 zadataka 1 i 2

## Trenutno stanje pri handoff-u

`bootstrap-working` milestone, memorijska dijagnostika i javni testovi 1-7 su
zavrseni. Rad je spreman za zavrsni read-only pregled.

Zavrseno:

- provenance istorija, lokalni projekat, QEMU/SSH/rsync workflow i zvanicna
  Ubuntu 20.04 x86_64 VM ostaju kao ranije dokumentovani;
- RCA 001 je potvrdio da `supervisorTrap.s` ne ulazi u clean build; rename u
  `supervisorTrap.S` je 100% content-preserving i commitovan kao `0f7bd99`;
- RCA 002 je GDB-om dokazao da trap restore pregazi validan `mem_alloc`
  rezultat starim syscall kodom; trap-frame popravka je commitovana kao
  `9edfa48`;
- RCA 003 je dokazao da U-mode `ecall` nije delegiran S-mode-u; `medeleg`
  popravka je commitovana kao `866473b`;
- RCA 004 je dokazao da bazna konzola nije inicijalizovana i da
  `console_write` skace kroz null pokazivac; `consoleinit` bootstrap je
  commitovan kao `aca8889`;
- RCA 005 je dokazao da konzolni ulaz nema povezanu PLIC/delegation/handler
  putanju; popravka je commitovana kao `6dc8df7`;
- svaki funkcionalni diff je prosao nezavisni code review;
- poslednji `make clean && make` je uspeo iz cistog izvornog koda;
- napravljeni su novi `build/src/supervisorTrap.o` i `kernel`;
- GDB regresija je potvrdila validan heap stack, U-mode syscall putanju,
  supervisor external interrupt, `console_handler` i `uartintr`;
- zavrsna provera je sacekala stvarni meni, zatim poslala `0` i dobila:

```text
Unesite broj testa? [1-7]
Niste uneli odgovarajuci broj za test
```

- odvojeni memorijski harness je kroz javni `mem_alloc`/`mem_free` put potvrdio
  alokaciju, poravnanje, nepreklapanje, reuse, coalescing, exhaustion i
  oporavak; zavrsio je sa `MEMORY_DIAGNOSTICS_PASS`;
- originalni javni Test 1 je zavrsen do svoje zavrsne oznake;
- originalni javni Test 2 je zavrsen do svoje zavrsne oznake, ukljucujuci
  destruktore i `delete` pozive;
- Test 7 je GDB-om potvrdjen: `MPP=0`, `mcause=2`, a `mepc` pokazuje na
  privilegovanu `csrr t6,sepc` instrukciju;
- detaljna evidencija testova je u `docs/test-status.md`;
- dijagnosticki harness i sirovi logovi ostaju u gitignored
  `debug-artifacts/` i ne ulaze u predajni kod.
- RCA 008 je povezao ETF timer, asinhroni preemption i cuvanje trap CSR
  registara; commitovan je kao `268a7eb`;
- RCA 009 je aktivirao postojeci sleep handler, sleep queue tick i C++ sleep
  wrapper; commitovan je kao `c6bce1e`;
- originalni Testovi 5 i 6 prolaze, a zvanicni test fajlovi su uvezeni sa samo
  potrebnim include prilagodjavanjem;
- nivoi 3 i 4 su ukljuceni u kanonskom `userMain`.

Tag `bootstrap-working` oznacava prvi meni checkpoint. Tag `memory-working`
oznacava stanje u kom memorijska dijagnostika i ciljani javni testovi prolaze.
Git status na oba checkpoint-a mora biti cist.

Sledeci korak je read-only pregled celog implementiranog opsega prema PDF-u.
Posebno proveriti poznatu `thread_create` ABI neuskladjenost (`a6/a7` umesto
`a3/a4`) i zahtev za uklanjanje `lib/mem.lib` kada se koristi studentski
alokator. Nijednu novu izmenu ne primenjivati bez zasebnog dokaza, RCA-a i
odobrenja.

## Problem i cilj

Sestrin projekat je ranije prolazio javne testove 1, 2 i 7 dok su se funkcije jezgra pozivale direktno. Posle prelaska na propisani put `C API -> ecall -> ABI/trap -> kernel`, program puca pre nego sto prikaze meni za izbor testa.

Rad je podeljen na dva jasno odvojena cilja:

1. Prvi milestone je cist build iz trenutnog izvornog koda i dolazak do menija za izbor testa. Kada se meni pojavi, pravi se checkpoint `bootstrap-working` i rad se zaustavlja radi pregleda.
2. Tek posle posebnog odobrenja testiraju se i popravljaju memorija, javni test 1, javni test 2 i javni test 7, tim redosledom.

Opseg su samo:

- zadatak 1: alokacija memorije;
- zadatak 2: niti, sinhrona promena konteksta i C/C++ API za niti;
- ABI, trap i bootstrap delovi od kojih ova dva zadatka zavise.

Semafori, sleep, asinhroni tajmer i puna konzola nisu u opsegu, osim ako direktno sprecavaju build ili bootstrap zadataka 1 i 2. Zadatke 1 i 2 PDF vrednuje sa ukupno 15 poena, sto je manje od praga od 20 poena za uspesnu odbranu; prosirenje na zadatak 3 nije deo ovog plana.

## Ulazni materijali i utvrdjeno stanje

- `~/Downloads/project-base-v1.1.zip`: zvanicna prazna baza, Makefile, linker skripta i ETF biblioteke.
- `~/Downloads/os (2).zip`: zamrznuti snapshot sestrinog projekta.
- `~/Downloads/javniTestovi_2024_1_1.zip`: zvanicni javni testovi.
- `~/Downloads/OS_student.zip`: VMware Ubuntu 20.04.3 x86_64 VM.

Vec je utvrdjeno:

- Mac je Apple Silicon `arm64`; VM je Intel/AMD `x86_64`, pa mora da radi kroz QEMU TCG emulaciju.
- VM ZIP je kompletan i bez gresaka.
- VMware disk je raspakovan i konvertovan u ispravan `qcow2` disk. Ubuntu se podigao i login je uspeo.
- Koristi se jedna radna VM; ne pravi se dodatna kopija diska. Originalni ZIP ostaje dovoljan za ponovno formiranje VM-a.
- UTM i Homebrew QEMU su instalirani, ali se za ponovljiv rad koristi direktna QEMU launch skripta.
- Baza (`Makefile`, `kernel.ld`, biblioteke i zaglavlja) nije menjana u sestrinom ZIP-u.
- Testovi 1, 2 i 7 imaju istu logiku kao zvanicni testovi; promenjene su samo include putanje. Sestrin `userMain.cpp` ima podesene nivoe 1-2 i dodatni debug ispis.
- Postojeci `build/`, `kernel` i `kernel.asm` su generisani artefakti i ne smeju se koristiti kao dokaz da trenutni izvorni kod moze cisto da se prevede.
- Sestra nece paralelno menjati ovaj baseline.

## Pravila rada

### Evidence-first

Kod se menja samo kada je problem potvrdjen na jedan od dva nacina:

1. minimalna reprodukcija i GDB/build dokaz pokazuju pogresno ponasanje; ili
2. trenutni kod direktno krsi nedvosmislen zahtev PDF-a, uz citat tacnog zahteva.

Ostale sumnjive tacke ostaju hipoteze. Nema spekulativnog popravljanja niti kopiranja tudjih gotovih OS1 resenja.

### Approval gate za svaki bug

Za svaki potvrdjeni bug redosled je:

1. reprodukovati problem na neizmenjenom kodu;
2. sacuvati minimalan build/GDB dokaz;
3. napisati jednostavan RCA;
4. pokazati RCA i predlozenu minimalnu popravku korisniku;
5. sacekati izricito odobrenje pre izmene;
6. primeniti samo odobrenu izmenu;
7. pregledati diff liniju po liniju i objasniti svaku izmenu;
8. pokrenuti nezavisni code-review agent nad tim diff-om;
9. pokrenuti minimalni reproduktor, zatim sve do tada prolazne regresione testove;
10. tek tada napraviti jedan Git commit za taj bug.

Jedan bug dobija jedan commit. Nezavisni review i testovi moraju biti cisti pre commit-a.

### RCA format

Svaki RCA se cuva u `docs/rca/` i pise jednostavnim srpskim jezikom. Strucni termini ostaju navedeni na engleskom. Dokument sadrzi:

- vidljivi simptom;
- najmanju reprodukciju;
- konkretan dokaz iz builda, GDB-a ili PDF-a;
- root cause prostim jezikom;
- mali ASCII dijagram toka podataka/registara;
- objasnjenje zasto je raniji direktni poziv radio, a sistemski put nije;
- razmatrane popravke i zasto je izabrana ispravna;
- minimalni diff;
- dokaz da je popravka uspela;
- 2-3 pitanja za samoproveru, bez odgovora odmah ispod.

Sirovi logovi se cuvaju u gitignored `debug-artifacts/`; u RCA ulaze samo kratki relevantni izvodi.

## Todo 1: Formirati proverljiv Git projekat

Napraviti `~/code/os1-project` i registrovati ga kao lokalni Copilot projekat sa jednom trajnom coding sesijom.

Git istorija se formira pre prve funkcionalne izmene:

1. commit zvanicne baze;
2. commit sestrinog izvornog koda preko baze;
3. commit zvanicnih javnih testova sa samo neophodnim promenama include putanja i `userMain` konfiguracijom za nivoe 1-2.

Ne uvoziti:

- `build/`;
- `kernel`;
- `kernel.asm`;
- generisani `.gdbinit`;
- IDE i privremene fajlove.

Sacuvati SHA-256 i listing tri ulazne arhive radi provenance-a, ali ne kopirati ZIP fajlove u Git.

`userMain` treba da koristi zvanicnu test logiku, bez sestrinog dodatnog ispisa, sa ukljucenim nivoima 1 i 2 i iskljucenim nivoima 3 i 4.

## Todo 2: Napraviti ponovljivo VM/SSH okruzenje

Napraviti host launch skriptu koja koristi postojeci `qcow2` i proverenu konfiguraciju:

- QEMU x86_64 sa TCG emulacijom;
- 2 vCPU i 4 GB RAM-a, kao u originalnoj VM konfiguraciji;
- NAT mrezu;
- SSH prosledjivanje samo sa `127.0.0.1:2222` na guest port 22;
- host direktorijum `~/code/os1-project` izlozen VM-u kao deljeni direktorijum;
- graficki prozor samo za login i hitne intervencije.

Pri prvom pokretanju:

1. komandom `whoami` utvrditi username;
2. proveriti da li `sshd` vec postoji;
3. ako nedostaje, instalirati samo `openssh-server`, bez `apt upgrade`;
4. napraviti namenski SSH kljuc koji se ne cuva u Git-u i dodati samo njegov javni deo u VM;
5. proveriti SSH preko `127.0.0.1:2222`;
6. montirati deljeni direktorijum preko virtio/9p.

Ako 9p nije podrzan u ovoj staroj VM, fallback je automatski `rsync` host radne kopije u guest radni direktorijum pre svakog builda. Host Git kopija ostaje jedini source of truth; nema rucnog razmenjivanja ZIP-ova.

Ne menjati verzije RISC-V GCC-a, QEMU-a, GDB-a niti ostalih razvojnih alata u VM. Evidentirati njihove postojece verzije i proveriti:

- `make`;
- jedan od RISC-V GCC toolchain prefiksa koje Makefile podrzava;
- `qemu-system-riscv64`;
- `gdb-multiarch`.

## Todo 3: Uspostaviti cist build baseline

Prvi merodavan build mora krenuti samo od izvornog koda:

```sh
make clean
make
```

Ne pokretati stari `kernel` iz ZIP-a kao dokaz ispravnosti.

Prva visoko pouzdana hipoteza je naziv `src/supervisorTrap.s`: PDF i Makefile ocekuju studentske asemblerske fajlove sa ekstenzijom `.S`, a `hw.lib` ne daje simbol `Riscv::supervisorTrap`. Cist build treba da potvrdi ili odbaci ovu hipotezu. Ako je potvrdi:

- sacuvati linker/build dokaz;
- napisati RCA;
- traziti odobrenje;
- tek potom predloziti preimenovanje `.s` u `.S`;
- ponoviti cist build i regresiju.

Makefile, linker skripta i ETF biblioteke ostaju nepromenjeni, uz jedan uslovni izuzetak kasnije: ako zadatak 1 zaista koristi sestrin alokator, `lib/mem.lib` mora biti uklonjen iz `LIBS`, kako PDF zahteva. To dobija zaseban dokaz, RCA, odobrenje i commit. `hw.lib` i `console.lib` ostaju.

## Todo 4: Reprodukovati pre-menu runtime pad u GDB-u

Kada cist build uspe, prvo pokrenuti neizmenjeni kod. Ako ne stigne do menija, koristiti zvanicni `make qemu-gdb` i dve SSH sesije: jednu za RISC-V QEMU, drugu za `gdb-multiarch`.

Pocetni breakpoint-i:

- `main`;
- `Riscv::supervisorTrap`;
- `Riscv::handle_supervisor_trap`;
- `Handlers::handle_sys_call`;
- `Handlers::handle_mem_alloc`;
- `Handlers::handle_thread_create`;
- `TCB::create_thread`;
- `TCB::thread_dispatch`;
- `Handlers::handle_exception`.

Za prvi i drugi `thread_create`, prvi `mem_alloc` i prvi `thread_dispatch` sacuvati:

- `a0-a7`;
- `sp` i `ra`;
- `scause`, `sepc`, `sstatus` i `stval`;
- sačuvani trap frame pre i posle C++ handlera;
- vrednost pokazivaca steka upisanu u novi TCB;
- adresu instrukcije na kojoj nastaje izuzetak.

### Prva runtime hipoteza koju treba dokazati

Trenutna trap rutina:

1. sacuva stari `a0` pre poziva C++ handlera;
2. handler upise povratnu vrednost sistemskog poziva u zivi `a0`;
3. trap rutina zatim restaurira stari `a0` i pregazi rezultat.

Ocekivani dokaz, ako je hipoteza tacna:

- `mem_alloc` handler izracuna validnu heap adresu;
- pred `sret` sačuvani `a0` i dalje sadrzi `0x01`, kod poziva;
- C API zato primi pokazivac `0x1`;
- novi TCB dobije stack pointer oko `0x1001`;
- prvi upis na tom steku izazove fault pre ispisa menija.

Ne primenjivati popravku samo na osnovu staticke analize. Ako GDB potvrdi hipotezu, preferirana ispravna arhitektura je prosledjivanje pokazivaca na sacuvani trap frame C++ handleru, citanje ABI argumenata iz odgovarajucih slotova i upis povratne vrednosti u sacuvani `a0` slot pre restauracije. Ne koristiti krhki trik "nemoj restaurirati a0", jer bi prekidi mogli da pokvare korisnicki registar.

I ova popravka prolazi puni RCA/approval/diff/review/test gate.

Ako hipoteza nije potvrdjena, nastaviti binarnu pretragu istim breakpoint-ima kroz:

1. instaliranje `stvec`;
2. prvi `thread_create` bez korisnickog steka;
3. postavljanje `TCB::running`;
4. alokaciju steka druge niti;
5. ubacivanje niti u scheduler;
6. prvi context switch;
7. prelazak u user mode;
8. prvi poziv `userMain`.

## Todo 5: Zakljucati prvi milestone

Prvi milestone je ispunjen tek kada:

- `make clean && make` uspe iz izvornog koda;
- `make qemu` podigne trenutni kernel;
- prikaze se originalni meni za izbor testa;
- meni moze da primi cifru;
- nijedan stari build artefakt nije potreban.

Tada:

1. napraviti checkpoint `bootstrap-working`;
2. pokazati sve RCA dokumente i commit-e do tog trenutka;
3. zaustaviti dalji rad;
4. sacekati posebno odobrenje za testiranje memorije i niti.

Regularan zavrsetak programa jos nije uslov ovog prvog milestone-a.

## Todo 6: Testirati zadatak 1 kroz javni C API

Posle odobrenja za nastavak dodati odvojene dijagnosticke testove koji koriste samo:

```cpp
mem_alloc(...)
mem_free(...)
```

Testovi ne smeju direktno pozivati internu klasu alokatora. Moraju pokriti zahteve PDF-a:

- uspeh i non-null pokazivac za razumne velicine;
- najmanje trazeni broj korisnih bajtova;
- poravnanje i nepreklapanje aktivnih alokacija;
- oslobadjanje i ponovnu upotrebu prostora;
- spajanje susednih slobodnih fragmenata;
- vise uzastopnih alokacija i oslobadjanja razlicitih velicina;
- `nullptr` kada zahtev ne moze da se ispuni.

Ne testirati nevalidan pokazivac za `mem_free`, jer PDF za taj slucaj definise ponasanje kao nedefinisano.

Dijagnosticki testovi ostaju odvojeni od zvanicnih testova i nikada ne ulaze u predajni ZIP.

Za svaki pad primeniti puni evidence-first bug ciklus. Kada memorijski testovi prodju, sacuvati `memory-working` checkpoint.

## Todo 7: Pokrenuti zvanicni test 1

Pokrenuti neizmenjenu logiku `Threads_C_API_test` i proveriti:

- uspesno kreiranje cetiri niti;
- prosledjivanje tela niti i argumenta;
- sinhronu predaju procesora kroz `thread_dispatch`;
- cuvanje registara kroz trap i context switch;
- izvrsavanje svih tela niti;
- korektan povratak iz zavrsenih niti.

Prvo probati originalni test. Samo ako je zbog dvostruke emulacije neprakticno spor, napraviti zasebnu skracenu dijagnosticku varijantu sa istom logikom. Original ostaje neizmenjen i mora se koristiti za zavrsnu proveru.

## Todo 8: Pokrenuti zvanicni test 2

Tek kada memorija i test 1 prolaze, pokrenuti `Threads_CPP_API_test` i proveriti:

- globalne `new/delete` omotace;
- konstrukciju izvedenih `Thread` objekata;
- `start`;
- virtuelni `run`;
- C++ `dispatch`;
- destrukciju bez ostecenja scheduler-a ili memorije.

Svaka popravka mora ponovo pokrenuti memorijske testove i test 1 kao regresiju.

## Todo 9: Dokazati zvanicni test 7

Test 7 se po zvanicnom uputstvu namerno ne zavrsava regularno. Prolaz se ne dokazuje samo timeout-om.

GDB mora pokazati da:

- korisnicka nit zaista radi u user mode-u;
- pokusaj privilegovane `csrr` instrukcije izaziva ocekivani illegal-instruction trap;
- izvršavanje ne nastavlja kao da je instrukcija dozvoljena.

## Todo 10: Zavrsni pregled zadataka 1 i 2

Kada svi ciljani testovi prodju, uraditi read-only pregled celog opsega prema PDF-u:

- ABI kodovi, redosled argumenata i povratne vrednosti;
- C API i C++ API potpisi i semantika;
- granice i matematika alokatora;
- scheduler invariants;
- formiranje i poravnanje steka;
- cuvanje registara i context switch;
- zivotni ciklus, zavrsetak i dealokacija niti;
- prelazak S-mode/U-mode;
- zavrsetak programa.

Eksplicitno poznata specifikacijska neuskladjenost za kasniju proveru: PDF trazi argumente `thread_create` redom u `a1`, `a2`, `a3`, `a4`, dok trenutni wrapper i handler koriste `a1`, `a2`, `a6`, `a7`. To se tretira kao potvrđen bug samo uz citat PDF-a, dobija zaseban RCA i approval gate cak i ako trenutni javni testovi slucajno prolaze.

Pregled samo predlaze dodatne ispravke. Nista se ne refaktorise automatski. Svaki prihvaceni nalaz prolazi isti bug ciklus.

## Zavrsni kriterijumi

Plan za zadatke 1 i 2 je zavrsen kada:

- projekat se cisto prevodi u zvanicnoj VM bez starih artefakata;
- meni se pouzdano pojavljuje;
- dijagnosticki testovi memorije prolaze kroz C API/ABI put;
- zvanicni testovi 1 i 2 prolaze;
- test 7 daje GDB-om potvrdjen ocekivani trap;
- originalni testovi i bazna infrastruktura nisu prikriveno menjani;
- svaki potvrdjeni bug ima jednostavan RCA, odobrenu minimalnu popravku, pregledan diff, nezavisni code review, regresione dokaze i zaseban commit;
- sestra moze da objasni svaku promenu i odgovori na pitanja za samoproveru.

Pakovanje konacne predaje i prosirenje na zadatak 3 nisu deo ovog plana i dogovaraju se zasebno.
