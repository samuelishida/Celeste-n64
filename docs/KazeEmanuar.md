
Sim. Pesquisei as fontes técnicas do N64, o trabalho do próprio Kaze, a documentação original da Nintendo e a evolução do HackerSM64. E tem um ponto importante: **algumas partes do plano que você escreveu precisam ser corrigidas**, principalmente “CPU e RCP em segmentos separados” e “desenrolamento de loops como otimização”. Na realidade, justamente o oposto do *loop unrolling* é uma das ideias importantes do trabalho do Kaze.

Abaixo está uma versão bem mais completa, pensada como **plano técnico de otimização para um projeto N64 real**, não apenas para SM64.

---

# Plano de Otimização Extrema para Nintendo 64

## 0. Modelo mental correto: o problema não é simplesmente “falta de CPU”

O N64 possui uma arquitetura bastante particular:

```text
                    ┌──────────────────┐
                    │     VR4300 CPU   │
                    │     93.75 MHz    │
                    │  L1: 16K I + 8K D│
                    └────────┬─────────┘
                             │
                             │
                    ┌────────▼─────────┐
                    │   RDRAM / RCP    │
                    │   memória UMA    │
                    │   ~4 MB          │
                    └────────┬─────────┘
                             │
              ┌──────────────┴──────────────┐
              │                             │
       ┌──────▼──────┐               ┌──────▼──────┐
       │     RSP     │               │     RDP     │
       │ 62.5 MHz    │               │ 62.5 MHz    │
       │ Vector Unit │               │ Rasterizer   │
       └─────────────┘               └─────────────┘
```

A RDRAM é **memória unificada**: CPU, RSP e RDP compartilham o mesmo espaço de memória. A documentação da Nintendo inclusive descreve explicitamente que os três processadores podem utilizar a mesma RDRAM. ([Ultra64][1])

Ela possui uma característica aparentemente paradoxal:

> **altíssima largura de banda sequencial, mas latência ruim para acessos aleatórios.**

A documentação cita até **500 MB/s para transferências consecutivas**, mas deixa claro que essa taxa não é alcançada em acessos aleatórios. ([Ultra64][1])

É justamente daí que vem boa parte da filosofia do Kaze:

**não basta diminuir instruções; é necessário diminuir tráfego de memória e cache misses.**

---

# 1. Memória: prioridade máxima

## 1.1. Otimizar para a latência da RDRAM

A RDRAM do N64 é extremamente rápida em transferências sequenciais, mas acessar novos locais de memória repetidamente é caro. Estudos da arquitetura estimam latências da ordem de centenas de nanossegundos para determinados acessos. ([Blink Copetti][2])

Portanto:

```text
ERRADO:

load A
load B
load C
load D
load E
...
```

quando `A B C D E` estão espalhados pela memória.

Preferir:

```text
DMA / burst
      ↓
[ A B C D E F G H ]
      ↓
cache / local memory
      ↓
processamento
```

### Regra para o projeto

> **Faça o acesso à RDRAM de forma sequencial e previsível sempre que possível.**

---

# 2. Cache da CPU

O VR4300 possui:

* **16 KB de instruction cache**
* **8 KB de data cache**

totalizando 24 KB de L1. ([The Copetti site][3])

Isso é absurdamente pequeno pelos padrões modernos.

Consequentemente:

```text
Código pequeno
      ↓
cabe no I-cache
      ↓
menos fetch da RDRAM
      ↓
CPU fica mais tempo executando
```

versus:

```text
Código gigante
      ↓
I-cache misses
      ↓
fetch da RDRAM
      ↓
stall
      ↓
CPU esperando
```

E existe um efeito ainda mais interessante:

### O código também compete com os dados pela infraestrutura de memória.

Portanto, **reduzir o tamanho do código pode ser mais importante que reduzir o número absoluto de instruções executadas.**

Essa é uma das grandes inversões de lógica do N64.

---

# 3. NÃO fazer loop unrolling indiscriminadamente

Aqui eu corrigiria diretamente seu plano original.

Em hardware moderno, frequentemente:

```c
for (i = 0; i < 4; i++)
    process(data[i]);
```

pode ser transformado em:

```c
process(data[0]);
process(data[1]);
process(data[2]);
process(data[3]);
```

para eliminar branches e overhead.

No N64 isso pode **piorar** o desempenho.

O código maior:

```text
loop pequeno
   ↓
poucas instruções
   ↓
bom I-cache
```

pode ser melhor que:

```text
loop expandido
   ↓
muitas instruções
   ↓
mais I-cache misses
   ↓
mais acessos à RDRAM
```

Kaze demonstrou justamente esse comportamento contraintuitivo; análises independentes do trabalho também destacam que loops desenrolados podem aumentar o código e provocar mais fetches de instrução. ([Set Side B][4])

