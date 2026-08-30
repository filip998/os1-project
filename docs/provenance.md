# Poreklo radne kopije

Radna kopija je formirana iz sledecih arhiva:

| Arhiva | SHA-256 | Uloga |
| --- | --- | --- |
| `project-base-v1.1.zip` | `763920bf5def13d054f7dfcccd118e405828b14c7f2673036eaabeadc580584d` | Zvanicna ETF baza |
| `os (2).zip` | `a779e55351d13be8c142a2af01132631ae77559996642b770443d0f7d2431213` | Sestrin zamrznuti snapshot |
| `javniTestovi_2024_1_1.zip` | `2ca061c823ec4df810510837e4718849d3076a44ed33c2044a0fef04a59a12ca` | Zvanicni javni testovi |
| `OS_student.zip` | `65d14d9524104a03cdaeeec11010e7e99a3de57616487b5850039c0633fa3af3` | Zvanicna Ubuntu VM |

Generisani `build/`, `kernel`, `kernel.asm` i `.gdbinit` iz sestrinog ZIP-a nisu uvezeni. Baseline mora da se prevede od nule.

Testovi 1, 2 i 7 zadrzavaju zvanicnu logiku. Prilagodjene su samo include putanje potrebne zbog rasporeda direktorijuma. `userMain` je konfigurisan za nivoe 1 i 2 bez lokalnih debug ispisa.

Naknadnim prosirenjem opsega na Test 3 utvrdjeno je da su njegov C test i
`Buffer` u studentskom snapshot-u direktno pozivali internu klasu
`semaphore`. Pozivi su vraceni na zvanicne `sem_open`, `sem_wait`,
`sem_signal` i `sem_close` C API funkcije iz arhive
`javniTestovi_2024_1_1.zip`; zadrzane su samo potrebne include putanje.
