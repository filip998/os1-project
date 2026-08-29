# RCA 001: Trap rutina se ne ukljucuje u cist build

## Simptom

Potpuno cist build se zavrsava linker greskom:

```text
src/main.cpp:13: undefined reference to `Riscv::supervisorTrap()'
make: *** [Makefile:92: kernel] Error 1
```

To znaci da je `main.cpp` uspeo da se prevede, ali pri pravljenju konacnog `kernel` fajla nije pronadjen masinski kod funkcije `Riscv::supervisorTrap`.

## Minimalna reprodukcija

U zvanicnoj VM, iz korena projekta:

```sh
make clean
make
```

`make clean` je vazan zato sto uklanja stari `kernel` i prethodno prevedene `.o` fajlove.

## Dokaz

Makefile automatski pronalazi asemblerske izvore ovom komandom:

```make
SOURCES_ASM = $(shell find . -name "*.S" -printf "%P ")
```

Sestrin trap fajl se zove:

```text
src/supervisorTrap.s
```

Ekstenzija ima malo slovo `.s`, pa ga pretraga za `*.S` ne pronalazi. U build logu se zato vide `clearSStatus.S`, `copy_and_swap.S`, `contextSwitch.S` i `entry.S`, ali se `supervisorTrap.s` nigde ne prevodi niti se `build/src/supervisorTrap.o` prosledjuje linkeru.

Biblioteka `hw.lib` takodje ne definise `Riscv::supervisorTrap`, pa linker nema drugo mesto na kom bi mogao da pronadje tu funkciju.

## Root cause prostim jezikom

Na Linux-u su veliko i malo slovo razliciti:

```text
supervisorTrap.S  !=  supervisorTrap.s
```

Makefile uzima samo fajlove sa velikim `.S`. Kod trap rutine postoji, ali build sistem ne zna da treba da ga ukljuci.

```text
src/supervisorTrap.s
          |
          | ne odgovara obrascu *.S
          v
nije preveden supervisorTrap.o
          |
          v
linker ne nalazi Riscv::supervisorTrap
          |
          v
kernel nije napravljen
```

## Zasto je ranije moglo da deluje da radi?

Sestrin ZIP je sadrzao stare fajlove:

```text
build/src/supervisorTrap.o
kernel
```

Ako se pokrene postojeci `kernel` bez cistog builda, stari masinski kod moze da sakrije cinjenicu da trenutni izvorni fajl vise nije deo builda. Ne mozemo dokazati kada je promenjeno veliko `.S` u malo `.s`, ali mozemo dokazati da trenutni izvorni kod bez starih artefakata nije reproduktivan.

Ovaj konkretan bug jos ne govori da li je implementacija `ecall` putanje ispravna. On samo sprecava da napravimo nov kernel koji tu putanju mozemo pouzdano da debagujemo.

## Razmatrane popravke

### Menjanje Makefile-a da prihvata i `.s`

Ovo bi moglo da radi, ali bi menjalo zvanicnu baznu infrastrukturu bez potrebe.

### Preimenovanje studentskog fajla u `.S`

Ovo prati PDF, koji za studentski asembler koristi `.S`, i postojeci Makefile. Ne menja nijednu instrukciju.

## Predlozena minimalna popravka

```text
src/supervisorTrap.s -> src/supervisorTrap.S
```

Nema izmene sadrzaja fajla.

## Planirana verifikacija

Posle odobrenja popravke:

1. preimenovati fajl;
2. ponovo pokrenuti `make clean && make`;
3. proveriti da log sadrzi pravljenje `build/src/supervisorTrap.o`;
4. proveriti da linker napravi novi `kernel`;
5. pokrenuti nezavisni review tacnog diff-a.

Uspeh cistog builda nece automatski znaciti da je runtime problem resen. Sledeci korak ce i dalje biti zasebna reprodukcija pada pre menija.

## Pitanja za samoproveru

1. Zasto postojanje starog `kernel` fajla nije dokaz da trenutni izvorni kod moze da se prevede?
2. Zasto `supervisorTrap.s` i `supervisorTrap.S` nisu isto ime na Linux-u?
3. Zasto je bolje preimenovati studentski fajl nego menjati zvanicni Makefile?