### Regra

**Não otimizar loops pelo número de instruções sem medir o impacto no cache.**

---

# 4. Organização espacial do código

O objetivo não deve ser:

> “fazer cada função individualmente ser a mais rápida possível”.

O objetivo deve ser:

> **manter o hot path inteiro dentro do cache.**

Imagine:

```text
Frame
 ├── input
 ├── physics
 ├── collision
 ├── animation
 ├── object update
 └── camera
```

Descubra quais funções são chamadas praticamente todos os frames.

Depois:

```text
HOT CODE
──────────────
physics
collision
movement
animation
camera
──────────────

COLD CODE
──────────────
debug
menus
cutscenes
error handling
unused features
```

O hot code deve ficar compacto.

---

# 5. Data locality

O mesmo conceito vale para dados.

Em vez de:

```c
struct Object {
    position;
    huge_unused_data;
    texture;
    debug_data;
    collision;
    ...
};
```

se o frame só utiliza:

```text
position
rotation
velocity
collision
```

você quer que esses campos estejam próximos.

Idealmente:

```text
CACHE LINE
┌────────────────────────────┐
│ position                   │
│ rotation                   │
│ velocity                   │
│ collision                  │
└────────────────────────────┘
```

em vez de:

```text
CACHE LINE
┌────────────────────────────┐
│ position                   │
│ unused                     │
│ pointer                    │
│ debug                      │
│ ...                        │
└────────────────────────────┘

CACHE LINE
┌────────────────────────────┐
│ velocity                   │
└────────────────────────────┘
```

A documentação técnica do N64 também enfatiza requisitos de alinhamento e organização de memória.

---

# 6. Separar Read-Mostly, Write-Mostly e Read/Write

Uma otimização mais profunda é classificar os dados:

### Read-only

```text
ROM → RAM → CPU/RSP
```

Exemplos:

* tabelas
* constantes
* geometria
* animações
* lookup tables

### Write-only

```text
CPU → RDRAM → RCP
```

Exemplos:

* display lists
* command buffers
* buffers intermediários

### Read/Write

```text
CPU ↔ RDRAM
```

Exemplos:

* estado dos objetos
* física
* colisão

Isso permite desenhar uma política de memória baseada em **quem realmente precisa tocar em cada região**.

---

# 7. Aproveitar os bancos de RDRAM

Esse ponto é particularmente importante para um projeto N64.

A RDRAM é dividida em bancos de aproximadamente **1 MB**, com registradores de página ativos separados. A própria documentação recomenda posicionar framebuffer e Z-buffer em bancos diferentes para melhorar execução. ([Ultra64][1])

Então não pense apenas:

```text
address = 0x123456
```

Pense:

```text
BANK 0
├── CPU hot data
└── code

BANK 1
├── framebuffer
└── graphics

BANK 2
├── Z-buffer
└── geometry

BANK 3
├── audio
└── streaming
```

Isso reduz situações em que múltiplos consumidores ficam disputando o mesmo banco.

---

# 8. Framebuffer e Z-buffer

Uma regra prática extremamente importante:

```text
Framebuffer
      ≠
Z-buffer
```

no mesmo banco quando possível.

A documentação técnica do N64 explicitamente recomenda separar esses buffers em bancos diferentes para melhorar o desempenho.

Para um renderer próprio:

```text
RDRAM
┌───────────────┐
│ Bank 0        │ CPU / game
├───────────────┤
│ Bank 1        │ Framebuffer A
├───────────────┤
│ Bank 2        │ Framebuffer B
├───────────────┤
│ Bank 3        │ Z-buffer / graphics
└───────────────┘
```

---

# 9. DMA: mover dados em blocos

O RSP possui apenas:

```text
4 KB IMEM
4 KB DMEM
```

e dados fora dessas memórias precisam ser transferidos por DMA. ([GitHub][5])

Isso muda completamente a forma de programar o RSP.

Não faça:

```text
RDRAM
 ↓
pequeno dado
 ↓
RSP
 ↓
RDRAM
 ↓
pequeno dado
 ↓
RSP
```

Prefira:

```text
RDRAM
    │
    │ DMA
    ▼
┌─────────────┐
│    DMEM     │
│ 4 KB local  │
└──────┬──────┘
       │
       ▼
   PROCESSAR
       │
       ▼
     DMA OUT
```

Ou seja:

> **DMA grande + processamento local > muitos acessos pequenos à RDRAM.**

---

# 10. RSP como acelerador de propósito específico

O RSP possui uma unidade vetorial com:

* 32 registradores vetoriais
* 8 lanes de 16 bits
* ALUs vetoriais
* memória local IMEM/DMEM

([GitHub][5])

Isso permite mover determinadas operações da CPU para o RSP.

Excelente para:

