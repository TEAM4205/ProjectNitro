# 09 - Reproducción y suavizado

Cómo el Replayer reconstruye el movimiento grabado. Código en
[replayer.hpp](../../include/nitrogen/recordnreplay/replayer.hpp) y
[smoothing.hpp](../../include/nitrogen/recordnreplay/smoothing.hpp).

## El problema

El archivo guarda un frame cada ~10 ms, pero **no exactamente** cada 10 ms: VEXos a veces entrega tramas de
4 o 6 ms. Y al reproducir, el robot quiere un valor en *su* propio ritmo, que no tiene por qué coincidir.

Por eso el Replayer **no** pasa de un frame al siguiente de golpe. En su lugar, mantiene un reloj propio y, en cada
instante, calcula **cuánto valdría** cada canal en ese momento exacto, a partir de los frames vecinos.

## El reloj

```
 tiempo grabado:    t=0      t=9.6ms     t=19.5ms    t=29.4ms     ...   (tiempos reales de cada frame)
 frames:            [F0]──────[F1]────────[F2]─────────[F3]
 reloj del replay:         ▲ ahora (T)
```

1. Al empezar, el reloj toma como cero el tiempo del **primer frame** grabado (no hay una espera inicial).
2. En cada paso, `T = tiempo_primer_frame + (microsegundos transcurridos × speed)`.
3. Se buscan los dos frames que rodean a `T`: `p1` (justo antes) y `p2` (justo después).
4. Se calcula `u = (T - t1) / (t2 - t1)`, un número de 0 a 1 que dice qué tan cerca de `p2` está `T`.
5. Con `p1`, `p2` y `u` (y a veces dos frames más, `p0` y `p3`) se calcula el valor de cada canal.
6. Se aplican esos valores a los objetos y se espera hasta el siguiente periodo (`delay_until`).

Si el lector de la SD se atrasa, el reproductor espera el siguiente bloque. El reloj sigue corriendo mientras
tanto, así que al llegar los datos se salta lo que ya pasó: **el tiempo del movimiento nunca se estira**.

`Replayer::config().speed` multiplica el avance del reloj (0.5 = a mitad de velocidad, 2.0 = el doble).

## Modos de interpolación

`Replayer::config().interp` elige cómo se calcula el valor entre `p1` y `p2`:

| Modo | Fórmula | Cuándo usarlo |
|---|---|---|
| `HOLD` | el frame más **cercano** en el tiempo (`p1` o `p2`, el que esté a menos de medio periodo) | Datos que no deben mezclarse; control grabado |
| `LINEAR` (por defecto) | `p1 + (p2 - p1) × u` | Uso general: recta entre frames |
| `CUBIC` | Catmull-Rom con `p0, p1, p2, p3` | Movimiento más redondeado |

Catmull-Rom pasa **exactamente por todos los frames grabados** y su pendiente es continua, así que no hay
"quiebres" en cada frame. La fórmula (con `u2 = u²`, `u3 = u³`):

```
0.5 × ( 2·p1 + (p2 − p0)·u + (2·p0 − 5·p1 + 4·p2 − p3)·u2 + (3·p1 − p0 − 3·p2 + p3)·u3 )
```

Al principio y al final del archivo, donde no hay frame anterior o siguiente, se repite el frame más cercano.

### Canales especiales

| Canal | Cómo se interpola |
|---|---|
| `DIGITAL` (botones, on/off) | **Nunca**: siempre `HOLD`. No tiene sentido un botón "a medias" |
| `ANGLE` | Por el **camino corto**: de 350° a 10° pasa por 0°, no por 180° |
| Los demás | Según `interp` |

Para los ángulos, el valor se "desenvuelve" antes de interpolar y se vuelve a llevar a 0..360 después. La vuelta
completa se calcula como `360 × 10^exp10` en las unidades guardadas.

## Suavizado extra (filtro EMA)

```cpp
Replayer::config().ema = 0.3;   // 1 = sin filtro; más cerca de 0 = más suave
```

El filtro EMA (media móvil exponencial) acerca el valor poco a poco al objetivo:

```
y = y + alfa × (x − y)
```

- `alfa = 1` → sin filtro (por defecto).
- `alfa = 0.3` → suaviza bastante, pero agrega algo de retraso.
- Se aplica **después** de interpolar y **solo** a canales que no son `DIGITAL` ni `ANGLE`.
- Úsalo para quitar ruido de sensores o movimientos bruscos de un joystick; no para corregir un mal muestreo
  (el archivo guarda los datos crudos, y el suavizado vive solo en la reproducción).

## Aplicar los valores

Cada `period_ms` (10 ms por defecto), para cada objeto ligado:

```
objeto.apply(perfil, valores)       // los valores van en el orden de describe()
```

Los canales de un mismo objeto y perfil se entregan **juntos**, en un solo `apply`.

Qué objetos se ligan a qué canales:

1. Se agrupan los canales seguidos con el mismo objeto y perfil.
2. Se busca un objeto con ese **nombre** (los attacheados con `Recorder::attach`, el control, o los que añadiste
   con `Replayer::bind`).
3. Se comprueba que su perfil devuelve la misma cantidad de canales que el archivo.
4. Si no cumple, ese grupo se **ignora** y se avisa por `on_error`. El resto se reproduce.

## Terminar

La reproducción termina cuando el reloj llega al tiempo del **último frame** (o con `Replayer::stop()`). Entonces:

1. Se llama a `rest()` de cada objeto ligado (los deja en un estado seguro).
2. Se detiene el lector y se libera el anillo.
3. `is_playing()` pasa a `false` y se llama a `on_finish` si lo definiste.

Si pones `loop = true`, vuelve a empezar desde el principio hasta que llames a `stop()`. Un archivo que falla al
abrir no se repite en bucle.

## El control grabado como control virtual

Cuando el archivo trae el control (`master` o `partner`), su `apply` no mueve nada: guarda los valores en un
"control virtual". De ahí lo leen:

- Tu código: `Replayer::analog(...)`, `Replayer::digital(...)`, `Replayer::controller_active()`.
- El `ProfileManager` (carbon): si el control virtual está activo, los perfiles ven **ese** control en lugar del real.
  Así un perfil reacciona a una pulsación grabada igual que a una pulsación en vivo.

Al terminar, `rest()` pone el control virtual en cero y lo desactiva.

## Reproducir una macro

Las macros se reproducen distinto: no hay valores que interpolar, solo eventos que disparar en su momento. Ver
[04 - Modo pasivo](04-modo-pasivo-macro.md#reproducir-una-macro).

## Configuración

| Opción | Por defecto | Qué hace |
|---|---|---|
| `interp` | `LINEAR` | `HOLD`, `LINEAR` o `CUBIC` |
| `ema` | 1.0 | Filtro de suavizado (1 = apagado) |
| `speed` | 1.0 | Escala de tiempo |
| `loop` | `false` | Repetir al terminar |
| `period_ms` | 10 | Cada cuánto se aplican valores / se revisan eventos |
| `ring_bytes` | 256 KB | Lectura adelantada de un `.match` |
| `wait_timeout_ms` | 15000 | Tope de espera de `WAIT_PREV` en macros |
| `player_priority`, `reader_priority` | 13, 12 | Prioridades (antes de `rnr::init()`) |
| `on_finish`, `on_error` | — | Funciones que se llaman al terminar / ante un error |
