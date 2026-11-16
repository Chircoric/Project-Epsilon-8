REM SPDX-License-Identifier: AGPL-3.0-only
REM Copyright (C) 2026 Chircoric
@echo off
rem Diagnostico de Nebulosa: dice que tarjeta grafica tiene este equipo, si el juego puede abrirla
rem y cuantos fotogramas por segundo da en calidad Baja. Deja el resultado en diagnostico.txt
rem junto a este archivo: se puede enviar a quien le dio el juego.
chcp 65001 > nul
cd /d "%~dp0"
epsilon_game.exe --diagnostico > diagnostico.txt 2>&1
type diagnostico.txt
echo.
echo El informe esta en %~dp0diagnostico.txt
pause