```text
vertex transformation
lighting
matrix operations
animation
particle systems
audio DSP
geometry processing
```

O princípio:

```text
CPU
 │
 │ prepara batch
 ▼
DMA
 │
 ▼
RSP
 │
 ├── SIMD/vector processing
 │
 ▼
DMA
 │
 ▼
RDRAM / RDP
```

---

# 11. Não desperdiçar o RSP com operações que a CPU faz melhor

Também é importante não cair na armadilha:

> “RSP = GPU, então tudo deve ir para o RSP.”

Não.

O RSP tem apenas **4 KB de IMEM + 4 KB de DMEM**. ([GitHub][5])

Se uma tarefa exige:

```text
DMA
→ trocar microcode
→ DMA
→ processar 20 operações
→ DMA
```

pode ser pior que simplesmente deixar a CPU executar.

### Regra:

Use RSP quando o trabalho:

* é altamente repetitivo;
* é vetorizável;
* trabalha em batches;
* pode ficar bastante tempo no DMEM;
* compensa o custo do DMA.

---

# 12. Batch rendering

Aqui seu plano original está correto e pode ser levado muito mais longe.

Em vez de:

```text
coin
 ↓
setup
 ↓
render
 ↓

coin
 ↓
setup
 ↓
render
```

fazer:

```text
coins
 ├── coin 1
 ├── coin 2
 ├── coin 3
 ├── coin 4
 └── coin 5
       ↓
   ONE BATCH
       ↓
     RSP/RDP
```

Isso reduz:

* mudanças de estado;
* comandos redundantes;
* carregamentos de textura;
* configuração de pipeline;
* overhead de CPU.

E isso não é apenas teoria: o próprio ecossistema HackerSM64 atualmente possui trabalho de **coin batching** e um *batching engine* com *shadow batching* em desenvolvimento. ([GitHub][6])

---

# 13. Shadow batching

Sombras são um excelente candidato.

Em vez de:

```text
Mario
 → shadow setup
 → shadow geometry
 → render

Bob-omb
 → shadow setup
 → shadow geometry
 → render

Coin
 → ...
```

agrupar:

```text
ALL SHADOWS
      ↓
GENERATE
      ↓
ONE BATCH
      ↓
RDP
```

O HackerSM64 inclusive possui uma iniciativa específica para **batching engine + shadow batching**. ([GitHub][6])

---

# 14. Reduzir mudanças de estado do RDP

Um renderer N64 eficiente deve organizar objetos por:

```text
texture
material
combine mode
render mode
geometry mode
```

e não necessariamente pela ordem lógica dos objetos.

Por exemplo:

```text
Scene objects:

tree
coin
rock
coin
tree
coin
rock
```

Transformar em:

```text
TEXTURE A
 ├── tree
 ├── tree

TEXTURE B
 ├── coin
 ├── coin
 ├── coin

TEXTURE C
 ├── rock
 └── rock
```

Menos:

```text
texture load
state change
texture load
state change
```

---

# 15. TMEM: tratar os 4 KB como recurso precioso

O RDP possui apenas **4 KB de TMEM** para texturas. Isso significa que textura não deve ser tratada como “VRAM infinita”.

A estratégia deve ser:

```text
ROM/RDRAM
     ↓
DMA
     ↓
TMEM 4 KB
     ↓
render batch
     ↓
nova textura
```

Em vez de ficar alternando texturas constantemente.

### Consequência para seu engine

Organize o renderer por:

```text
Texture residency
       ↓
Material
       ↓
Batch
       ↓
Draw
```

e não simplesmente:

```text
Object
 ↓
Draw
 ↓
Object
 ↓
Draw
```

---

# 16. Reduzir lookup tables gigantes

Essa é uma otimização que merece um capítulo próprio.

Uma tabela de lookup pode parecer extremamente rápida:

```c
result = table[x];
```

mas existe uma pegadinha:

```text
table
 ↓
cache miss
 ↓
RDRAM
 ↓
latência
```

Às vezes:

```c
calculate(x)
```

é **mais rápido** que:

```c
table[x]
```

se a tabela for grande e destruir a localidade do cache.

Kaze demonstrou isso em otimizações matemáticas do SM64. Em particular, funções trigonométricas do jogo já usavam uma tabela de 4096 ângulos, e posteriormente ele investigou formas ainda mais eficientes de calcular essas funções. ([Set Side B][7])

### Regra

Para qualquer LUT:

```text
LUT size
+
access frequency
+
locality
+
miss cost
```

devem ser medidos juntos.

---

# 17. Matemática: aproximar em vez de calcular exatamente

O N64 não precisa necessariamente de:

```text
sin()
cos()
sqrt()
atan2()
```

com precisão moderna.

Podemos usar:

