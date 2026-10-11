# Simulador de Memoria con Paginación

Simulador de una MMU (Memory Management Unit) con paginación: 400 KB de
RAM física (100 marcos de 4 KB) y una "V-RAM" infinita en disco para las
páginas desalojadas. Corre dos simulaciones en paralelo sobre la misma
secuencia de instrucciones — el algoritmo de reemplazo elegido por el
usuario vs. el algoritmo Óptimo (OPT/Belady) — para poder comparar.

## Características

- **5 algoritmos de reemplazo de páginas, cada uno con lógica propia:**
  FIFO, Second Chance (SC), LRU, LFU y OPT (óptimo, con *lookahead* real
  sobre las instrucciones futuras).
- **Mapa de punteros (`ptr`) global y nunca reutilizado**, incluso tras un
  `delete()`, tal como exige el enunciado.
- **Reloj y thrashing simulados:** +1 s por *hit*, +5 s por *fallo* de
  página (las páginas nuevas siempre cuentan como fallo), con alerta en
  rojo cuando el thrashing supera el 50 % del tiempo total.
- **Fragmentación interna**, procesos activos, páginas cargadas/
  descargadas y uso de RAM/V-RAM reportados tras cada instrucción.
- **Parser de archivos de operaciones** (formato Anexo 1: `new(pid,size)`,
  `use(ptr)`, `delete(ptr)`, `kill(pid)`), tolerante a espacios y saltos
  de línea `CRLF`/`LF`, que ignora líneas en blanco y comentarios (`#`).
- **Generador de operaciones aleatorio y reproducible** (misma semilla →
  mismo archivo), respetando las reglas del enunciado y guardando el
  archivo generado en disco (`operaciones_generadas_<semilla>.txt`)
  *antes* de arrancar la simulación.
- **Modo paso a paso o automático**, con pausa/reanudación por `ENTER`
  cuando se corre desde una terminal interactiva.

## Compilación (Debian 13 / cualquier Linux con gcc)

No hay dependencias externas, solo la librería estándar de C y POSIX
(`select`, `strcasecmp`, `isatty`). Se probó explícitamente con `gcc`
en modo estricto `-std=c11 -pedantic -Wall -Wextra` y en modo por
defecto, sin advertencias:

```bash
cd backend
gcc main.c mmu.c simulacion.c parser.c -o simulador
```

## Ejecución

### Modo interactivo

```bash
./simulador
```

Pide por consola: algoritmo (FIFO/SC/LRU/LFU/OPT), si se carga un archivo
de operaciones existente o se genera uno aleatorio (semilla, cantidad de
procesos `P`, cantidad total de operaciones `N`), y el modo de avance
(paso a paso o automático).

### Modo por línea de comandos (recomendado para pruebas automatizadas)

```bash
# Con un archivo de operaciones ya existente
./simulador fifo --archivo ops.txt
./simulador lru  --archivo ops.txt --auto

# Generando una secuencia aleatoria nueva (semilla, P, N)
./simulador opt --generar 42 10 200
```

`<algoritmo>` es uno de: `fifo | sc | lru | lfu | opt` (sin distinguir
mayúsculas). `--auto` avanza solo, en vez de pedir `ENTER` por paso.

Si la entrada estándar no es una terminal interactiva (por ejemplo al
redirigir desde un archivo, un *pipe*, o al correr en un script de
calificación con `< archivo` o `< /dev/null`), el simulador detecta esto
automáticamente y corre de corrido sin pedir `ENTER`, para que una
corrida no interactiva nunca se quede esperando entrada.

### Ejemplo reproducible (verificado)

```bash
cat > ops_ejemplo.txt << 'EOF'
new(1,250)
new(1,50)
new(2,5320)
new(3,345)
use(1)
use(3)
delete(1)
kill(2)
new(1,100)
EOF

./simulador fifo --archivo ops_ejemplo.txt --auto
```

Esto asigna `ptr` 1, 2, 3, 4 y 5 en orden estrictamente secuencial (el
`ptr` de `new(2,5320)` es el 3, el de `new(3,345)` es el 4 y el de
`new(1,100)` final es el 5), sin repetir nunca un `ptr` ya usado — se
verificó explícitamente contra este caso.

## Qué se corrigió respecto a la versión anterior

Esta versión del backend fue reescrita a fondo a partir de una revisión
contra el enunciado. Los problemas encontrados y corregidos:

1. **`ptr` no era único globalmente** — se calculaba como
   `total_simbolos_del_proceso + 1`, así que dos procesos distintos
   podían recibir el mismo `ptr` (ej. PID 1 y PID 2 ambos con `ptr=1`).
   Ahora el mapa de `ptr → páginas` vive en la MMU (no en el proceso),
   con un contador global monótono que nunca se reutiliza, ni siquiera
   tras un `delete()`.
2. **El reloj se contaba doble** — el código sumaba `+1s` fijo después
   de cada instrucción, además de lo que ya sumaban `mmu_new`/`mmu_use`
   internamente. Ahora el reloj solo lo actualiza la MMU, con la regla
   exacta del enunciado (hit = +1s, fallo = +5s + thrashing, páginas
   nuevas siempre cuentan como fallo).
3. **Solo FIFO y Second Chance estaban realmente implementados** — LRU,
   LFU y OPT caían al mismo caso genérico que FIFO (usaban los campos de
   metadatos, pero nunca los consultaban para decidir la víctima). Ahora
   cada algoritmo tiene su propia lógica de selección de víctima,
   incluyendo OPT con *lookahead* real sobre las instrucciones futuras.
   Se verificó que OPT siempre iguala o supera (reloj menor o igual) a
   los demás algoritmos en pruebas con cientos de instrucciones.
4. **`parser.c`/`parser.h` estaban vacíos** — no existía forma de leer
   un archivo de operaciones ni de generar una secuencia aleatoria. Se
   implementaron ambos, con generación reproducible por semilla.
5. **Arreglos de tamaño fijo** que limitaban la cantidad de símbolos
   (`ptr`) rastreables — ahora se reserva dinámicamente con `malloc`,
   dimensionado según la cantidad real de instrucciones.
6. **La simulación no corría en modo no interactivo** — esperar `ENTER`
   con la entrada estándar redirigida (como hace un script de
   calificación) provocaba que la simulación terminara tras el primer
   paso al leer `EOF`. Se detecta ahora si `stdin` es una terminal real
   y, si no lo es, se corre de corrido.

Se verificó con `gcc -fsanitize=address,undefined` corriendo los 5
algoritmos sobre cientos de instrucciones generadas aleatoriamente, sin
errores de memoria ni fugas.

## Limitación conocida: `frontend/index.html`

El archivo `frontend/index.html` sigue siendo el prototipo visual
original: es una maqueta estática en HTML/JS que **no está conectada al
backend en C** (usa `Math.random()` para simular datos). Conectarlo de
verdad requeriría un servidor HTTP en C (o similar) sirviendo la
simulación por una API, lo cual es un cambio de arquitectura mucho más
grande y riesgoso de entregar sin poder probarlo a fondo en el tiempo
disponible.

En su lugar, el backend en C ahora imprime un **dashboard de texto**
completo después de cada instrucción (barra de ocupación de RAM, tabla
de páginas estilo Anexo 2, y métricas), para el algoritmo del usuario y
para OPT en paralelo, lo cual cubre la parte funcional/algorítmica de la
visualización pedida en el enunciado — pero no es una interfaz gráfica.
Si el rubro de "diseño de interfaz" pesa en la calificación, vale la
pena mencionarlo con el profesor o, si hay tiempo después de la entrega,
conectar el frontend es el siguiente paso natural.
