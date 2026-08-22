1. Primeiro: o gargalo real do N64

O ponto mais importante é entender a arquitetura:

```text
                 Nintendo 64
                      │
              ┌───────▼───────┐
              │     RDRAM      │
              │     4 MB       │
              └───────┬───────┘
                      │
          ┌───────────┴───────────┐
          │                        │
     ┌────▼─────┐            ┌─────▼─────┐
     │   CPU    │            │    RCP    │
     │ MIPS R4300│           │           │
     │ 93.75 MHz│            │ RSP + RDP │
     └──────────┘            └───────────┘
```

O problema não é simplesmente:

> "o N64 tem pouca CPU."

É principalmente:

> **quantos dados e instruções precisam atravessar o sistema de memória para produzir cada frame.**

A CPU pode passar muito tempo:

* calculando transformações;
* preparando vértices;
* criando display lists;
* fazendo colisão;
* atualizando objetos;
* copiando dados;
* esperando memória;
* esperando DMA;
* preparando coisas que poderiam ser processadas pela RCP.

Kaze basicamente tenta transformar:

```text
CPU faz tudo
   ↓
CPU espera
   ↓
CPU prepara GPU
   ↓
CPU espera
```

em algo mais próximo de:

```text
CPU ────────────────┐
                    │
                    ▼
                  RSP
                    │
                    ▼
                  RDP
                    │
CPU continua ───────┘
```

Ou seja: **paralelismo.**

---

# 2. Separação da memória CPU/RCP

Essa é uma das otimizações mais interessantes.

O N64 possui um sistema de memória compartilhado. Portanto, simplesmente aumentar a quantidade de processamento pode não resolver se CPU e RCP estiverem constantemente acessando os mesmos dados.

Imagine:

```text
CPU
 │
 ├── lê objeto
 ├── escreve objeto
 ├── lê vértice
 ├── escreve vértice
 │
 ▼
RDRAM
 ▲
 │
 ├── RSP lê vértices
 ├── RDP lê display list
 └── RDP lê texturas
```

Existe competição pelo acesso à memória.

Kaze reorganiza os dados para que diferentes partes do sistema tenham **menos interferência entre si**.

A ideia é algo como:

```text
CPU-oriented data

[Game State]
[Physics]
[AI]
[Object State]
[Collision]

        ↓

CPU trabalha principalmente aqui


RCP-oriented data

[Vertices]
[Display Lists]
[Render State]
[Textures]

        ↓

RSP/RDP trabalham principalmente aqui
```

Isso não significa que existam duas RAMs fisicamente separadas.

É uma **separação lógica e de acesso**.

---

# 3. Cache vs Uncached Memory

O R4300i possui cache, e isso é extremamente importante.

Se a CPU escreve algo que depois nunca precisa ler novamente, colocar esse dado no cache pode ser desperdício.

Exemplo:

```c
generate_render_data();
```

Suponha que a CPU produza:

```text
Vertex buffer
      ↓
RSP
      ↓
RDP
```

A CPU não precisa necessariamente voltar a ler esses vértices.

Então:

```text
CPU
 ↓
write
 ↓
RDRAM
 ↓
RSP
```

pode ser melhor do que:

```text
CPU
 ↓
cache
 ↓
writeback
 ↓
RDRAM
 ↓
RSP
```

A ideia é liberar o cache para dados realmente reutilizados:

```text
CACHE

Player
Camera
Physics
Object state
Collision

UNCACHED

Temporary render buffers
One-way generated data
DMA buffers
```

Isso é uma otimização bastante específica de arquitetura.

---

# 4. Loop Unrolling

Considere:

```c
for (int i = 0; i < 4; i++)
    vertices[i].x += offset;
```

Normalmente existe:

```text
load i
compare
branch
increment
load vertex
store vertex
```

A versão desenrolada pode ser:

```c
vertices[0].x += offset;
vertices[1].x += offset;
vertices[2].x += offset;
vertices[3].x += offset;
```

Agora o compilador/CPU não precisa executar a estrutura do loop repetidamente.

No N64 isso pode ser especialmente útil porque:

```text
instruction fetch
       ↓
RDRAM
       ↓
cache
       ↓
CPU
```

é relativamente caro.

Então você troca:

```text
menos código
+
mais branches
```

por:

```text
mais código
+
menos branches
+
menos overhead
```

O interessante é que isso representa uma escolha clássica de engine N64:

> **gastar ROM/RAM para economizar ciclos de CPU.**

---

# 5. Remover abstrações que custam CPU

Um engine moderno pode fazer:

```c
Object *object = get_object(id);

if (object->type == TYPE_COIN)
    ...

if (object->collision != NULL)
    ...

if (object->flags & ...)
    ...
```

No N64, milhares dessas operações por frame podem ser significativas.

Kaze reduz estruturas complexas quando elas não trazem benefício real.

Por exemplo, em vez de:

```text
CollisionType
    ↓
enum
    ↓
lookup table
    ↓
function
    ↓
conditional
```

pode virar simplesmente:

```c
if (object->flags & COLLISION_SOLID)
```