```text
lookup
+
polynomial approximation
+
fixed point
+
range reduction
```

Por exemplo:

```text
angle
 ↓
reduce range
 ↓
small polynomial
 ↓
approximation
```

Isso é especialmente útil em:

* animação;
* física;
* câmera;
* partículas;
* iluminação.

---

# 18. Fixed-point onde fizer sentido

Nem toda matemática precisa ser `float`.

Em operações altamente frequentes:

```text
position
velocity
angle
collision
animation
```

pode ser interessante:

```text
16.16 fixed point
```

ou formatos equivalentes.

Exemplo conceitual:

```text
0x00018000
   │
   └── 1.5
```

Isso pode reduzir:

* custo de operações;
* tamanho dos dados;
* pressão no cache;
* movimentação de dados.

Mas **não deve ser aplicado cegamente**: o VR4300 possui FPU, então a escolha precisa ser baseada em profiling.

---

# 19. Physics: eliminar trabalho redundante

Uma engine N64 deve evitar:

```text
object A
 ↓
collision

object B
 ↓
collision

object C
 ↓
collision
```

se muitos objetos estão longe uns dos outros.

Use:

```text
broad phase
     ↓
potential pairs
     ↓
narrow phase
```

Por exemplo:

```text
distance > threshold
       ↓
    SKIP

distance < threshold
       ↓
collision test
```

O HackerSM64 inclusive incorporou uma técnica atribuída ao Kaze para **calcular automaticamente uma distância ótima de colisão baseada nos vértices do objeto**. ([GitHub][8])

Isso é exatamente o tipo de otimização que você quer replicar.

---

# 20. Graph / scene traversal

Outra área importante é o scene graph.

Não faça:

```text
root
 ↓
ALL children
 ↓
ALL objects
 ↓
ALL geometry
```

independentemente da câmera.

Use:

```text
camera
  ↓
frustum
  ↓
spatial hierarchy
  ↓
visible nodes
  ↓
render
```

E ainda:

```text
distance
 ↓
LOD
```

O HackerSM64 lista **graph node optimizations** entre as contribuições e otimizações relacionadas ao trabalho de Kaze/Wiseguy. ([GitHub][8])

---

# 21. LOD dinâmico

O N64 não precisa renderizar o mesmo objeto da mesma forma em qualquer distância.

```text
0–20m
HIGH
 ├── full geometry
 └── full animation

20–50m
MEDIUM
 ├── simplified geometry
 └── reduced animation

50m+
LOW
 └── billboard / simplified
```

Mas existe uma regra mais importante:

> **LOD deve economizar RDP + RDRAM, não apenas polígonos.**

Se um LOD reduz polígonos mas aumenta texture swaps ou command traffic, pode não ajudar.

---

# 22. Billboard caching

Sua ideia de calcular billboard uma vez por frame pode ser expandida.

Em vez de:

```text
object 1 → calculate billboard
object 2 → calculate billboard
object 3 → calculate billboard
...
```

faça:

```text
camera matrix
       ↓
cached billboard basis
       ↓
all billboard objects
```

Assim você reutiliza:

```text
camera right
camera up
camera forward
```

em todos os objetos.

---

# 23. Animation batching

O mesmo vale para animação.

Se 100 partículas usam a mesma animação:

```text
animation state
       ↓
calculate once
       ↓
100 instances
```

em vez de:

```text
calculate
calculate
calculate
...
```

Isso é particularmente poderoso para:

* partículas;
* moedas;
* inimigos do mesmo tipo;
* vegetação;
* efeitos.

---

# 24. Eliminar safety checks do hot path

Aqui seu plano também está essencialmente correto, mas existe uma nuance.

Em código genérico:

```c
void foo(int x) {
    if (x < 0)
        return;

    ...
}
```

é bom.

Mas em uma engine extremamente controlada:

```text
caller
 ↓
garante x >= 0
 ↓
foo()
```

então fazer:

```text
caller check
+
foo check
```

significa trabalho duplicado.

A abordagem de Kaze foi justamente eliminar verificações que eram redundantes dentro das premissas controladas do engine. ([SonicHits][9])

### Estratégia melhor

Não simplesmente:

> “remova todos os checks”.

Faça:

```text
DEBUG BUILD
 ├── assertions
 ├── bounds checks
 └── validation

RELEASE BUILD
 └── remove checks comprovadamente redundantes
```

---

# 25. Dead code elimination

SM64 carregava muito código que não era necessário para a execução final.

A análise de Kaze e de fontes posteriores destaca código/debug e estruturas desnecessárias como parte do desperdício original. ([Hackaday][10])

Faça:

```text
feature
 ↓
used?
 ├── YES → keep
 └── NO  → delete
```

Especialmente:

* debug;
* ferramentas;
* código de desenvolvimento;
* caminhos impossíveis;
* features abandonadas;
* error handling excessivo.

