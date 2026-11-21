#!/usr/bin/env python3
"""Genera gráficos y tabla a partir de los CSV reales de Colab y laptop.
Uso: python3 plot_histograma07.py colab07.csv laptop07.csv --out-dir resultados07
"""
import argparse
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd

parser = argparse.ArgumentParser()
parser.add_argument('colab_csv')
parser.add_argument('laptop_csv')
parser.add_argument('--out-dir', default='resultados07')
args = parser.parse_args()
out = Path(args.out_dir)
out.mkdir(parents=True, exist_ok=True)

colab = pd.read_csv(args.colab_csv)
laptop = pd.read_csv(args.laptop_csv)
df = pd.concat([colab, laptop], ignore_index=True)
assert (df.correct == 1).all(), 'Hay resultados incorrectos; NO generar conclusiones'
summary = df.groupby(['machine','variant','size','threads'], as_index=False).agg(
    mediana_ms=('time_ms', 'median'), minimo_ms=('time_ms', 'min'),
    maximo_ms=('time_ms', 'max'),
    kernel_mediano_ms=('kernel_ms', 'median'),
    solicitud_mediana_ms=('request_ms', 'median'),
)
summary.to_csv(out / 'resumen_mediciones07.csv', index=False)

# Comparación principal: mejor CPU multihilo de la laptop vs GPU total de Colab.
laptop_best = summary[(summary.machine=='laptop') & (summary.variant=='cpu_multithread')]
laptop_best = laptop_best.loc[laptop_best.groupby('size').mediana_ms.idxmin()].sort_values('size')
colab_gpu = summary[(summary.machine=='colab') & (summary.variant.str.startswith('gpu_'))]

plt.figure(figsize=(9,5.5))
laptop_seq=summary[(summary.machine=='laptop') & (summary.variant=='cpu_1thread')].sort_values('size')
plt.loglog(laptop_seq['size'], laptop_seq['mediana_ms'], 'o-', label='CPU 1 hilo (laptop)')
plt.loglog(laptop_best['size'], laptop_best['mediana_ms'], 'o-', label='Mejor CPU multihilo (laptop)')
for name, data in colab_gpu.groupby('variant'):
    data=data.sort_values('size')
    plt.loglog(data['size'], data['mediana_ms'], 'o-', label=f'{name} total (Colab T4)')
    plt.loglog(data['size'], data['kernel_mediano_ms'], 'o--', label=f'{name} kernel (Colab T4)')
plt.title('Feature 07: duración por tamaño de entrada')
plt.xlabel('Cantidad de entregas sintéticas (N)')
plt.ylabel('Tiempo mediano (ms)')
plt.grid(True, which='both', alpha=0.25)
plt.legend()
plt.tight_layout()
plt.savefig(out / '07_tiempo_vs_tamano.png', dpi=170)
plt.close()

# Separa el kernel del resto del tiempo total (estimación por diferencia de medianas).
plt.figure(figsize=(9,5.5))
for name, data in colab_gpu.groupby('variant'):
    data=data.sort_values('size')
    pct=100*(data['mediana_ms']-data['kernel_mediano_ms'])/data['mediana_ms']
    plt.semilogx(data['size'], pct, 'o-', label=name)
plt.title('Feature 07: proporción del tiempo total fuera del kernel')
plt.xlabel('Cantidad de entregas sintéticas (N)')
plt.ylabel('Porcentaje del tiempo GPU total (%)')
plt.ylim(0, 105)
plt.grid(True, alpha=0.25)
plt.legend()
plt.tight_layout()
plt.savefig(out / '07_costos_fuera_kernel.png', dpi=170)
plt.close()

best_gpu=colab_gpu.loc[colab_gpu.groupby('size').mediana_ms.idxmin()].sort_values('size')
compare=laptop_best[['size','threads','mediana_ms']].rename(columns={
    'threads':'mejores_hilos_laptop','mediana_ms':'cpu_laptop_ms'
}).merge(best_gpu[['size','variant','mediana_ms']].rename(columns={
    'variant':'gpu_variante','mediana_ms':'gpu_total_ms'
}),on='size',how='inner')
compare['speedup_total_vs_laptop'] = compare.cpu_laptop_ms / compare.gpu_total_ms
compare.to_csv(out / '07_comparacion_principal.csv',index=False)
colab_cpu=summary[(summary.machine=='colab') & (summary.variant=='cpu_multithread')]
colab_cpu=colab_cpu.loc[colab_cpu.groupby('size').mediana_ms.idxmin()].sort_values('size')
colab_compare=colab_cpu[['size','threads','mediana_ms']].rename(columns={
    'threads':'mejores_hilos_colab', 'mediana_ms':'cpu_colab_ms'
}).merge(best_gpu[['size','variant','mediana_ms']].rename(columns={
    'variant':'gpu_variante','mediana_ms':'gpu_total_ms'
}),on='size',how='inner')
colab_compare['speedup_misma_maquina'] = colab_compare.cpu_colab_ms / colab_compare.gpu_total_ms
colab_compare.to_csv(out / '07_comparacion_colab.csv',index=False)
print('\nCOMPARACION PRINCIPAL (laptop CPU vs Colab GPU)')
print(compare.to_string(index=False))
print('\nCOMPARACION MISMA MAQUINA (Colab CPU vs Colab GPU)')
print(colab_compare.to_string(index=False))
print('\nArchivos creados en:',out)
print('AVISO: laptop y Colab son máquinas diferentes. Reportar también la comparación dentro de Colab.')