Isso é muito mais barato.

---

# 6. Flags são extremamente importantes

Uma estrutura:

```c
struct Object {
    int type;
    int collision_type;
    int behavior;
    int state;
};
```

pode acabar gerando várias comparações.

Uma alternativa:

```c
#define SOLID      0x01
#define DAMAGE     0x02
#define CLIMBABLE  0x04
#define COLLECT    0x08

uint8_t flags;
```

Então:

```c
if (obj->flags & SOLID)
```

é uma operação extremamente simples.

Além disso, você diminui o tamanho da estrutura.

Isso importa porque estruturas menores significam:

```text
menos RDRAM
↓
melhor cache locality
↓
menos bandwidth
↓
mais objetos processáveis
```

---

# 7. Remover debug é mais importante do que parece

Em engines modernas:

```c
assert(object != NULL);
debug_check_collision();
debug_validate_state();
debug_log();
```

não representam necessariamente um problema.

No N64, porém, cada operação pode significar:

```text
load
compare
branch
function call
memory access
```

E isso acontece potencialmente milhares de vezes.

Portanto, a versão final pode remover:

```text
assertions
logging
debug rendering
validation
redundant bounds checks
```

especialmente em caminhos extremamente quentes.

O princípio é:

> **hot path não deve fazer trabalho que não afeta o resultado final.**

---

# 8. Batch Rendering

Aqui começa uma das partes mais importantes.

Imagine 100 moedas.

Uma implementação ingênua poderia fazer:

```text
Coin 1
 ├─ setup material
 ├─ setup texture
 ├─ setup geometry
 └─ draw

Coin 2
 ├─ setup material
 ├─ setup texture
 ├─ setup geometry
 └─ draw

Coin 3
 ...
```

Isso gera enorme quantidade de overhead.

Batching transforma isso em:

```text
setup texture
setup material

Coin 1
Coin 2
Coin 3
Coin 4
...
Coin 100

draw
```

Ou:

```text
                ┌── Coin 1
                ├── Coin 2
Common state ───┼── Coin 3
                ├── ...
                └── Coin 100
```

O ganho vem principalmente da redução de:

* display list commands;
* state changes;
* DMA;
* transformação repetida;
* carregamento repetido de assets.

---

# 9. Billboards

Esse é um excelente exemplo de trabalho redundante.

Imagine 100 sprites:

```text
     Camera
        ↓

  [A] [B] [C] [D]
```

Todos precisam ficar orientados para a câmera.

Uma abordagem ruim:

```text
for each billboard:
    calculate camera orientation
    calculate matrix
    transform
```

Ou seja:

```text
Camera Matrix × 100
```

Mas a câmera é a mesma.

Então você pode fazer:

```text
Camera
  ↓
Billboard orientation
  ↓
ONE matrix
  ↓
100 instances
```

Isso muda:

```text
O(N) expensive matrix calculations
```

para algo próximo de:

```text
O(1) camera calculation
+
O(N) cheap positioning
```

Esse tipo de transformação é extremamente valioso no N64.

---

# 10. Sombras: CPU → RSP

Essa é provavelmente uma das otimizações mais interessantes.

Uma abordagem tradicional seria a CPU gerar a geometria da sombra:

```text
CPU

shadow
 ↓
calculate vertices
 ↓
calculate triangles
 ↓
write geometry
 ↓
RSP
 ↓
RDP
```

Isso significa que a CPU está fazendo geometria.

A alternativa é fornecer uma transformação mais abstrata:

```text
CPU
 │
 └── shadow matrix
          ↓
         RSP
          ↓
   vector processing
          ↓
        RDP
```

O RSP possui uma arquitetura vetorial feita justamente para operações desse tipo.

Portanto, você quer aproveitar:

```text
CPU = game logic
RSP = vector/math/render preparation
RDP = rasterization
```

em vez de:

```text
CPU = game logic + geometry + rendering preparation
```

---

# 11. O RSP é basicamente um acelerador vetorial

Isso é fundamental para entender a filosofia.

O RSP tem:

```text
32 × 128-bit vector registers
```

e consegue trabalhar com múltiplos valores simultaneamente.

Conceitualmente:

```text
Scalar CPU:

a0 × matrix
a1 × matrix
a2 × matrix
a3 × matrix

↓

Vector RSP:

[v0 v1 v2 v3]
       ×
    MATRIX

        ↓

[v'0 v'1 v'2 v'3]
```

Então operações matemáticas que seriam muitas instruções MIPS podem ser muito mais eficientes no RSP.

---

# 12. O pipeline ideal

A grande meta é manter todos os componentes ocupados:

```text
Frame N

CPU
████████████████

RSP
      ███████████████

RDP
              ███████████████
```

Em vez de:

```text
CPU
████████████
            idle
                 ███████

RSP
            ███
                 idle

RDP
                 ███
```

O primeiro cenário aproveita paralelismo.

---

# 13. Compiler Optimization por arquivo

Essa parte é particularmente interessante.

Você não necessariamente quer:

```text
-O3 everywhere
```

no N64.

Por quê?