---

# 26. Remover abstrações caras

Em um engine moderno:

```c
generic_object_update()
    ↓
function pointer
    ↓
virtual dispatch
    ↓
generic handler
```

pode ser perfeitamente aceitável.

No N64:

```text
indirect call
 ↓
I-cache locality ruim
 ↓
branch prediction limitado
 ↓
mais código espalhado
```

pode custar caro.

Prefira, quando o profiling justificar:

```text
object type
 ↓
direct function
```

ou até:

```text
batch by behavior
```

---

# 27. Estrutura de dados: SoA vs AoS

Para muitos objetos:

### AoS

```c
struct Object {
    position;
    velocity;
    rotation;
    scale;
    ...
};

Object objects[100];
```

pode carregar dados que não são necessários.

### SoA

```text
positions[]
velocities[]
rotations[]
scales[]
```

permite processar somente:

```text
positions
+
velocities
```

em uma determinada fase.

Isso também é excelente para processamento vetorial no RSP.

---

# 28. RSP + SoA

Aqui dá para criar um pipeline extremamente eficiente:

```text
RDRAM

positions[]
velocities[]
rotations[]
        │
        │ DMA
        ▼
┌──────────────────┐
│       DMEM       │
│ positions        │
│ velocities       │
│ rotations        │
└────────┬─────────┘
         │
         ▼
    RSP Vector Unit
         │
         ▼
     transformed
         │
         │ DMA
         ▼
      RDRAM
```

Isso é muito mais próximo da maneira que o hardware foi projetado para trabalhar.

---

# 29. Compiler strategy

Seu item de compilação também merece expansão.

Não existe:

> “o melhor flag de GCC”.

Existe:

> **o melhor assembly para aquele hot path.**

A estratégia deve ser:

```text
C source
   ↓
compile
   ↓
inspect assembly
   ↓
benchmark real N64
   ↓
change
   ↓
compile
   ↓
benchmark
```

---

# 30. Otimização por tamanho vs velocidade

Essa é uma das peculiaridades mais importantes.

Normalmente:

```text
-O3
```

parece intuitivamente melhor.

Mas em N64:

```text
-O2 / size-oriented
       ↓
smaller binary
       ↓
better I-cache
       ↓
less RDRAM traffic
       ↓
POSSIBLY FASTER
```

enquanto:

```text
-O3
 ↓
aggressive inlining
loop transformations
code expansion
 ↓
larger binary
 ↓
I-cache misses
 ↓
POSSIBLY SLOWER
```

O trabalho de Kaze é justamente um exemplo de como otimizar para **localidade e tamanho do código**, não simplesmente para throughput de instruções. ([Hackaday][11])

---

# 31. Compilar funções individualmente

Uma estratégia ainda melhor:

```text
NORMAL CODE
    ↓
size-oriented

HOT FUNCTIONS
    ↓
speed-oriented

RSP CODE
    ↓
hand optimized assembly
```

Ou:

```text
physics.c
    ↓
-O2

renderer.c
    ↓
-O2 / custom

critical_math.c
    ↓
custom assembly

rsp_geometry
    ↓
RSP assembly
```

---

# 32. Assembly manual

Quando o compilador não produzir o assembly ideal:

```text
C
 ↓
compiler
 ↓
assembly
 ↓
identify bottleneck
 ↓
rewrite critical function
```

Principalmente para:

* memcpy especializado;
* matrix multiplication;
* vector math;
* collision;
* decompression;
* DSP;
* renderer.

Mas assembly deve ser **última etapa**, não primeira.

---

# 33. Branch delay slots

O VR4300 é um MIPS com pipeline e branch delay slots.

Portanto, em assembly:

```asm
beq   t0, t1, target
nop
```

pode ser melhor transformado conceitualmente em:

```asm
beq   t0, t1, target
addiu t2, t2, 1
```

quando a instrução do delay slot puder ser aproveitada.

Ou seja:

> **não desperdice slots do pipeline.**

---

# 34. Evitar branches imprevisíveis

Em uma rotina extremamente quente:

```c
if (condition)
    A();
else
    B();
```

pode ser reorganizada dependendo do comportamento esperado.

Mas isso precisa ser medido.

O objetivo é:

```text
hot path
─────────────
straight-line code
─────────────
rare path
```

e não espalhar caminhos raros no meio do caminho principal.

---

# 35. Streaming de assets

Outro ponto que eu adicionaria ao seu plano:

**não carregue tudo sem necessidade.**

Estruture:

```text
ROM
 │
 ▼
asset manager
 │
 ├── level data
 ├── textures
 ├── geometry
 ├── animation
 └── audio
```

e carregue somente o necessário.

