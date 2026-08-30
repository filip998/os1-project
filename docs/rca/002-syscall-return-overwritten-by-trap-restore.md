# RCA 002: Trap rutina pregazi povratnu vrednost sistemskog poziva

## Simptom

Posle uspesnog cistog builda, komanda:

```sh
make qemu
```

pokrene kernel, ali se meni za izbor testa nikada ne pojavi. Program ne ispisuje
gresku, vec ostane zaglavljen pre `userMain`.

## Minimalna reprodukcija

U zvanicnoj VM:

```sh
make clean
make
make qemu
```

Za precizan dokaz pokrenut je isti kernel kroz `make qemu-gdb`. Praceni su:

- ulazak u trap;
- sacuvani registri na steku;
- rezultat `mem_alloc` handlera;
- restauracija registra `a0`;
- pokazivac steka nove niti;
- prva instrukcija koja pristupa tom steku.

Sirovi logovi su sacuvani u gitignored `debug-artifacts/`.

## Konkretan GDB dokaz

Prilikom alokacije steka za korisnicku nit:

```text
mem_alloc handler result: a0=0x8000d320
live_a0=0x8000d320 saved_a0=0x1
restored_a0=0x1
```

Alokator je, dakle, vratio validnu heap adresu `0x8000d320`. Medjutim, trap
frame i dalje u slotu za `a0` sadrzi stari broj sistemskog poziva `0x1`.
Trap rutina zatim restaurira taj stari broj preko validnog rezultata.

C wrapper zbog toga dobija pokazivac `0x1` i prosledjuje ga pozivu
`thread_create`:

```text
TCB::create_thread: stack=0x1
new TCB: context.sp=0x1001 stack=0x1
```

`TCB::create_thread_stack` postavlja pocetni `sp` na:

```text
stack + DEFAULT_STACK_SIZE = 0x1 + 0x1000 = 0x1001
```

Pri ulasku u `TCB::thread_wrapper` funkcijski prolog prvo smanji `sp` na
`0xfe1`, a zatim pokusa da sacuva `ra` na adresu `sp + 24 = 0xff9`.
Procesor potvrdi upravo taj pad:

```text
mcause = 7             # store/AMO access fault
mepc   = 0x80001908    # sd ra,24(sp)
mtval  = 0xff9         # nevalidna adresa upisa
```

Posto je `mtvec=0`, machine-mode fault odvodi izvrsavanje na adresu nula. Zato
kernel spolja izgleda kao da je samo zauvek stao.

## Root cause prostim jezikom

Registar `a0` ima dve uloge:

1. pre `ecall` sadrzi broj sistemskog poziva;
2. posle sistemskog poziva treba da sadrzi rezultat.

Trap rutina na ulazu sacuva sve registre, ukljucujuci stari `a0`. Handler zatim
izracuna dobar rezultat i upise ga u zivi `a0`. Na izlazu trap rutina ne zna da
se namena `a0` promenila. Ona ucita staru sacuvanu vrednost i pregazi rezultat.

To je kao da se pre odlaska na salter sacuva stari papir sa brojem zahteva.
Sluzbenik na isti sto stavi ispravan odgovor, ali se pri izlasku preko odgovora
vrati kopija starog papira. Pozivalac zato dobije broj zahteva umesto odgovora.

```text
C wrapper: a0 = 0x01 (MEM_ALLOC)
                  |
                  v
trap sacuva a0=0x01 u frame[10]
                  |
                  v
handler: a0 = 0x8000d320 (validan rezultat)
                  |
                  v
trap restaurira frame[10] -> a0 = 0x01
                  |
                  v
C wrapper dobije pokazivac 0x1
                  |
                  v
nova nit dobije sp = 0x1001
                  |
                  v
prvi upis ide na 0xff9 i procesor prijavi fault
```

## Zasto je direktni poziv ranije radio?

Kod direktnog poziva:

```text
C/C++ kod -> Mem::mem_alloc -> povratna vrednost
```

rezultat funkcije ostaje u `a0` i odmah se vraca pozivaocu. Nema trap rutine
koja posle toga restaurira staru kopiju registra.

Kod propisanog sistemskog puta postoji dodatni korak:

```text
C API -> ecall -> trap -> handler -> trap restore -> C API
```

Alokator je i na tom putu izracunao dobar rezultat. Greska nije u konkretnoj
heap adresi, vec u tome sto trap restore odbaci rezultat pre povratka u C API.

## Razmatrane popravke

### Ne restaurirati `a0`

