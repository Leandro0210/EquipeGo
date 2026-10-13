# DP12.3.1 — CPU ociosa tras calentamiento

La medicion excluye el arranque y se obtiene de /proc/PID/stat (CPU de todos los hilos).
100% representa un nucleo completo; la rubrica no define un umbral numerico.

| Motos | Modo | CPU estable (%) | CPU estable (s) | Pared medida (s) |
|---:|---|---:|---:|---:|
| 64 | headless | 0.000 | 0.000 | 20.00 |
| 64 | gui | 21.200 | 4.240 | 20.00 |

El modo headless es solo diagnostico. La rubrica exige que la aplicacion evaluada abra ventana.
Esta medicion no demuestra por si sola ausencia de spinning; revisar codigo y TSAN.