Isso é especialmente relevante porque análises recentes do SM64 identificaram **asset loading excessivamente guloso** como uma fonte de desperdício de memória. ([Hackaday][10])

---

# 36. Compressão + DMA

Como o N64 possui:

```text
ROM → RDRAM
```

e a CPU pode trabalhar enquanto determinados componentes estão ocupados, pode ser vantajoso armazenar assets comprimidos:

```text
ROM
 ↓
compressed data
 ↓
DMA
 ↓
CPU/RSP decompression
 ↓
RAM
```

em vez de ocupar cartucho/RDRAM com dados totalmente expandidos.

O trade-off é:

```text
CPU cycles
vs
ROM bandwidth
vs
RDRAM bandwidth
vs
RAM capacity
```

---

# 37. Double buffering

Para manter CPU e RCP trabalhando simultaneamente:

```text
Buffer A
CPU → prepara

Buffer B
RCP → processa
```

depois:

```text
swap
```

Pipeline:

```text
FRAME N
CPU:  prepare A
RCP:  render B

FRAME N+1
CPU:  prepare B
RCP:  render A
```

Isso evita que CPU e RCP fiquem esperando um pelo outro.

---

# 38. Producer/consumer architecture

O engine ideal deveria parecer:

```text
CPU
 │
 ├── game logic
 ├── physics
 ├── visibility
 └── command generation
          │
          ▼
    command buffer
          │
          ▼
         RSP
          │
          ▼
    display lists
          │
          ▼
         RDP
```

Cada estágio produz trabalho para o próximo.

---

# 39. Não sincronizar cedo demais

Um erro comum seria:

```text
CPU
 ↓
wait RSP
 ↓
continue
 ↓
wait RDP
```

O objetivo:

```text
CPU ────────────────┐
                    │
RSP ────────┐       │
            │       │
RDP ────────┴───────┘
```

é manter todos ocupados.

### Regra

> **Uma sincronização é uma oportunidade de perguntar: “eu realmente precisava esperar aqui?”**

---

# 40. Profiling: a otimização precisa ser guiada por hardware

Essa talvez seja a maior coisa que eu adicionaria ao seu plano.

Não faça:

```text
acho que isso está lento
 ↓
otimizar
```

Faça:

```text
measure
 ↓
identify bottleneck
 ↓
change ONE thing
 ↓
measure again
```

Métricas:

```text
CPU time
RSP time
RDP time
RDRAM stalls
DMA time
frame time
cache misses
display-list size
texture DMA
audio time
```

---

# 41. Profiling por frame

Seu profiler ideal:

```text
FRAME 12345

CPU
██████████████░░░░ 70%

RSP
██████████░░░░░░░░ 50%

RDP
████████████████░░ 80%

DMA
██████░░░░░░░░░░░░ 30%

RDRAM contention
████████████░░░░░░ 60%
```

Isso permite saber imediatamente:

```text
CPU bottleneck?
RSP bottleneck?
RDP bottleneck?
MEMORY bottleneck?
```

---

# 42. A métrica mais importante: frame budget

Para:

### 30 FPS

```text
33.33 ms/frame
```

### 60 FPS

```text
16.67 ms/frame
```

O objetivo não é necessariamente:

```text
CPU = 100%
```

É:

```text
CPU + RSP + RDP + DMA
        ↓
terminar dentro do budget
```

E, idealmente:

```text
CPU ──────────────┐
RSP ──────────────┤
RDP ──────────────┤ → frame budget
DMA ──────────────┘
```

---

# 43. A grande descoberta: otimizar uma parte pode piorar outra

Esse é provavelmente o conceito mais importante para o seu projeto.

Imagine:

```text
otimização A
 ↓
CPU -20%
```

parece ótimo.

Mas:

```text
código +40%
 ↓
I-cache misses +50%
 ↓
RDRAM traffic +30%
 ↓
RDP stalls +15%
```

Resultado:

```text
FRAME TIME = pior
```

Por isso o N64 precisa ser tratado como **sistema inteiro**, e não como CPU + GPU independentes.

A análise do trabalho de Kaze destaca justamente que o tráfego de memória da CPU pode interferir no trabalho do RCP, e que reduzir cache misses pode gerar ganhos que parecem desproporcionais ao número de instruções economizadas. ([Hacker News][12])

---

# 44. Otimizações gráficas

Depois que o CPU/memory path estiver otimizado:

### Prioridade:

```text
1. state changes
2. texture loads
3. DMA
4. overdraw
5. geometry
6. triangle count
```

Não simplesmente:

```text
triangle count
```

porque o N64 pode ser limitado por memória e pipeline antes de ser limitado pelo número bruto de triângulos.

---

# 45. Overdraw

Uma cena:

```text
background
██████████████████

wall
██████████████████

object
██████████████████

particle
██████████████████
```