Ovo bi moglo da propusti rezultat sistemskog poziva, ali nije bezbedno opste
pravilo. Ista trap rutina obradjuje i prekide i izuzetke. Kod njih prekinuti kod
ocekuje da njegov `a0` ostane sacuvan. C++ handler takodje sme da koristi i
promeni caller-saved registre.

### Posle handlera uvek kopirati zivi `a0` u sacuvani slot

Ovo resava samo vidljivi simptom i oslanja se na to da poslednja C++ funkcija
slucajno ostavi rezultat u zivom `a0`. Ne resava drugi deo istog ABI problema:
handleri trenutno citaju argumente iz zivih caller-saved registara umesto iz
sacuvane slike stanja.

### Cuvati rezultat u globalnoj promenljivoj

Ovo uvodi deljeno stanje i postaje nebezbedno kada syscall promeni nit ili kada
se trap-ovi preklapaju. Rezultat pripada konkretnom trap frame-u, a ne celom
kernelu.

### Koristiti sacuvani trap frame kao izvor argumenata i rezultata

Ovo je izabrano resenje. Trap frame vec predstavlja tacno stanje prekinutog
koda. Handler treba da dobije pokazivac na njega, procita originalne ABI
argumente iz odgovarajucih slotova i upise rezultat u sacuvani slot za `a0`.

## Predlozena minimalna popravka

1. `supervisorTrap.S` prosledjuje trenutni `sp`, odnosno adresu trap frame-a,
   funkciji `Riscv::handle_supervisor_trap`.
2. `Riscv::handle_supervisor_trap` prosledjuje isti frame samo syscall putanji.
3. `Handlers::handle_sys_call` cita syscall kod iz `frame[10]`.
4. Syscall argumenti se citaju iz sacuvanih ABI slotova (`a1` je `frame[11]`,
   `a2` je `frame[12]` i tako dalje).
5. Handleri koji vracaju rezultat upisuju ga u `frame[10]`.
6. Postojeca trap restauracija ostaje ista. Kada ucita sacuvani `a0`, ona sada
   ucitava rezultat, a ne stari syscall kod.

Najvazniji tok posle popravke je:

```text
frame[10] = 0x01
       |
       | handler pozove Mem::mem_alloc
       v
frame[10] = 0x8000d320
       |
       | nepromenjena restauracija registara
       v
a0 = 0x8000d320
       |
       v
C wrapper dobije validan pokazivac
```

Ovo resenje istovremeno cuva registre pri prekidima i pravilno vraca syscall
rezultate. Ne menja alokator, scheduler, velicinu steka niti logiku testova.

## Verifikacija popravke

Nezavisni code review nije pronasao problem u diff-u.

Ponovljeni `make clean && make` je uspeo za 69 sekundi. Napravljeni su novi
`build/src/supervisorTrap.o` i `kernel`, pa je prosla i RCA 001 regresija.

Disassembly potvrdjuje da trap prosledjuje frame i da handler rezultat upisuje
u sacuvani `a0` slot:

```text
mv a0,sp
jal Riscv::handle_supervisor_trap(unsigned long*)
...
sd a0,80(s1)
```

Post-fix GDB reprodukcija daje:

```text
mem_alloc result: live_a0=0x8000d320 frame_a0=0x8000d320
before restore: frame_a0=0x8000d320
after restore: a0=0x8000d320
thread_create receives: body=0x80001870 stack=0x8000d320
new TCB: context.sp=0x8000e320 stack=0x8000d320
thread_wrapper entry: sp=0x8000e320
```

Time je dokazano da trap vise ne vraca `0x1`, da C wrapper dobija validnu heap
adresu i da nova nit vise ne pada pri prvom upisu na stek.

Kanonski meni jos nije dostignut zbog sledeceg, odvojenog bootstrap problema.
Izvrsavanje sada prolazi kroz `wrapperUserMain`, `userMain`, `printString` i
`putc`, ali prvi `ecall` iz user mode-a odlazi u machine mode:

```text
mcause  = 8
mepc    = 0x8000145c  # ecall u putc
mtvec   = 0
medeleg = 0x200       # delegiran je bit 9, ali ne i bit 8
```

Ovaj novi dokaz ne ponistava RCA 002: nevalidan stack je uklonjen i izvrsavanje
sada stize do prvog korisnickog ispisa. Meni ostaje blokiran zasebnim problemom
delegiranja user-mode `ecall` izuzetka, koji ne sme biti popravljen bez novog
RCA-a i odobrenja.

## Pitanja za samoproveru

1. Zasto je `0x8000d320` dokaz da alokator nije uzrok ovog konkretnog pada?
2. Zasto nije bezbedno samo preskociti restauraciju registra `a0`?
3. Zasto rezultat treba upisati u trap frame pre restauracije registara?
