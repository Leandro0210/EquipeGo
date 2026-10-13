#!/usr/bin/env python3
"""Verificacion de contrato de arranque normal y atribucion en DP12.1.

Este test revisa el codigo fuente. No sustituye la verificacion GUI visual.
"""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
main = (root / 'src/main.cpp').read_text(encoding='utf-8')
dashboard = (root / 'src/visualization/SimulationDashboard.cpp').read_text(encoding='utf-8')
checks = {
    'GUI por defecto': 'const bool showGui = options.display != DisplayMode::Headless;' in main,
    'configuracion de timeScale': 'config.simulation.timeScale = 3.0;' not in main,
    'sobrescritura explicita': 'if (options.hasScale)' in main,
    'atribucion configurada': 'std::string attributionText = config.map.attribution;' in dashboard,
    'copyright renderizado': 'cv::circle(frame, center, 8' in dashboard,
    'texto renderizado': 'cv::putText(frame, attributionText' in dashboard,
}
for name, good in checks.items():
    print(f'{"OK" if good else "FALLO"}: {name}')
sys.exit(0 if all(checks.values()) else 1)
