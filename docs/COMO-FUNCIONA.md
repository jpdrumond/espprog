# Como a leitura do PROG foi conseguida

Registro técnico do que foi estabelecido **empiricamente** na bancada, com a placa
TRIFLEX CONNECT v2.36.33 real. Vários pontos aqui **corrigem ou completam** o
levantamento original (`PROTOCOLO.md`), que foi feito a partir de uma única captura
com o programador de fábrica já conectado e conversando.

Onde este documento e o levantamento divergem, **vale este** — cada afirmação abaixo
foi medida no hardware.

---

## 1. O barramento é 9 V, não 3,3 ou 5

O levantamento marcava a tensão do `VCC_PROG` como *[a confirmar]*, supondo 3,3 ou 5 V.
Medido: **8,8 V**.

Qualquer pull-up do lado alto precisa ser referenciado ao `VCC_PROG`, **nunca** ao 3V3 do
ESP: em lógica de 9 V o limiar de entrada fica entre ~4,5 e 6,3 V, e 3,3 V é lido como
nível baixo.

```
3V3 ──┬── R3 ──── IO5        P4 (9 V) ──── R6 ──── D3
      ├── R5 ──── IO17
      ├── gate Q2                        (SEM resistor no D2 -- ver abaixo)
      └── gate Q3
```

Os MOSFETs ficam com *source* no lado de 3,3 V e *drain* no lado de 9 V — invertidos, o
diodo de corpo conduz os 9 V direto para o pino do ESP.

### O R4 NÃO deve ser montado

**O pull-up no `D2` impede o funcionamento.** Descoberto por acaso: o circuito funcionou a
noite toda com o `R4` **esquecido**, sem solda. Assim que ele foi soldado, a placa parou
de transmitir na mesma hora; dessoldado, voltou a funcionar. Testado nos dois sentidos.

| `R4` | Resultado |
|---|---|
| ausente | `polls` a 23 ms, `bad: 0` — sessão perfeita |
| 10k para `VCC_PROG` | **zero quadros**, a placa fica muda |

Note que o sintoma não é falha de leitura: é a **placa parando de transmitir**. A linha
fica em repouso alto e silenciosa.

Hipótese que melhor explica — não verificada por medição direta: sem o `R4`, quem segura
o `D2` é o nosso próprio MOSFET, através do diodo de corpo a partir do `R3` de 3,3 V,
deixando a linha num nível **intermediário** (~3,3 a 3,9 V). O `R4` a levaria aos 9 V
plenos. Se a placa usa essa tensão para detectar a presença de um programador — o
original é alimentado pelo mesmo barramento e também carrega a linha —, o nível
intermediário é a assinatura de "tem alguém aqui", e 9 V limpos significam "não tem
ninguém".

Independente do mecanismo, **o fato está estabelecido: não monte o R4.**

---

## 2. A placa não inicia a conversa sozinha

**Esta é a descoberta central, e contradiz o levantamento.**

O levantamento afirma que a placa envia poll a cada 23,68 ms *"indefinidamente"*. Isso só
é verdade **depois** que uma sessão foi estabelecida. Com o conector ocioso, o
comportamento real é outro:

```
a cada 1000 ms, indefinidamente:
  7E 26 01 53 |"    TRIFLEX     CONNECT v2.36.33"| B2 51
```

Ela repete **o splash de segundo em segundo**, como um convite, e **nunca começa a
pollar** até alguém responder de forma aceitável. Por isso o splash — que normalmente
some em um segundo — fica preso na tela quando nada responde.

Confirmado capturando 16 quadros consecutivos, todos idênticos, com intervalo de
1000 ± 1 ms.

### O gatilho do engate

Responder ao splash com o eco correto **não basta** para engatar. O que engata é a
sessão perceber um programador **chegando**: as respostas pararem por cerca de um
segundo e voltarem.

Foi descoberto por acidente — o ESP reiniciou durante um teste e a sessão engatou na
hora, saltando de 0 para 393 polls. Reproduzido depois de forma controlada.