pode fazer o RDP processar muitos pixels repetidamente.

Então:

```text
front-to-back
+
occlusion
+
depth strategy
```

podem economizar muito trabalho.

---

# 46. Z-buffer estratégico

O Z-buffer não é “gratuito”.

Ele implica tráfego adicional de memória e operações do RDP.

Para determinados elementos:

```text
UI
particles
background
sky
```

pode ser possível utilizar estratégias diferentes de depth testing.

**Não desligue Z-buffer globalmente**, mas classifique os objetos:

```text
opaque
transparent
overlay
background
particle
```

e use o mínimo necessário para cada categoria.

---

# 47. Microcode customizado

Para um projeto realmente extremo:

```text
Fast3D
   ↓
custom microcode
```

O RSP é programável e possui microcode próprio, então o renderer pode ser adaptado ao workload específico. O HackerSM64 atualmente inclusive mantém uma iniciativa de **microcode overhaul**. ([GitHub][13])

Isso abre possibilidades como:

```text
custom vertex transform
custom lighting
custom clipping
custom batching
custom particle processing
```

---

# 48. Audio também compete pela mesma memória

Não esquecer:

```text
CPU
RSP graphics
RSP audio
RDP
```

todos dependem do sistema de memória.

A própria documentação do RCP mostra que o RSP participa de DMA entre RDRAM e suas memórias internas, enquanto AI, VI e outros dispositivos também utilizam DMA. ([Ultra64][14])

Então:

```text
audio DMA
+
graphics DMA
+
CPU memory traffic
```

precisam ser planejados juntos.

---

# 49. Audio streaming

Use:

```text
ROM
 ↓
DMA
 ↓
audio buffer
 ↓
RSP
```

com buffers suficientemente grandes para reduzir overhead.

Evitar:

```text
tiny DMA
tiny DMA
tiny DMA
tiny DMA
```

---

# 50. Arquitetura final recomendada

Para o seu projeto, eu montaria a arquitetura assim:

```text
                         ┌─────────────────────────┐
                         │          ROM             │
                         │ assets / code / audio    │
                         └────────────┬────────────┘
                                      │
                                     DMA
                                      │
                    ┌─────────────────▼────────────────┐
                    │             RDRAM                │
                    │                                  │
                    │ Bank 0 → CPU hot code/data       │
                    │ Bank 1 → framebuffer              │
                    │ Bank 2 → Z-buffer / graphics     │
                    │ Bank 3 → streaming/audio         │
                    └───────────────┬──────────────────┘
                                    │
                  ┌─────────────────┼─────────────────┐
                  │                 │                 │
                  ▼                 ▼                 ▼
              VR4300              RSP               RDP
              CPU                 Vector            Raster
              93.75MHz            62.5MHz           62.5MHz
                  │                 │                 │
                  │                 │                 │
            game logic          batches          pixels
            physics             geometry         textures
            collision           lighting         blending
            camera              animation        Z
                  │                 │                 │
                  └────────────┬────┴─────────────────┘
                               │
                         frame budget
                               │
                               ▼
                         16.67 ms @ 60 FPS
```

---

# 51. Ordem de implementação que eu usaria

Para um **engine N64 próprio**, eu não tentaria fazer tudo simultaneamente.

### Fase 1 — Instrumentação

```text
[ ] CPU profiler
[ ] RSP profiler
[ ] RDP timing
[ ] DMA timing
[ ] frame timing
[ ] memory map visualization
```

### Fase 2 — Memória

```text
[ ] organizar bancos RDRAM
[ ] separar framebuffer/Z-buffer
[ ] alinhar dados
[ ] reduzir random access
[ ] hot/cold data
[ ] hot/cold code
[ ] DMA batching
```

### Fase 3 — CPU

```text
[ ] reduzir código
[ ] eliminar dead code
[ ] eliminar safety checks redundantes
[ ] reduzir function calls
[ ] reduzir code size
[ ] evitar inlining excessivo
[ ] evitar loop unrolling indiscriminado
[ ] otimizar assembly crítico
```

### Fase 4 — Game logic

```text
[ ] broad-phase collision
[ ] spatial partition
[ ] cached transforms
[ ] cached matrices
[ ] LOD
[ ] visibility
[ ] animation batching
```

### Fase 5 — RSP

```text
[ ] batch vertices
[ ] vectorize transforms
[ ] vectorize animation
[ ] vectorize lighting
[ ] DMA em blocos
[ ] custom microcode
```

### Fase 6 — Renderer

```text
[ ] material sorting
[ ] texture batching
[ ] state batching
[ ] shadow batching
[ ] coin/instance batching
[ ] minimize TMEM reload
[ ] minimize overdraw
[ ] optimize Z usage
```

### Fase 7 — Matemática

