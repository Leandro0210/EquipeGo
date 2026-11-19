# Feature 08 — Reduction (etapa CPU)

**Objetivo:** seleccionar el courier con menor ETA mediante reduccion `argmin`.

- Regla determinista: menor ETA; empates por menor `courierIndex`.
- Entradas no disponibles, negativas o no finitas se descartan.
- CPU secuencial y CPU multihilo como referencias de la futura version CUDA.
- Integracion: `Dispatcher::assignBestCourier` usa una reduccion para elegir al
  primer candidato; si `Courier::tryAssignOrder` falla, conserva la ordenacion
  de respaldo y reintenta con los demas candidatos.
- La decision GPU futura debe seguir siendo un consejo; la asignacion final
  se valida bajo los bloqueos originales del courier (check-then-act).
- No se modifica `master`. No se crea commit antes de terminar toda la feature.

**Verificacion CPU independiente:**

```bash
g++ -std=c++17 -O2 -pthread -Isrc tests/test_eta_reduction.cpp -o /tmp/test_eta_reduction
/tmp/test_eta_reduction
```

**Pendiente:** backend CUDA, unificacion del hilo propietario, pruebas GPU,
benchmark de 5 tamaños y 10 repeticiones, perfilado y analisis.
