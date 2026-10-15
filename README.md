# Simulador de Memoria con Paginación

Simulador de una MMU (Memory Management Unit) con paginación: 400 KB de
RAM física (100 marcos de 4 KB) y una "V-RAM" infinita en disco para las
páginas desalojadas. Corre dos simulaciones en paralelo sobre la misma
secuencia de instrucciones — el algoritmo de reemplazo elegido por el
usuario vs. el algoritmo Óptimo (OPT/Belady) — para poder comparar.

## Características

- **Interfaz gráfica nativa (GTK4)**: ventana de configuración (algoritmo,
  semilla, archivo o generación aleatoria) que al iniciar abre una
  **nueva ventana** con la simulación en vivo — dos paneles lado a lado
  (algoritmo elegido vs. OPT), cada uno con su barra de ocupación de RAM
  coloreada por proceso, tabla de páginas con scroll (estilo Anexo 2) y
  tarjetas de métricas, más controles de reproducir/pausar/avanzar un
  paso y control de velocidad.
- **5 algoritmos de reemplazo de páginas, cada uno con lógica propia:**
  FIFO, Second Chance (SC), LRU, LFU y OPT (óptimo, con *lookahead* real
  sobre las instrucciones futuras). Solo FIFO/SC/LRU/LFU son
  seleccionables por el usuario; OPT corre siempre en paralelo como
  referencia, tal como lo pide el enunciado.
- **Mapa de punteros (`ptr`) global y nunca reutilizado**, incluso tras un
  `delete()`, tal como exige el enunciado.
- **Reloj y thrashing simulados:** +1 s por *hit*, +5 s por *fallo* de
  página (las páginas nuevas siempre cuentan como fallo), con alerta en
  rojo cuando el thrashing supera el 50 % del tiempo total.
- **Fragmentación interna**, procesos activos, páginas cargadas/
  descargadas y uso de RAM/V-RAM (con el V-RAM % pudiendo superar el
  100 %, tal como lo ilustra el enunciado) reportados en vivo.
- **Parser de archivos de operaciones** (formato Anexo 1: `new(pid,size)`,
  `use(ptr)`, `delete(ptr)`, `kill(pid)`), tolerante a espacios y saltos
  de línea `CRLF`/`LF`, que ignora líneas en blanco y comentarios (`#`).
- **Generador de operaciones aleatorio y reproducible** (misma semilla →
  mismo archivo), respetando las reglas del enunciado y guardando el
  archivo generado en disco *antes* de arrancar la simulación (su ruta
  queda visible en la propia ventana de la simulación).
- **Modo paso a paso o automático**, con pausa/reanudación, tanto en la
  interfaz gráfica como en el modo de texto/línea de comandos de
  respaldo.

## Compilación (Debian 13 / cualquier Linux con GTK4)

La interfaz gráfica usa GTK4. En Debian 13 (o cualquier Debian/Ubuntu
reciente) se instala con:

```bash
sudo apt install build-essential pkg-config libgtk-4-dev
```

Y se compila así (sin más dependencias):

```bash
cd carpeta-descomprimida-con-el-proyecto
gcc main.c mmu.c simulacion.c parser.c gui.c -o simulador $(pkg-config --cflags --libs gtk4)
```

Se probó explícitamente con `gcc -std=c11 -pedantic -Wall -Wextra`: compila
sin errores (solo avisos de "deprecated" de la propia GTK4 por usar
`GtkTreeView`/`GtkComboBoxText` en vez de los widgets más nuevos
`GtkColumnView`/`GtkDropDown`, que siguen siendo API estable y soportada).

## Ejecución

### Interfaz gráfica (modo por defecto)

```bash
./simulador
```

Abre la ventana de configuración: ahí se elige el algoritmo (FIFO, SC,
LRU o LFU — OPT corre siempre en paralelo, no se selecciona), y se
indica si se carga un archivo de operaciones existente (formato Anexo 1,
con selector de archivos) o se genera uno aleatorio nuevo (semilla,
cantidad de procesos `P`, cantidad total de operaciones `N`). Al
presionar "Iniciar simulación" se abre la ventana de la simulación, con
los dos paneles en vivo y los controles de reproducir / pausar / avanzar
un paso / velocidad.

**Controles de la ventana de simulación** (barra superior, siempre visible):

| Control | Qué hace | Tecla |
|---|---|---|
| ▶ Reproducir / ⏸ Pausar | Avanza sola, una instrucción tras otra; vuelve a presionar para pausar y reanudar | `Espacio` |
| ⏭ Avanzar un paso | Ejecuta una sola instrucción (solo con la simulación en pausa) | `→` (flecha derecha) |
| Velocidad (lenta ↔ rápida) | Deslizador que cambia el tiempo entre instrucciones en modo reproducir | — |

La ventana se ajusta al tamaño de la pantalla; si aun así el contenido no
cabe, aparece una barra de desplazamiento vertical.

Se verificó visualmente y funcionalmente: corridas
cortas, corridas completas de cientos de instrucciones hasta el final, y
bajo `gcc -fsanitize=address` interactuando con la interfaz, sin errores
ni fugas de memoria.

### Modo de texto / línea de comandos (respaldo, sin ventana)

Útil para correr el archivo de pruebas sin depender de la
interfaz gráfica, o si por algún motivo no hay entorno de escritorio
disponible:

```bash
./simulador --consola                               # menus por texto en la terminal

./simulador fifo --archivo ops.txt                   # con un archivo existente
./simulador lru  --archivo ops.txt --auto

./simulador opt --generar 42 10 200                  # generando una secuencia nueva
```

`<algoritmo>` es uno de: `fifo | sc | lru | lfu | opt` (sin distinguir
mayúsculas; en el modo CLI sí se acepta `opt` además de FIFO/SC/LRU/LFU,
por si se quiere comparar OPT contra sí mismo). `--auto` avanza solo, en
vez de pedir `ENTER` por paso.

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
verificó explícitamente contra este caso, tanto por línea de comandos
como cargando el mismo archivo desde la interfaz gráfica.

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
7. **No existía ninguna interfaz gráfica real** — solo un prototipo HTML
   desconectado. Se construyó una interfaz de escritorio en
   GTK4 (`gui.c`) que reutiliza exactamente el mismo motor ya probado en
   `mmu.c`/`parser.c` (no duplica lógica de simulación), con el flujo de
   dos ventanas que pide el enunciado y la visualización lado a lado
   estilo Anexo 2.

Se verificó con `gcc -fsanitize=address,undefined` corriendo los 5
algoritmos sobre cientos de instrucciones generadas aleatoriamente (modo
texto) y también interactuando con la interfaz gráfica (clics reales de
reproducir/pausar/avanzar simulados con `xdotool` sobre un X virtual),
sin errores de memoria ni fugas en ningún caso.