```text
[ ] fixed point onde vantajoso
[ ] LUT profiling
[ ] polynomial approximations
[ ] fast sqrt
[ ] fast sin/cos
[ ] fast normalization
```

### Fase 8 — Compiler/assembly

```text
[ ] gerar assembly
[ ] comparar -O2 / -O3 / size
[ ] medir I-cache
[ ] custom attributes por função
[ ] assembly hot paths
[ ] delay-slot optimization
```

### Fase 9 — Memory/CPU/RCP co-design

```text
[ ] CPU/RSP contention
[ ] DMA scheduling
[ ] bank placement
[ ] double buffering
[ ] pipeline CPU/RSP/RDP
[ ] eliminar waits
```

---

# 52. A filosofia final

Eu resumiria todo o trabalho do Kaze em uma regra muito mais útil para o seu agente de programação:

```text
                  ┌──────────────────┐
                  │  "Is it faster?" │
                  └────────┬─────────┘
                           │
                           ▼
                  ┌──────────────────┐
                  │ Profile on N64   │
                  └────────┬─────────┘
                           │
              ┌────────────┼────────────┐
              ▼            ▼            ▼
             CPU          RSP          RDP
              │            │            │
              └────────────┼────────────┘
                           ▼
                     RDRAM traffic
                           │
                           ▼
                    Cache locality
                           │
                           ▼
                     DMA efficiency
                           │
                           ▼
                    Frame time
```

A grande mudança de mentalidade é:

> **No N64, “menos instruções” não significa necessariamente “mais rápido”.**

Às vezes o melhor código é aquele que executa **mais instruções**, mas mantém tudo no cache.

Às vezes uma tabela de lookup é mais lenta que calcular o resultado.

Às vezes `-O3` é mais lento que uma otimização orientada a tamanho.

Às vezes reduzir triângulos não melhora o frame rate.

E às vezes uma otimização da **CPU** acelera o **RDP**, porque a CPU deixou de disputar a RDRAM.

Esse é o motivo pelo qual o trabalho do Kaze é tão interessante: ele não está simplesmente fazendo *micro-optimizations* de C. Ele está tratando **CPU + cache + RDRAM + DMA + RSP + RDP como um único pipeline de execução**. A própria documentação da Nintendo deixa claro que a RDRAM é compartilhada pelos três processadores, e o trabalho de Kaze mostra como explorar essa arquitetura de maneira muito mais agressiva. ([Ultra64][1])

E há uma evidência particularmente boa de que essas técnicas são generalizáveis: o **HackerSM64**, que incorporou diversas otimizações e técnicas atribuídas ao trabalho de Kaze, continua evoluindo em 2026, incluindo *batching*, otimizações de colisão, organização de código e uma futura revisão de microcode. ([GitHub][8])

**Para um projeto novo no N64, eu colocaria essa ordem de prioridade:**

**RDRAM/cache locality → DMA → CPU/RSP/RDP overlap → batching → code size → renderer → matemática → assembly.**

Essa ordem é muito mais próxima do que realmente faz um engine N64 “voar” do que simplesmente tentar reduzir polygon count ou jogar `-O3` em todo o projeto.

[1]: https://ultra64.ca/files/documentation/online-manuals/man/kantan/step1/2-4.html?utm_source=chatgpt.com
[2]: https://blink.copetti.org/writings/consoles/nintendo-64/cpu/?utm_source=chatgpt.com
[3]: https://www.copetti.org/writings/consoles/nintendo-64/?utm_source=chatgpt.com
[4]: https://setsideb.com/mario-64s-optimization-paradox/?utm_source=chatgpt.com
[5]: https://github.com/mikeryan/n64dev/blob/master/docs/n64ops/rcp.txt?utm_source=chatgpt.com
[6]: https://github.com/HackerN64/HackerSM64/pulls?utm_source=chatgpt.com
[7]: https://setsideb.com/2023/?w=26&utm_source=chatgpt.com
[8]: https://github.com/HackerN64/HackerSM64?utm_source=chatgpt.com
[9]: https://sonichits.com/video/Kaze_Emanuar/FIXING_the_ENTIRE_SM64_Source_Code_%28INSANE_N64_performance%29?utm_source=chatgpt.com
[10]: https://hackaday.com/tag/code-optimization/?utm_source=chatgpt.com
[11]: https://hackaday.com/2022/04/21/optimized-super-mario-64-offers-exciting-possibilities/?utm_source=chatgpt.com
[12]: https://news.ycombinator.com/item?id=36496691&utm_source=chatgpt.com
[13]: https://github.com/HackerN64/HackerSM64/milestone/6?utm_source=chatgpt.com
[14]: https://ultra64.ca/files/documentation/online-manuals/man/pro-man/pro08/08-01.html?utm_source=chatgpt.com