O firmware implementa isso em `progEngageService()`, escalonando para não derrubar a
rede à toa:

| Tentativa | Ação | Derruba web/SNMP? |
|---|---|---|
| 1ª | pausa as respostas por 1,5 s | não |
| 2ª | reinicializa a UART do barramento | não |
| 3ª+ | reinicia o módulo | sim, ~3 s |

Dispara quando chegam quadros mas nenhum poll por 8 s, com 12 s entre tentativas. Na
prática a primeira costuma resolver, e o reengate é invisível.

### Ciclo de vida da sessão

```
    cancela ligada, ninguem conectado
              │
              │  splash a cada 1000 ms, indefinidamente
              ▼
    ┌──────────────────────┐
    │  AGUARDANDO          │◄──────── volta aqui se a sessao cair
    │  (so manda splash)   │
    └──────────┬───────────┘
               │  respostas param ~1 s e voltam
               ▼
    ┌──────────────────────┐
    │  SESSAO ATIVA        │   poll a cada 23 ms
    │  status, teclas, ... │   telas so quando mudam
    └──────────────────────┘
```

O firmware trata os três casos de reinício sem intervenção:

| Cenário | O que acontece |
|---|---|
| **ESP reinicia**, cancela ligada | engata no próprio boot — a subida do firmware já é o "programador chegando" |
| **Cancela reinicia**, ESP ligado | o ESP detecta que só chega splash e reengata pelo escalonamento acima |
| **Queda geral** | os dois sobem juntos; o ESP engata no boot |

### A carga no VCC_PROG não é necessária

Durante a investigação foi testada a hipótese de que a placa detectaria a presença do
programador pelo **consumo** no `VCC_PROG` — o original é alimentado por ele e puxa
dezenas de miliampères, enquanto o adaptador pendura só dois resistores de 10k (~1,8 mA).

Foram ligados 4 × 4,7 kΩ em paralelo (~7,5 mA) entre `VCC_PROG` e `GND` e **não houve
diferença**. Depois ficou provado que a placa manda o splash **mesmo sem nenhum
programador conectado**, o que descarta de vez a detecção por consumo.

**Não coloque resistor de carga no `VCC_PROG`.** Além de inútil, foi observado que ele
capta ruído na linha.

---

## 3. `+` e `−` simultâneos: `0x48`

O levantamento afirma que essa combinação *"não existe"*, porque as duas teclas estão no
mesmo canal (`TECLA_A`). A conclusão veio de uma captura onde ninguém apertou as duas
juntas.

Na prática o original usa uma **escada resistiva** lida por ADC: fechar os dois caminhos
em paralelo derruba mais a leitura do que qualquer tecla sozinha, produzindo um terceiro
valor.

**Capturado do programador real** espionando a linha `D3`:

```
7E 08 01 3F 48 B4 4B 81
            ^^
```

| Tecla | `TECLA_A` | Decimal |
|---|---|---|
| nenhuma | `B4` | 180 |
| `+` | `A8` | 168 |
| `−` | `54` | 84 |
| **`+` e `−`** | **`48`** | **72** |

O valor mais baixo de todos, exatamente como a topologia previa.

No firmware: `KEY_A_PLUSMINUS` em `config.h`, resolvido por `keysToBytes()`, que olha o
**par** de teclas em vez de uma por vez — necessário justamente porque as duas dividem o
mesmo canal.

---

## 4. Modo espião: como capturar o que falta

O protocolo tem dois sentidos no mesmo par de fios, distinguidos pelo byte final:

| `TAIL` | Quem fala |
|---|---|
| `0x51` | a placa |
| `0x81` | o programador |

O firmware aceita **os dois**. Quadros de programador entram no log de
`/api/frames` mas não contam como tráfego da placa e nunca são respondidos.

Para espionar uma sessão do programador original:

1. mova o fio do RX de `D2` (P3) para **`D3`** (P2)
2. garanta **modo escuta** (`txEnabled=0`) — ver secção 5
3. conecte o programador original e opere normalmente
4. leia `/api/frames`

Foi assim que o valor `0x48` foi descoberto. A mesma técnica resolve qualquer
comportamento futuro que o levantamento não cubra: grave o que o original faz e compare.

---

## 5. Modo escuta precisa tri-statear o TX

Parar de enviar quadros **não basta** para ser passivo. Como saída de UART, o `IO17`
repousa em 3,3 V; quando outro dispositivo puxa o `D3` para baixo, o diodo de corpo do
Q3 liga o MOSFET e o nosso driver passa a brigar com quem está falando.

Sintoma observado: com o programador original conectado e o nosso adaptador "só
escutando", **ele travava** — as respostas dele chegavam corrompidas à placa.

Correção: em modo escuta o `IO17` vai para **alta impedância** (`progApplyTxMode()`).
Aí sim o adaptador é um observador puro.

---

## 6. Segurar tecla exige keep-alive

O firmware solta qualquer tecla após **3 s** sem notícias do cliente
(`KEY_WATCHDOG_MS`). Isso é proposital: se o navegador travar ou a rede cair com uma
tecla pressionada, ela não pode ficar presa numa cancela em operação.

A contrapartida é do cliente: **quem segura precisa reenviar `action=down` a cada
segundo**. A interface web faz isso automaticamente. Quem usar a API para segurar uma
tecla por muito tempo precisa fazer o mesmo, ou emitir vários `tap`.

---

## 7. Temporização confirmada

Medido com a sessão ativa, e batendo com o levantamento:

| Parâmetro | Medido |
|---|---|
| Período do poll | **23 ms** |
| Duração de 1 bit | **103–104 µs** (9600 baud) |
| Turnaround usado | 3000 µs (dentro da janela 2800–3300) |

Mais de **3300 polls consecutivos sem um único erro** de forma ou checksum. Se o
turnaround estivesse fora da janela, a placa teria desistido da sessão.

---

## 8. Ferramentas de diagnóstico que ficaram no firmware

Construídas durante a investigação, todas úteis para bancada:

| Endpoint | Para quê |
|---|---|
| `/api/rxdiag?pull=3` | estado elétrico da linha; `pull=3` liga pull-down interno e revela se há pull-up **externo** |
| `/api/edges` | tempos entre bordas, em µs — mede o baud real e separa dado de ruído |
| `/api/frames` | últimos 16 quadros em hexadecimal, com intervalo entre eles |
| `/api/txraw?hex=…` | transmite bytes crus na linha |
| `/api/txlow?ms=…` | segura o TX em nível baixo, para medir o `D3` com multímetro |
| `/api/keyraw?a=XX&b=YY` | envia bytes de tecla arbitrários |

### Uma armadilha de medição

A sonda de bordas roda em prioridade baixa e é **preemptada pela task do barramento**,
que roda em prioridade 19. Isso produz intervalos falsos muito curtos — durante a
investigação apareceram como "pulsos de 2 µs" e me levaram a diagnosticar borda lenta
onde não havia, custando uma troca de resistor desnecessária.

**Com tráfego real e limpo a sonda mede 103 µs corretamente.** Para avaliar qualidade de
sinal, prefira os contadores `bad` e `badchk` de `/api/state` — eles não têm esse viés.

---

## 9. O que continua em aberto

1. **O que `+` e `−` juntos fazem** na placa. O byte está correto e ela confirma com
   bipe, mas o efeito não foi identificado.
2. **Tabela de acentos do LCD** — só `0xEA` = `ê` está confirmado. Use o botão HEX da
   aba Tela para completar (ver README §9).
3. **Unidade de tempo do beep** — hipótese de 10 ms, configurável.
4. **Significado de `L 64 0A`** (comando `0x4C`).
5. **Mapa completo de menus**, necessário para a varredura automática ser segura.