Porque otimização de tamanho e velocidade podem gerar resultados diferentes.

Por exemplo:

```text
-O2
```

pode produzir:

```text
smaller code
better cache behavior
```

enquanto:

```text
-O3
```

pode produzir:

```text
more aggressive optimization
loop transformations
inlining
larger binary
```

Mas código maior pode piorar:

```text
instruction cache
memory bandwidth
```

Então você pode acabar com:

```text
physics.c       → optimize speed
render.c        → optimize speed
collision.c     → optimize size
menu.c          → optimize size
audio.c         → optimize speed
```

Ou seja:

> **otimização deixa de ser uma configuração global e vira uma decisão por hot path.**

---

# 14. Inlining

Considere:

```c
int clamp(int x)
{
    if (x < 0) return 0;
    if (x > 255) return 255;
    return x;
}
```

Chamando:

```c
x = clamp(x);
```

você pode ter:

```text
call
argument setup
jump
function body
return
```

Com inline:

```text
function body
```

é inserido diretamente no caller.

Isso elimina:

```text
function call
stack interaction
jump
return
```

Mas existe um trade-off:

```text
Inlining
   ↓
menos overhead
   ↓
maior código
   ↓
potential cache pressure
```

Por isso novamente:

> **não é simplesmente "inline tudo".**

---

# 15. A grande filosofia por trás das otimizações

Se condensarmos todo o trabalho do Kaze em uma única ideia:

### Não é fazer o N64 trabalhar mais rápido.

É fazer o N64 **trabalhar menos**.

Por exemplo:

| Problema           | Abordagem ingênua       | Abordagem otimizada |
| ------------------ | ------------------------ | ------------------- |
| 100 moedas         | 100 setups               | 1 batch             |
| 100 billboards     | 100 matrizes             | 1 matriz            |
| Sombras            | CPU gera geometria       | RSP processa        |
| Collision          | estruturas complexas     | flags               |
| Debug              | executado no runtime     | removido            |
| Dados temporários | cache                    | uncached            |
| Loops              | branch por iteração    | unrolling           |
| Funções pequenas | call/return              | inline              |
| Código            | mesmo optimization level | por hot path        |
| CPU/RCP            | competindo por memória  | acessos organizados |

---

# 16. Isso explica como projetos como SM64 conseguem ficar absurdamente mais complexos

O mais interessante é que essas técnicas são **complementares**.

Imagine um frame originalmente custando:

```text
CPU

Physics          20%
Collision        15%
Object update    20%
Matrix math      15%
Rendering prep   25%
Misc              5%

= 100%
```

Depois de otimizações:

```text
Physics           15%
Collision          7%
Object update     12%
Matrix math        3%
Rendering prep     8%
Misc               5%

CPU = 50%
```

E o restante pode ser deslocado para:

```text
RSP
██████████████

RDP
████████████████
```

Agora existe espaço para:

```text
mais objetos
+
mais polígonos
+
mais efeitos
+
maior draw distance
+
mais lógica
```

sem necessariamente aumentar o clock.

---

# 17. A arquitetura resultante

Uma engine N64 extremamente otimizada poderia ser pensada assim:

```text
                  GAME ENGINE
                       │
          ┌────────────┴────────────┐
          │                         │
        CPU                         │
          │                         │
 ┌────────┼─────────┐               │
 │        │         │               │
Game    Physics  Collision          │
Logic                           Render Queue
 │                                  │
 │                                  ▼
 └──────────────────────────────► RSP
                                   │
                         ┌─────────┴─────────┐
                         │                   │
                    Transformations      Lighting
                         │                   │
                         └─────────┬─────────┘
                                   │
                                   ▼
                                  RDP
                                   │
                                   ▼
                              Framebuffer
```

A regra fundamental passa a ser:

> **CPU prepara o mínimo necessário; RSP transforma/processa; RDP rasteriza.**

---

## 18. E existe uma conclusão ainda mais importante

O trabalho de Kaze não é simplesmente uma coleção de "truques de C".

É praticamente uma **reengenharia do modelo de execução do jogo em função da arquitetura do N64**.

Um programador moderno tende a pensar:

```text
CPU
 ↓
GPU
```

Mas para N64 você deve pensar:

```text
                  RDRAM
                    │
          ┌─────────┴─────────┐
          │                   │
         CPU                 RCP
          │             ┌─────┴─────┐
          │             │           │
          │            RSP         RDP
          │             │           │
          └─────────────┴───────────┘
```

E o objetivo é **minimizar a quantidade de trabalho que precisa passar entre essas unidades**.

É justamente essa mentalidade que permite coisas aparentemente absurdas para o hardware original — como **mundos maiores, draw distance maior, mais objetos e sistemas muito mais complexos — em uma máquina com apenas 4 MB de RDRAM**.

Se você estiver usando isso como base para o seu **engine N64/open-world**, as técnicas mais importantes para implementar primeiro seriam: **streaming/segmentação de memória → render queue/batching → RSP para transformações → frustum/visibility culling → object pools → dados compactados → DMA/cache management → só depois micro-otimizações de C/compiler**.
